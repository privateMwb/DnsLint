// Unit Checks Test Suite
// Verifies every check (missing records, email security, TTL) end to end
// against the in-process mock DNS server (see support/mockDns.h): a real UDP
// round trip, a real parsed answer, and the status the check reports.
//
// Covers:
// - AAAA / MX / NS: found (with the decoded value in the message), missing, timeout
// - SPF / DMARC / DKIM: found (with the record text), missing, and a warning (not a failure)
//   when the resolver can't be reached
// - TTL: low, in range, high, the exact boundaries, the lowest of several, no record

#include <support/framework.h>
#include <support/mockDns.h>

#include "checks/EmailSecurityChecks.h"
#include "checks/MissingRecordChecks.h"
#include "checks/TtlChecks.h"

#include <cstdint>
#include <string>

namespace {

using DnsCheckup::CheckStatus;
using DnsCheckup::QueryEngine;
namespace Checks = DnsCheckup::Checks;

constexpr std::uint16_t kA = 1;
constexpr std::uint16_t kNs = 2;
constexpr std::uint16_t kMx = 15;
constexpr std::uint16_t kTxt = 16;
constexpr std::uint16_t kAaaa = 28;

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

} // namespace

// ---- Missing records ----

// Verifies a present AAAA record passes and shows the decoded address.
static void aaaa_present() {
    mockdns::Server server;
    server.answer("example.com", kAaaa, {{kAaaa, 300, mockdns::ipv6Rdata()}});
    QueryEngine engine("127.0.0.1", server.port(), 1000);

    const auto result = Checks::checkMissingAaaa(engine, "example.com");
    CHK(result.name == "missing_aaaa");
    CHK(result.status == CheckStatus::Pass);
    CHK(contains(result.message, "2001:db8:0:0:0:0:0:1"));
}

// Verifies no AAAA record fails.
static void aaaa_missing() {
    mockdns::Server server;
    QueryEngine engine("127.0.0.1", server.port(), 1000);

    CHK(Checks::checkMissingAaaa(engine, "example.com").status == CheckStatus::Fail);
}

// Verifies a present MX record passes and shows the mail server name.
static void mx_present() {
    mockdns::Server server;
    server.answer("example.com", kMx, {{kMx, 300, mockdns::mxRdata(10, "mail.example.com")}});
    QueryEngine engine("127.0.0.1", server.port(), 1000);

    const auto result = Checks::checkMissingMx(engine, "example.com");
    CHK(result.status == CheckStatus::Pass);
    CHK(contains(result.message, "mail.example.com"));
}

// Verifies no MX record fails.
static void mx_missing() {
    mockdns::Server server;
    QueryEngine engine("127.0.0.1", server.port(), 1000);

    CHK(Checks::checkMissingMx(engine, "example.com").status == CheckStatus::Fail);
}

// Verifies present NS records pass and list every nameserver.
static void ns_present() {
    mockdns::Server server;
    server.answer("example.com", kNs,
                  {{kNs, 300, mockdns::encodeName("ns1.example.com")},
                   {kNs, 300, mockdns::encodeName("ns2.example.com")}});
    QueryEngine engine("127.0.0.1", server.port(), 1000);

    const auto result = Checks::checkMissingNs(engine, "example.com");
    CHK(result.status == CheckStatus::Pass);
    CHK(contains(result.message, "ns1.example.com"));
    CHK(contains(result.message, "ns2.example.com"));
}

// Verifies a name that uses a compression pointer back to the question still decodes.
static void ns_with_compression_pointer() {
    mockdns::Server server;
    server.answer("example.com", kNs, {{kNs, 300, {0xC0, 0x0C}}}); // points at the question name
    QueryEngine engine("127.0.0.1", server.port(), 1000);

    const auto result = Checks::checkMissingNs(engine, "example.com");
    CHK(result.status == CheckStatus::Pass);
    CHK(contains(result.message, "example.com"));
}

// Verifies a resolver that never answers is a warning, not a failure.
static void missing_checks_warn_on_timeout() {
    mockdns::Server server;
    server.silence(true);
    QueryEngine engine("127.0.0.1", server.port(), 120, 0);

    CHK(Checks::checkMissingAaaa(engine, "example.com").status == CheckStatus::Warn);
    CHK(Checks::checkMissingMx(engine, "example.com").status == CheckStatus::Warn);
    CHK(Checks::checkMissingNs(engine, "example.com").status == CheckStatus::Warn);
}

// ---- Email security ----

// Verifies SPF passes when a v=spf1 TXT record exists, and shows it.
static void spf_present() {
    mockdns::Server server;
    server.answer("example.com", kTxt,
                  {{kTxt, 300, mockdns::txtRdata("google-site-verification=abc")},
                   {kTxt, 300, mockdns::txtRdata("v=spf1 include:_spf.example.com ~all")}});
    QueryEngine engine("127.0.0.1", server.port(), 1000);

    const auto result = Checks::checkSpf(engine, "example.com");
    CHK(result.name == "spf_present");
    CHK(result.status == CheckStatus::Pass);
    CHK(result.message == "v=spf1 include:_spf.example.com ~all");
}

