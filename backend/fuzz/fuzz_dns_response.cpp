// ============================================================
// backend/fuzz/fuzz_dns_response.cpp
//
// Fuzzer for the whole path an untrusted DNS response takes after it
// arrives: DnsPro::Parser::parse() on the raw datagram, then every check
// decoder applied to every answer exactly as the checks do it (TXT text,
// AAAA address, and MX/NS names read from the original buffer at the
// record's rdata offset, which is how compression pointers resolve).
//
// There is nothing to assert beyond "does not crash or hang": the value is
// in running a hostile response through the same code the live server runs,
// under ASan/UBSan.
// ============================================================

#include "checks/RDataDecode.h"

#include <DnsPro/DnsResolver.h>

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    VectorPro::Vector<std::byte> raw;
    for (std::size_t i = 0; i < size; ++i)
        raw.push_back(static_cast<std::byte>(data[i]));

    DnsPro::Packet::Message message;
    if (size == 0 ||
        DnsPro::Parser::parse(std::span<const std::byte>(&raw[0], raw.size()), message) !=
            DnsPro::Status::OK)
        return 0;

    for (const auto& record : message.answers) {
        (void)DnsCheckup::decodeTxt(record.rdata);
        (void)DnsCheckup::decodeAaaa(record.rdata);
        (void)DnsCheckup::decodeName(raw, record.rdataOffset);     // NS
        (void)DnsCheckup::decodeName(raw, record.rdataOffset + 2); // MX (after the preference)
    }
    return 0;
}
