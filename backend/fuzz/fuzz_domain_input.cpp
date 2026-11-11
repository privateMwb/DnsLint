// ============================================================
// backend/fuzz/fuzz_domain_input.cpp
//
// Fuzzer for the text a visitor submits to POST /api/check and
// GET /api/history. The domain is attacker-controlled, and it is
// cleaned (normalizeDomain), vetted (isAcceptableDomain) and reduced
// (apexOf) before it is put into a real DNS query.
//
// Checked for every input:
//   - normalizeDomain() never leaves whitespace around the result,
//     uppercase letters, a scheme, or any of / ? # : in it,
//   - an accepted domain is non-empty, within the length cap, and
//     contains a dot,
//   - apexOf() returns a suffix of its input with at most two labels.
// ============================================================

#include "routes/CheckRoutes.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace DnsCheckup::Routes::detail;

    const std::string input(reinterpret_cast<const char*>(data), size);
    const std::string domain = normalizeDomain(input);

    if (domain.find_first_of("/?#:") != std::string::npos)
        __builtin_trap();
    if (std::any_of(domain.begin(), domain.end(),
                    [](unsigned char c) { return std::isupper(c) != 0; }))
        __builtin_trap();
    if (!domain.empty() && (std::isspace(static_cast<unsigned char>(domain.front())) != 0 ||
                            std::isspace(static_cast<unsigned char>(domain.back())) != 0))
        __builtin_trap();

    if (isAcceptableDomain(domain)) {
        if (domain.empty() || domain.size() > kMaxDomainLength ||
            domain.find('.') == std::string::npos)
            __builtin_trap();
    }

    const std::string apex = apexOf(domain);
    if (apex.size() > domain.size() ||
        domain.compare(domain.size() - apex.size(), apex.size(), apex) != 0)
        __builtin_trap();
    if (std::count(apex.begin(), apex.end(), '.') > 1)
        __builtin_trap();

    return 0;
}
