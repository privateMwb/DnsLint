/**
 * @file            CheckRoutes.h
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
#include <FalconHTTP/HTTP/HttpRequest.h>  // HttpRequest
#include <FalconHTTP/HTTP/HttpResponse.h> // HttpResponse
#include <FalconHTTP/HTTP/HttpStatus.h>   // HttpStatus

#include <CheckResult.h>                 // DnsCheckup::CheckResult, DnsCheckup::CheckStatus
#include <QueryEngine.h>                 // DnsCheckup::QueryEngine
#include <checks/EmailSecurityChecks.h>  // DnsCheckup::Checks::checkSpf, checkDmarc, checkDkim
#include <checks/MissingRecordChecks.h>  // DnsCheckup::Checks::checkMissingAaaa, checkMissingMx, checkMissingNs
#include <checks/TtlChecks.h>            // DnsCheckup::Checks::checkTtlA, checkTtlMx
#include <database/HistoryStore.h>       // DnsCheckup::HistoryStore, DnsCheckup::CheckRun

#include <VectorPro/Vector.h> // VectorPro::Vector

#include <JsonPro/Json.h> // JsonPro::Json

#include <algorithm> // std::find_if
#include <cctype>    // std::isspace, std::tolower
#include <chrono>    // std::chrono::system_clock
#include <cstdio>    // std::fprintf, stderr
#include <string>    // std::string
// clang-format on

// Phase 3: POST /api/check, plus GET /api/history. Runs every Phase 2
// check module against the submitted domain and returns their combined
// CheckResult list as JSON; every run is also persisted via
// HistoryStore, and GET /api/history?domain=... reads it back.
// QueryEngine is shared across all requests -- see this file's own
// note on why that's safe despite QueryEngine::query() not being
// thread-safe against a single instance. HistoryStore is safe to share
// across concurrent requests unconditionally -- see its own doc
// comment on its internal locking.
//
// NOTE: results are collected into VectorPro::Vector<CheckResult>, matching
// the rest of the app's dependency on VectorPro -- but checksJson stays a
// plain std::vector<Json>, since that's JsonPro::Json::ArrayType's fixed
// underlying type (JsonPro's own choice, not this project's).

namespace DnsCheckup::Routes {

using namespace VectorPro;

namespace detail {

/// @brief Maximum accepted length for a submitted domain, in characters.
/// @details Not a DNS-protocol limit (a full domain name is capped at 255
/// octets by RFC 1035 S3.1 anyway) -- this is a cheap first filter so an
/// absurdly long string doesn't get as far as QueryEngine::toName()'s
/// label-splitting loop.
inline constexpr std::size_t kMaxDomainLength = 255;

/// @brief Normalizes raw user input into a bare domain before it's
/// queried.
/// @details A pasted URL (`"https://m.youtube.com/"`) is common,
/// reasonable input for a domain-checkup tool, but QueryEngine::toName()
/// splits on every '.' with no scheme/path awareness -- fed a whole URL
/// unmodified, it produces garbage labels ("https://m", "youtube",
/// "com/") and every check fails, even though the domain itself is
/// perfectly fine. This strips a leading scheme, anything after the
/// host (path/query/fragment), and a trailing port, then lowercases
/// the result (DNS names are case-insensitive, and lowercasing keeps
/// the "_dmarc."/"._domainkey." prefixes built in
/// EmailSecurityChecks.h from ever comparing case-mismatched).
[[nodiscard]] inline std::string normalizeDomain(std::string input) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    input.erase(input.begin(), std::find_if(input.begin(), input.end(), notSpace));
    input.erase(std::find_if(input.rbegin(), input.rend(), notSpace).base(), input.end());

    auto stripPrefix = [&input](std::string_view prefix) {
        if (input.size() < prefix.size())
            return;
        for (std::size_t i = 0; i < prefix.size(); ++i)
            if (std::tolower(static_cast<unsigned char>(input[i])) !=
                std::tolower(static_cast<unsigned char>(prefix[i])))
                return;
        input.erase(0, prefix.size());
    };
    stripPrefix("https://");
    stripPrefix("http://");
    stripPrefix("//");

    if (auto pos = input.find_first_of("/?#"); pos != std::string::npos)
        input.erase(pos);

    if (auto pos = input.find(':'); pos != std::string::npos)
        input.erase(pos);

    for (char& c : input)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    return input;
}

/// @brief Rejects obviously malformed domain input.
/// @details Deliberately simple, matching this project's "no full RFC
/// parse" precedent (see Shrtn's UrlRoutes.h isAcceptableUrl()): non-
/// empty, under kMaxDomainLength, and containing at least one '.' --
/// enough to catch blank submissions and copy-paste mistakes (a bare
/// word, a URL with a scheme) without trying to fully validate label
/// syntax, which DnsPro::Builder itself would reject via
/// Status::LABEL_TOO_LONG on an actually malformed label. Run this
/// AFTER normalizeDomain(), not on raw input -- it's a shape check on
/// the cleaned-up domain, not a scheme/path detector.
[[nodiscard]] inline bool isAcceptableDomain(const std::string& domain) {
    if (domain.empty() || domain.size() > kMaxDomainLength)
        return false;
    return domain.find('.') != std::string::npos;
}

/// @brief Derives the apex ("organizational") domain from a normalized
/// domain -- "www.facebook.com" -> "facebook.com".
/// @details MX, NS, and DMARC are apex-domain concepts by nature: a
/// "www" subdomain never has its own MX/NS/DMARC regardless of how well
/// configured the site actually is (mail goes to the apex; a subdomain
/// isn't its own delegated zone; DMARC policies are published once per
/// organizational domain). Checking those three against whatever exact
/// host the caller typed would misreport a perfectly fine setup as
/// broken -- see postCheck() for where this is applied.
///
/// This is a naive last-two-labels heuristic ("a.b.com" -> "b.com"),
/// not a full public-suffix-list lookup. It's correct for ordinary
/// TLDs but wrong for compound ones -- "www.example.co.uk" reduces to
/// "co.uk", not "example.co.uk". A fully correct answer needs the
/// Public Suffix List (a large, regularly-updated external data set);
/// pulling that in is a deliberate scope decision to revisit later, not
/// an oversight. Domains already at or below two labels pass through
/// unchanged.
[[nodiscard]] inline std::string apexOf(const std::string& domain) {
    std::size_t lastDot = domain.rfind('.');
    if (lastDot == std::string::npos || lastDot == 0)
        return domain;

    std::size_t secondLastDot = domain.rfind('.', lastDot - 1);
    if (secondLastDot == std::string::npos)
        return domain;

    return domain.substr(secondLastDot + 1);
}

/// @brief Converts one CheckResult's CheckStatus into its JSON string form.
[[nodiscard]] inline std::string statusToString(CheckStatus status) {
    switch (status) {
    case CheckStatus::Pass:
        return "pass";
    case CheckStatus::Warn:
        return "warn";
    case CheckStatus::Fail:
        return "fail";
    }
    return "unknown"; // Unreachable for a valid CheckStatus; satisfies -Wreturn-type.
}

/// @brief Serializes one CheckResult into a JSON object.
[[nodiscard]] inline JsonPro::Json toJson(const CheckResult& result) {
    JsonPro::Json row = JsonPro::Json::ObjectType{};
    row["name"] = result.name;
    row["status"] = statusToString(result.status);
    row["message"] = result.message;
    return row;
}

/// @brief Serializes one CheckRun into a JSON object.
[[nodiscard]] inline JsonPro::Json toJson(const CheckRun& run) {
    JsonPro::Json row = JsonPro::Json::ObjectType{};
    row["domain"] = run.domain;
    row["checkedAt"] = static_cast<double>(run.checkedAtEpochSeconds);
    row["checks"] = run.results;
    return row;
}

/// @brief Current Unix time, in whole seconds.
[[nodiscard]] inline std::uint64_t nowEpochSeconds() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count());
}

} // namespace detail

/**
 * @brief Handles `POST /api/check`.
 * @param engine Shared QueryEngine used to run every check. Not
 * thread-safe against concurrent calls on the same instance (see
 * QueryEngine.h) -- safe here only because Server is thread-per-
 * connection and this handler is registered as the sole route touching
 * `engine`; if concurrent request volume ever makes this a bottleneck,
 * the fix is a QueryEngine per request (cheap: it owns one lazily-opened
 * socket) rather than adding locking around a shared one.
 * @param history Shared HistoryStore every completed run is persisted
 * to. Safe to share across concurrent requests unconditionally -- see
 * its own class doc comment on internal locking.
 * @details Expects a JSON body `{"domain": "..."}`. On success, responds
 * `200 OK` with `{"domain": "...", "checks": [...]}`, where each entry is
 * `{"name", "status", "message"}` from `MissingRecordChecks.h`,
 * `EmailSecurityChecks.h`, and `TtlChecks.h` combined. `400 Bad Request`
 * for a missing, non-string, or unacceptable domain.
 */