// Verifies SPF fails when no TXT record starts with v=spf1.
static void spf_missing() {
    mockdns::Server server;
    server.answer("example.com", kTxt, {{kTxt, 300, mockdns::txtRdata("hello")}});
    QueryEngine engine("127.0.0.1", server.port(), 1000);

    CHK(Checks::checkSpf(engine, "example.com").status == CheckStatus::Fail);
}

// Verifies DMARC is read from _dmarc.<domain>.
static void dmarc_present_and_missing() {
    mockdns::Server server;
    server.answer("_dmarc.example.com", kTxt,
                  {{kTxt, 300, mockdns::txtRdata("v=DMARC1; p=reject")}});
    QueryEngine engine("127.0.0.1", server.port(), 1000);

    const auto found = Checks::checkDmarc(engine, "example.com");
    CHK(found.status == CheckStatus::Pass);
    CHK(found.message == "v=DMARC1; p=reject");

    CHK(Checks::checkDmarc(engine, "other.example.org").status == CheckStatus::Fail);
}

// Verifies an unreachable resolver is a warning for SPF and DMARC too, not "no record found".
static void email_checks_warn_on_timeout() {
    mockdns::Server server;
    server.silence(true);
    QueryEngine engine("127.0.0.1", server.port(), 120, 0);

    CHK(Checks::checkSpf(engine, "example.com").status == CheckStatus::Warn);
    CHK(Checks::checkDmarc(engine, "example.com").status == CheckStatus::Warn);
    CHK(Checks::checkDkim(engine, "example.com").status == CheckStatus::Warn);
}

// Verifies DKIM is found under a common selector, and a miss is only a warning.
static void dkim_found_and_undetected() {
    mockdns::Server server;
    server.answer("google._domainkey.example.com", kTxt,
                  {{kTxt, 300, mockdns::txtRdata("v=DKIM1; k=rsa; p=AAAA")}});
    QueryEngine engine("127.0.0.1", server.port(), 1000);

    const auto found = Checks::checkDkim(engine, "example.com");
    CHK(found.status == CheckStatus::Pass);
    CHK(contains(found.message, "google"));

    // A domain with no record under any common selector can't be told apart from
    // one using a selector this tool doesn't know, so it is a warning, never a failure.
    CHK(Checks::checkDkim(engine, "nodkim.example.org").status == CheckStatus::Warn);
}

// ---- TTL ----

namespace {

CheckStatus ttlStatusFor(std::uint32_t ttl) {
    mockdns::Server server;
    server.answer("example.com", kA, {{kA, ttl, mockdns::ipv4Rdata(1, 2, 3, 4)}});
    QueryEngine engine("127.0.0.1", server.port(), 1000);
    return Checks::checkTtlA(engine, "example.com").status;
}

} // namespace

// Verifies a TTL under 5 minutes warns and one at or above passes.
static void ttl_low_boundary() {
    CHK(ttlStatusFor(60) == CheckStatus::Warn);
    CHK(ttlStatusFor(299) == CheckStatus::Warn);
    CHK(ttlStatusFor(300) == CheckStatus::Pass);
}

// Verifies a normal TTL passes.
static void ttl_in_range() {
    CHK(ttlStatusFor(3600) == CheckStatus::Pass);
}

// Verifies a TTL over 48 hours warns and exactly 48 hours passes.
static void ttl_high_boundary() {
    CHK(ttlStatusFor(172800) == CheckStatus::Pass);
    CHK(ttlStatusFor(172801) == CheckStatus::Warn);
}

// Verifies the lowest TTL in a set is the one judged.
static void ttl_uses_lowest_of_several() {
    mockdns::Server server;
    server.answer("example.com", kA,
                  {{kA, 3600, mockdns::ipv4Rdata(1, 1, 1, 1)},
                   {kA, 30, mockdns::ipv4Rdata(2, 2, 2, 2)}});
    QueryEngine engine("127.0.0.1", server.port(), 1000);

    const auto result = Checks::checkTtlA(engine, "example.com");
    CHK(result.status == CheckStatus::Warn);
    CHK(contains(result.message, "30s"));
}

// Verifies no record to judge is a warning, and the MX variant uses MX records.
static void ttl_no_record_and_mx() {
    mockdns::Server server;
    server.answer("example.com", kMx, {{kMx, 3600, mockdns::mxRdata(10, "mail.example.com")}});
    QueryEngine engine("127.0.0.1", server.port(), 1000);

    CHK(Checks::checkTtlA(engine, "example.com").status == CheckStatus::Warn);
    const auto mx = Checks::checkTtlMx(engine, "example.com");
    CHK(mx.name == "ttl_mx");
    CHK(mx.status == CheckStatus::Pass);
}

static void run_tests() {
    RUN(aaaa_present);
    RUN(aaaa_missing);
    RUN(mx_present);
    RUN(mx_missing);
    RUN(ns_present);
    RUN(ns_with_compression_pointer);
    RUN(missing_checks_warn_on_timeout);
    RUN(spf_present);
    RUN(spf_missing);
    RUN(dmarc_present_and_missing);
    RUN(email_checks_warn_on_timeout);
    RUN(dkim_found_and_undetected);
    RUN(ttl_low_boundary);
    RUN(ttl_in_range);
    RUN(ttl_high_boundary);
    RUN(ttl_uses_lowest_of_several);
    RUN(ttl_no_record_and_mx);
}

REGISTER_TEST_SUITE();
