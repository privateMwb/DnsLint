/**
 * @file            EmailSecurityChecks.h
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
#include <checks/RDataDecode.h> // DnsCheckup::decodeTxt

#include <VectorPro/Vector.h> // Vector

#include <array>       // std::array
#include <cstdint>     // std::uint16_t
#include <string>      // std::string
#include <string_view> // std::string_view
// clang-format on

/**
 * @brief SPF, DMARC, and (best-effort) DKIM checks -- all read TXT record
 * content, unlike `MissingRecordChecks.h`'s presence-only checks, so this
 * file depends on `RDataDecode.h`.
 */

namespace DnsCheckup::Checks {

using namespace VectorPro;

namespace detail {

/// @brief Queries TXT records at `name` and returns every decoded TXT
/// value found, or an empty vector on any query failure.
[[nodiscard]] inline Vector<std::string> queryTxtValues(QueryEngine& engine,
                                                        std::string_view name) {
    Vector<std::string> values;

    QueryResult queryResult = engine.query(name, RecordType::TXT);
    if (queryResult.status != QueryStatus::Ok)
        return values;

    for (const auto& record : queryResult.message.answers)
        if (record.type == static_cast<std::uint16_t>(RecordType::TXT))
            values.push_back(decodeTxt(record.rdata));

    return values;
}

/// @brief Returns a pointer to the first decoded TXT value starting
/// with `prefix`, or nullptr if none match.
/// @details A pointer (not a bool) so callers can show the actual
/// matched record content in the pass message, rather than a generic
/// "found" -- the value the caller wanted was decoded here, no reason
/// to discard it and report a boolean instead.
[[nodiscard]] inline const std::string* findStartsWith(const Vector<std::string>& values,
                                                       std::string_view prefix) {
    for (const auto& value : values)
        if (value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0)
            return &value;

    return nullptr;
}

} // namespace detail

/// @brief Checks for an SPF record (a TXT record on the domain itself
/// starting with "v=spf1"), showing its actual content on a pass.
[[nodiscard]] inline CheckResult checkSpf(QueryEngine& engine, std::string_view domain) {
    Vector<std::string> values = detail::queryTxtValues(engine, domain);

    if (const std::string* match = detail::findStartsWith(values, "v=spf1"))
        return {"spf_present", CheckStatus::Pass, *match};

    return {"spf_present", CheckStatus::Fail, "No SPF record found."};
}

/// @brief Checks for a DMARC record (a TXT record at "_dmarc.<domain>"
/// starting with "v=DMARC1"), showing its actual content on a pass.
[[nodiscard]] inline CheckResult checkDmarc(QueryEngine& engine, std::string_view domain) {
    std::string dmarcName = "_dmarc." + std::string(domain);
    Vector<std::string> values = detail::queryTxtValues(engine, dmarcName);

    if (const std::string* match = detail::findStartsWith(values, "v=DMARC1"))
        return {"dmarc_present", CheckStatus::Pass, *match};

    return {"dmarc_present", CheckStatus::Fail, "No DMARC record found."};
}

/// @brief Best-effort DKIM check against a short list of commonly used
/// selectors, showing the actual matched record content on a pass.
/// @details DKIM has no fixed record location -- it lives at
/// "<selector>._domainkey.<domain>", and the selector is chosen by
/// whatever mail provider set it up, with no way to discover it from DNS
/// alone. This tries a handful of selectors real-world providers commonly
/// default to (Google Workspace, Microsoft 365, generic "default"/"k1")
/// and reports Pass only if one of those specific selectors resolves.
/// Anything else returns Warn, not Fail -- an unrecognized selector means
/// "not detectable by this tool", not "DKIM is missing", and reporting it
/// as a hard failure would be misleading for a domain that's actually
/// configured correctly under a selector this list doesn't know about.
[[nodiscard]] inline CheckResult checkDkim(QueryEngine& engine, std::string_view domain) {
    static constexpr std::array<const char*, 4> kCommonSelectors = {"google", "selector1",
                                                                    "default", "k1"};

    for (const char* selector : kCommonSelectors) {
        std::string selectorName = std::string(selector) + "._domainkey." + std::string(domain);
        Vector<std::string> values = detail::queryTxtValues(engine, selectorName);

        if (const std::string* match = detail::findStartsWith(values, "v=DKIM1"))
            return {"dkim_present", CheckStatus::Pass,
                    std::string("(selector \"") + selector + "\") " + *match};
    }

    return {
        "dkim_present", CheckStatus::Warn,
        "No DKIM record found under common selectors (google, selector1, default, k1). This "
        "domain may still have DKIM configured under a different selector this tool can't detect."};
}

/// @brief Runs every email-security check for `domain`.
[[nodiscard]] inline Vector<CheckResult> runEmailSecurityChecks(QueryEngine& engine,
                                                                std::string_view domain) {
    return {
        checkSpf(engine, domain),
        checkDmarc(engine, domain),
        checkDkim(engine, domain),
    };
}

} // namespace DnsCheckup::Checks
