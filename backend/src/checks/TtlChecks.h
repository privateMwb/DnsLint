/**
 * @file            TtlChecks.h
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
#include <CheckResult.h> // DnsCheckup::CheckResult, DnsCheckup::CheckStatus
#include <QueryEngine.h> // DnsCheckup::QueryEngine, DnsCheckup::RecordType, DnsCheckup::QueryStatus

#include <VectorPro/Vector.h> // Vector

#include <cstdint>     // std::uint32_t
#include <string>      // std::string, std::to_string
#include <string_view> // std::string_view
// clang-format on

/**
 * @brief Checks whether a domain's TTLs fall inside a sane range.
 * @details Reads only `ResourceRecord::ttl` and `ResourceRecord::type`,
 * never `rdata`, so this file has no dependency on `RDataDecode.h`.
 */

namespace DnsCheckup::Checks {

using namespace VectorPro;

namespace detail {

/// @brief Below this, a record is re-fetched from authoritative servers
/// so often it adds needless query load for a value that (outside active
/// migrations) rarely changes this fast.
constexpr std::uint32_t kTooLowTtlSeconds = 300; // 5 minutes.

/// @brief Above this, a stale record left over from a since-corrected
/// misconfiguration would linger in caches for an inconveniently long
/// time before the fix takes effect everywhere.
constexpr std::uint32_t kTooHighTtlSeconds = 172800; // 48 hours.

/// @brief Runs one TTL-range check and builds its `CheckResult`.
[[nodiscard]] inline CheckResult checkTtlRange(QueryEngine& engine, std::string_view domain,
                                               RecordType type, const char* name,
                                               const char* recordLabel) {
    QueryResult queryResult = engine.query(domain, type);

    if (queryResult.status == QueryStatus::Timeout)
        return {name, CheckStatus::Warn,
                std::string("Query for ") + recordLabel + " records timed out."};

    if (queryResult.status != QueryStatus::Ok)
        return {name, CheckStatus::Warn,
                std::string("Could not query ") + recordLabel + " records."};

    auto wanted = static_cast<std::uint16_t>(type);
    std::uint32_t lowestTtl = 0;
    bool found = false;

    for (const auto& record : queryResult.message.answers) {
        if (record.type != wanted)
            continue;
        if (!found || record.ttl < lowestTtl)
            lowestTtl = record.ttl;
        found = true;
    }

    if (!found)
        return {name, CheckStatus::Warn,
                std::string("No ") + recordLabel + " record to check TTL on."};

    if (lowestTtl < kTooLowTtlSeconds)
        return {name, CheckStatus::Warn,
                std::string(recordLabel) + " TTL is " + std::to_string(lowestTtl) +
                    "s, unusually low -- this adds query load for a value that rarely changes this "
                    "fast."};

    if (lowestTtl > kTooHighTtlSeconds)
        return {name, CheckStatus::Warn,
                std::string(recordLabel) + " TTL is " + std::to_string(lowestTtl) +
                    "s, unusually high -- a future fix to this record would take a long time to "
                    "propagate."};

    return {name, CheckStatus::Pass,
            std::string(recordLabel) + " TTL is " + std::to_string(lowestTtl) + "s."};
}

} // namespace detail

/// @brief Checks the TTL on the domain's A record(s).
[[nodiscard]] inline CheckResult checkTtlA(QueryEngine& engine, std::string_view domain) {
    return detail::checkTtlRange(engine, domain, RecordType::A, "ttl_a", "A");
}

/// @brief Checks the TTL on the domain's MX record(s).
[[nodiscard]] inline CheckResult checkTtlMx(QueryEngine& engine, std::string_view domain) {
    return detail::checkTtlRange(engine, domain, RecordType::MX, "ttl_mx", "MX");
}

/// @brief Runs every TTL check for `domain`.
[[nodiscard]] inline Vector<CheckResult> runTtlChecks(QueryEngine& engine,
                                                      std::string_view domain) {
    return {
        checkTtlA(engine, domain),
        checkTtlMx(engine, domain),
    };
}

} // namespace DnsCheckup::Checks