inline void postCheck(QueryEngine& engine, HistoryStore& history,
                      const FalconHTTP::HTTP::HttpRequest& request,
                      FalconHTTP::HTTP::HttpResponse& response) {
    JsonPro::Json body;
    try {
        body = request.json();
    } catch (...) {
        response.setStatus(FalconHTTP::HTTP::HttpStatus::BadRequest);
        response.setBody("Malformed JSON body.");
        return;
    }

    if (!body.contains("domain") || !body["domain"].isString()) {
        response.setStatus(FalconHTTP::HTTP::HttpStatus::BadRequest);
        response.setBody("Missing or non-string \"domain\" field.");
        return;
    }

    const std::string domain = detail::normalizeDomain(body["domain"].asString());
    if (!detail::isAcceptableDomain(domain)) {
        response.setStatus(FalconHTTP::HTTP::HttpStatus::BadRequest);
        response.setBody("\"domain\" must be a non-empty domain name, e.g. \"example.com\".");
        return;
    }

    const std::string apexDomain = detail::apexOf(domain);

    // AAAA, SPF, DKIM, and A-record TTL can legitimately differ per
    // subdomain -- checked against exactly what the caller submitted.
    // MX, NS, DMARC, and MX-record TTL are apex-domain concepts by
    // nature -- see detail::apexOf()'s docs for why.
    Vector<CheckResult> results{
        Checks::checkMissingAaaa(engine, domain),   Checks::checkMissingMx(engine, apexDomain),
        Checks::checkMissingNs(engine, apexDomain), Checks::checkSpf(engine, domain),
        Checks::checkDmarc(engine, apexDomain),     Checks::checkDkim(engine, domain),
        Checks::checkTtlA(engine, domain),          Checks::checkTtlMx(engine, apexDomain),
    };

    JsonPro::Json::ArrayType checksJson;
    checksJson.reserve(results.size());
    for (const CheckResult& result : results)
        checksJson.push_back(detail::toJson(result));

    // A history-write failure degrades this endpoint (no record of the
    // run saved), not fails it -- the check itself already succeeded
    // and the caller is owed that result regardless of whether
    // persisting a copy of it also worked.
    JsonPro::Json checksArray = checksJson;
    MiniDB::Common::Status historyStatus =
        history.recordCheck(domain, detail::nowEpochSeconds(), checksArray);
    if (historyStatus != MiniDB::Common::Status::OK)
        std::fprintf(stderr, "HistoryStore::recordCheck failed for \"%s\": %s\n", domain.c_str(),
                     MiniDB::Common::statusToString(historyStatus));

    JsonPro::Json responseBody = JsonPro::Json::ObjectType{};
    responseBody["domain"] = domain;
    responseBody["checks"] = checksJson;

    response.setStatus(FalconHTTP::HTTP::HttpStatus::Ok);
    response.setJson(responseBody);
}

