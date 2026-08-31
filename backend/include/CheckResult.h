/**
 * @file            CheckResult.h
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
#include <string> // std::string
// clang-format on

/**
 * @brief The outcome of a single DNS health check (e.g. "Missing AAAA
 * record").
 * @details Every check in `checks/` (`MissingRecordChecks.h`,
 * `EmailSecurityChecks.h`, `TtlChecks.h`) returns one of these;
 * `CheckRoutes.h` collects them into the JSON array returned by
 * `POST /api/check`.
 */

namespace DnsCheckup {

/// @brief Severity of a single check's outcome.
enum class CheckStatus {
    Pass, ///< Best practice satisfied.
    Warn, ///< Not wrong, but worth the domain owner's attention.
    Fail, ///< Missing or misconfigured.
};

/**
 * @brief The result of one check rule run against one domain.
 */
struct CheckResult {
    std::string name;    ///< Short identifier, e.g. "missing_aaaa", "spf_present".
    CheckStatus status;  ///< Pass / warn / fail.
    std::string message; ///< Human-readable explanation for the frontend row.
};

} // namespace DnsCheckup
