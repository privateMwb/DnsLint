/**
 * @file            RDataDecode.h
 *
 * @date            2026-8-27
 *
 * @version         1.0.0
 *
 * @copyright       Copyright (c) 2026 privateMwb
 *                  All rights reserved.
 *                  https://github.com/privateMwb/DnsCheckup
 *
 * @attention       This source is released under the MIT license
 *                  SPDX-License-Identifier: MIT
 *                  <http://opensource.org/licenses/MIT>
 */

#pragma once

// clang-format off
#include <DnsPro/DnsResolver.h> // DnsPro::Parser, DnsPro::Packet::Name, DnsPro::Status

#include <VectorPro/Vector.h> // VectorPro::Vector

#include <cstddef> // std::byte, std::size_t, std::to_integer
#include <cstdio>  // std::snprintf
#include <span>    // std::span
#include <string>  // std::string
// clang-format on

/**
 * @brief Decodes the untyped `ResourceRecord::rdata` byte blob for record
 * types `checks/` needs the actual content of, not just its presence.
 * @details `DnsPro` does zero semantic parsing per record type -- `rdata`
 * is raw bytes for every record, TXT included (confirmed in
 * `ResourceRecord.h`). Only TXT decoding lives here: `MissingRecordChecks.h`
 * and `TtlChecks.h` only need a record's type/ttl fields, never its rdata,
 * so they have no reason to depend on this file. If a future check needs
 * AAAA-as-IPv6 or MX preference/exchange decoding, add it here rather than
 * duplicating ad hoc byte-parsing in that check file.
 */

namespace DnsCheckup {

/**
 * @brief Decodes a TXT record's rdata into its plain string content.
 * @param rdata Raw rdata bytes from a `ResourceRecord` of type TXT (16).
 * @return The concatenated text, with the DNS character-string framing
 * removed.
 * @details TXT rdata is one or more length-prefixed "character-strings"
 * (RFC 1035 S3.3, S3.3.14) back to back -- a single 1-byte length
 * followed by that many bytes of text, repeated until rdata is exhausted.
 * A resolver may split "v=spf1 include:_spf.example.com ~all" across
 * multiple character-strings even though it reads as one value; this
 * strips every length byte and concatenates the text with no separator,
 * so the caller sees one continuous string to run prefix checks
 * (`"v=spf1"`, `"v=DMARC1"`) against.
 *
 * A malformed record (a length byte claiming more bytes than remain)
 * stops decoding at that point and returns whatever was decoded so far,
 * rather than reading past the end of `rdata`.
 */
[[nodiscard]] inline std::string decodeTxt(const VectorPro::Vector<std::byte>& rdata) {
    std::string text;
    std::size_t i = 0;

    while (i < rdata.size()) {
        auto length = std::to_integer<std::size_t>(rdata[i]);
        ++i;

        if (i + length > rdata.size())
            break; // Malformed: length byte overruns rdata. Stop, don't overread.

        for (std::size_t j = 0; j < length; ++j)
            text.push_back(std::to_integer<char>(rdata[i + j]));

        i += length;
    }

    return text;
}

/**
 * @brief Decodes an AAAA record's rdata into its human-readable IPv6
 * address.
 * @param rdata Raw rdata bytes from a `ResourceRecord` of type AAAA (28).
 * @return The address as 8 colon-separated hex groups (leading zeros in
 * each group stripped), or an empty string if `rdata` isn't exactly the
 * 16 bytes an IPv6 address requires -- the empty string doubles as "no
 * value to show" for callers, since a malformed AAAA rdata is not a
 * value this tool should try to display.
 * @details Unlike MX/NS, AAAA rdata is a fixed-size raw address with no
 * embedded domain name, so (unlike those) it needs no name-decompression
 * support to decode -- this is a complete decode, not a partial one.
 */
[[nodiscard]] inline std::string decodeAaaa(const VectorPro::Vector<std::byte>& rdata) {
    if (rdata.size() != 16)
        return {};

    std::string text;
    for (std::size_t group = 0; group < 8; ++group) {
        auto hi = std::to_integer<unsigned>(rdata[group * 2]);
        auto lo = std::to_integer<unsigned>(rdata[group * 2 + 1]);
        unsigned value = (hi << 8) | lo;

        char buf[5];
        std::snprintf(buf, sizeof(buf), "%x", value);
        text += buf;
        if (group != 7)
            text += ':';
    }
    return text;
}

/**
 * @brief Decodes a domain name embedded inside a record's rdata (an MX
 * record's exchange, an NS record's nsdname, ...) -- something no other
 * function in this file can do, since those names can be compression
 * pointers (RFC 1035 S4.1.4) that only resolve against the *original*
 * message buffer, not the disconnected `rdata` copy `ResourceRecord`
 * carries.
 * @param buffer The full raw response, as returned in
 * `QueryResult::rawResponse` -- must be the same buffer `offset` was
 * computed against.
 * @param offset Absolute byte offset in `buffer` where the name starts.
 * For NS, this is `record.rdataOffset` directly (nsdname is the entire
 * rdata). For MX, it's `record.rdataOffset + 2` (the exchange name
 * follows a 2-byte preference field).
 * @return The name as dot-joined labels (e.g. "mail.example.com"), or
 * an empty string if `buffer` is empty or decoding fails -- callers
 * treat an empty string as "no value to show", same convention as
 * `decodeAaaa()`.
 */
[[nodiscard]] inline std::string decodeName(const VectorPro::Vector<std::byte>& buffer,
                                            std::size_t offset) {
    if (buffer.size() == 0)
        return {};

    std::span<const std::byte> span(&buffer[0], buffer.size());
    DnsPro::Packet::Name name;
    std::size_t cursor = offset;
    DnsPro::Status status = DnsPro::Parser::parseName(span, cursor, name);
    if (status != DnsPro::Status::OK)
        return {};

    std::string text;
    for (std::size_t i = 0; i < name.labels.size(); ++i) {
        if (i != 0)
            text += '.';
        text += name.labels[i];
    }
    return text;
}

} // namespace DnsCheckup