/**
 * @brief Handles `GET /api/history?domain=...`.
 * @param history Shared HistoryStore to read past runs from.
 * @details Responds `200 OK` with `{"domain": "...", "runs": [...]}`,
 * newest first, each run shaped `{"domain", "checkedAt", "checks"}` --
 * the same "checks" array shape `POST /api/check` returns. `400 Bad
 * Request` for a missing or unacceptable `domain` query parameter.
 * Domain is normalized the same way `postCheck()` normalizes it before
 * recording, so a history lookup for a pasted URL still matches what
 * was actually stored.
 */
inline void getHistory(HistoryStore& history, const FalconHTTP::HTTP::HttpRequest& request,
                       FalconHTTP::HTTP::HttpResponse& response) {
    const std::string domain = detail::normalizeDomain(request.queryParam("domain"));
    if (!detail::isAcceptableDomain(domain)) {
        response.setStatus(FalconHTTP::HTTP::HttpStatus::BadRequest);
        response.setBody(
            "\"domain\" query parameter must be a non-empty domain name, e.g. \"example.com\".");
        return;
    }

    constexpr std::size_t kHistoryLimit = 20;
    Vector<CheckRun> runs = history.getHistory(domain, kHistoryLimit);

    JsonPro::Json::ArrayType runsJson;
    runsJson.reserve(runs.size());
    for (const CheckRun& run : runs)
        runsJson.push_back(detail::toJson(run));

    JsonPro::Json responseBody = JsonPro::Json::ObjectType{};
    responseBody["domain"] = domain;
    responseBody["runs"] = runsJson;

    response.setStatus(FalconHTTP::HTTP::HttpStatus::Ok);
    response.setJson(responseBody);
}

} // namespace DnsCheckup::Routes
