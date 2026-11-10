// Unit Domain Input Test Suite
// Verifies how a user-supplied domain is cleaned up, accepted and reduced to
// its apex before any DNS query is sent.
//
// Covers:
// - normalizeDomain(): trimming (also after a path/port is stripped), URL scheme/path/query/port
//   removal, lowercasing
// - isAcceptableDomain(): needs a dot, non-empty, at most 255 characters
// - apexOf(): reduces subdomains to the last two labels, leaves apexes alone

#include <support/framework.h>

#include "routes/CheckRoutes.h"

#include <string>

namespace {

using DnsCheckup::Routes::detail::apexOf;
using DnsCheckup::Routes::detail::isAcceptableDomain;
using DnsCheckup::Routes::detail::kMaxDomainLength;
using DnsCheckup::Routes::detail::normalizeDomain;

} // namespace

// Verifies whitespace and letter case are cleaned up.
static void normalize_trims_and_lowercases() {
    CHK(normalizeDomain("  Example.COM \n") == "example.com");
    CHK(normalizeDomain("WWW.Example.Com") == "www.example.com");
}

// Verifies a pasted URL is reduced to the bare host.
static void normalize_strips_scheme_path_query_port() {
    CHK(normalizeDomain("https://example.com/some/path") == "example.com");
    CHK(normalizeDomain("HTTP://Example.com") == "example.com");
    CHK(normalizeDomain("//example.com/x") == "example.com");
    CHK(normalizeDomain("example.com?utm=1") == "example.com");
    CHK(normalizeDomain("example.com#frag") == "example.com");
    CHK(normalizeDomain("example.com:8080/x") == "example.com");
}

// Verifies whitespace that sat before a path, query or port does not stay behind.
static void normalize_trims_whitespace_left_by_stripping() {
    CHK(normalizeDomain("example.com /path") == "example.com");
    CHK(normalizeDomain("example.com :80") == "example.com");
    CHK(normalizeDomain("https:// example.com") == "example.com");
    CHK(normalizeDomain("example.com \t?x=1") == "example.com");
}

// Verifies degenerate input normalizes to something the acceptance check refuses.
static void normalize_degenerate_input() {
    CHK(normalizeDomain("").empty());
    CHK(normalizeDomain("   ").empty());
    CHK(normalizeDomain("https://").empty());
    CHK(normalizeDomain("/only/a/path").empty());
}

// Verifies what counts as an acceptable domain.
static void acceptable_domain_rules() {
    CHK(isAcceptableDomain("example.com"));
    CHK(isAcceptableDomain("a.b"));
    CHK(!isAcceptableDomain(""));
    CHK(!isAcceptableDomain("localhost"));
}

// Verifies the length cap is inclusive: 255 passes, 256 does not.
static void acceptable_domain_length_cap() {
    const std::string atLimit = std::string(kMaxDomainLength - 4, 'a') + ".com";
    CHK(atLimit.size() == kMaxDomainLength);
    CHK(isAcceptableDomain(atLimit));
    CHK(!isAcceptableDomain(atLimit + "x"));
}

// Verifies subdomains reduce to their apex and apexes stay as they are.
static void apex_reduces_subdomains() {
    CHK(apexOf("www.example.com") == "example.com");
    CHK(apexOf("a.b.c.example.com") == "example.com");
    CHK(apexOf("example.com") == "example.com");
    CHK(apexOf("localhost") == "localhost");
    CHK(apexOf("").empty());
}

static void run_tests() {
    RUN(normalize_trims_and_lowercases);
    RUN(normalize_strips_scheme_path_query_port);
    RUN(normalize_trims_whitespace_left_by_stripping);
    RUN(normalize_degenerate_input);
    RUN(acceptable_domain_rules);
    RUN(acceptable_domain_length_cap);
    RUN(apex_reduces_subdomains);
}

REGISTER_TEST_SUITE();
