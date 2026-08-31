/**
 * @file            MissingRecordChecks.h
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
#include <CheckResult.h>        // DnsCheckup::CheckResult, DnsCheckup::CheckStatus
#include <QueryEngine.h>        // DnsCheckup::QueryEngine, DnsCheckup::RecordType, DnsCheckup::QueryStatus
#include <checks/RDataDecode.h> // DnsCheckup::decodeAaaa, DnsCheckup::decodeName

#include <VectorPro/Vector.h> // Vector

#include <cstdint>     // std::uint16_t
#include <string>      // std::string, std::to_string
#include <string_view> // std::string_view
// clang-format on

/**
 * @brief Checks for records a well-configured domain is expected to have
 * (A/AAAA presence, an MX for mail, at least one NS).
 * @details AAAA and MX/NS all show their actual decoded value now, not
 * just a generic "found" message -- AAAA's rdata is a fixed-size raw
 * address (no embedded name, trivial to decode); MX/NS embed a domain
 * name in rdata that can be compression-pointer-encoded (RFC 1035
 * S4.1.4), decoded here via `RDataDecode.h::decodeName()`, which needs
 * both `ResourceRecord::rdataOffset` and `QueryResult::rawResponse` --
 * both added to DnsResolver/QueryEngine specifically to make this
 * possible (a compression pointer only resolves against the original
 * message buffer, not the disconnected `rdata` copy alone).
 */

namespace DnsCheckup::Checks {

using namespace VectorPro;

namespace detail {

/// @brief Joins every found value with ", " instead of showing only
/// the first and summarizing the rest as "(+N more)" -- a domain
/// commonly has 2-4 NS/MX/AAAA records and every one of them is real,
/// useful information for a checkup tool, not overflow to hide.
[[nodiscard]] inline std::string joinAll(const Vector<std::string>& values) {
    std::string joined;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0)
            joined += ", ";
        joined += values[i];
    }
    return joined;
}

} // namespace detail

/// @brief Checks for an AAAA (IPv6) record, showing the actual
/// address(es) found rather than a generic "found" message.
[[nodiscard]] inline CheckResult checkMissingAaaa(QueryEngine& engine, std::string_view domain) {
    QueryResult queryResult = engine.query(domain, RecordType::AAAA);

    if (queryResult.status == QueryStatus::Timeout)
        return {"missing_aaaa", CheckStatus::Warn, "Query for AAAA records timed out."};
    if (queryResult.status != QueryStatus::Ok)
        return {"missing_aaaa", CheckStatus::Warn, "Could not query AAAA records."};

    Vector<std::string> addresses;
    for (const auto& record : queryResult.message.answers) {
        if (record.type != static_cast<std::uint16_t>(RecordType::AAAA))
            continue;
        std::string address = decodeAaaa(record.rdata);
        if (!address.empty()) // Empty means malformed rdata -- skip, don't display it as a value.
            addresses.push_back(address);
    }

    if (addresses.empty())
        return {"missing_aaaa", CheckStatus::Fail, "No AAAA record found."};

    return {"missing_aaaa", CheckStatus::Pass, "AAAA record found: " + detail::joinAll(addresses)};
}

/// @brief Checks for an MX (mail exchange) record, showing the actual
/// exchange hostname(s) found rather than a generic "found" message.
/// @details The exchange name follows a fixed 2-byte preference field
/// in rdata (RFC 1035 S3.3.9), so decoding starts at `rdataOffset + 2`.
[[nodiscard]] inline CheckResult checkMissingMx(QueryEngine& engine, std::string_view domain) {
    QueryResult queryResult = engine.query(domain, RecordType::MX);

    if (queryResult.status == QueryStatus::Timeout)
        return {"missing_mx", CheckStatus::Warn, "Query for MX records timed out."};
    if (queryResult.status != QueryStatus::Ok)
        return {"missing_mx", CheckStatus::Warn, "Could not query MX records."};

    Vector<std::string> exchanges;
    for (const auto& record : queryResult.message.answers) {
        if (record.type != static_cast<std::uint16_t>(RecordType::MX))
            continue;
        std::string exchange = decodeName(queryResult.rawResponse, record.rdataOffset + 2);
        if (!exchange.empty()) // Empty means decoding failed -- skip, don't display it as a value.
            exchanges.push_back(exchange);
    }

    if (exchanges.empty())
        return {"missing_mx", CheckStatus::Fail, "No MX record found."};

    return {"missing_mx", CheckStatus::Pass, "MX record found: " + detail::joinAll(exchanges)};
}

/// @brief Checks for at least one NS (nameserver) record, showing the
/// actual nameserver hostname(s) found rather than a generic "found"
/// message.
/// @details Unlike MX, NS rdata is entirely the nsdname -- no fixed
/// field precedes it, so decoding starts directly at `rdataOffset`.
[[nodiscard]] inline CheckResult checkMissingNs(QueryEngine& engine, std::string_view domain) {
    QueryResult queryResult = engine.query(domain, RecordType::NS);

    if (queryResult.status == QueryStatus::Timeout)
        return {"missing_ns", CheckStatus::Warn, "Query for NS records timed out."};
    if (queryResult.status != QueryStatus::Ok)
        return {"missing_ns", CheckStatus::Warn, "Could not query NS records."};

    Vector<std::string> nameservers;
    for (const auto& record : queryResult.message.answers) {
        if (record.type != static_cast<std::uint16_t>(RecordType::NS))
            continue;
        std::string ns = decodeName(queryResult.rawResponse, record.rdataOffset);
        if (!ns.empty())
            nameservers.push_back(ns);
    }

    if (nameservers.empty())
        return {"missing_ns", CheckStatus::Fail, "No NS record found."};

    return {"missing_ns", CheckStatus::Pass, "NS record found: " + detail::joinAll(nameservers)};
}

/// @brief Runs every missing-record check for `domain`.
[[nodiscard]] inline Vector<CheckResult> runMissingRecordChecks(QueryEngine& engine,
                                                                std::string_view domain) {
    return {
        checkMissingAaaa(engine, domain),
        checkMissingMx(engine, domain),
        checkMissingNs(engine, domain),
    };
}

} // namespace DnsCheckup::Checks
