// Unit QueryEngine Test Suite
// Verifies QueryEngine over real UDP round trips against an in-process mock
// DNS server on 127.0.0.1 (see support/mockDns.h): answers, response caching,
// timeouts, and unparseable replies. Nothing here touches the network.
//
// Covers:
// - A successful answer is returned and parsed
// - A repeat query is served from the cache (the server sees one request)
// - cacheCapacity 0 disables caching (the server sees two)
// - The record type is part of the cache key
// - An empty (but successful) answer set is cached too
// - A timeout is reported as Timeout and is never cached
// - Bytes that are not a DNS message are reported as ParseFailed
// - A domain that cannot be encoded is BuildFailed and never sent
// - Concurrent queries on one engine each get their own answer (no cross-talk)

#include <support/framework.h>
#include <support/mockDns.h>

#include "QueryEngine.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace {

using DnsCheckup::QueryEngine;
using DnsCheckup::QueryStatus;
using DnsCheckup::RecordType;

constexpr std::uint16_t kA = 1;
constexpr std::uint16_t kTxt = 16;

} // namespace

// Verifies a canned A record comes back parsed.
static void returns_parsed_answer() {
    mockdns::Server server;
    server.answer("example.com", kA, {{kA, 300, mockdns::ipv4Rdata(93, 184, 216, 34)}});

    QueryEngine engine("127.0.0.1", server.port(), 1000);
    const auto result = engine.query("example.com", RecordType::A);

    CHK(result.status == QueryStatus::Ok);
    CHK(result.message.answers.size() == 1);
    CHK(result.message.answers[0].type == kA);
    CHK(result.message.answers[0].ttl == 300);
    CHK(result.message.answers[0].rdata.size() == 4);
}

// Verifies the second identical query never reaches the server.
static void second_query_is_cached() {
    mockdns::Server server;
    server.answer("example.com", kA, {{kA, 300, mockdns::ipv4Rdata(1, 2, 3, 4)}});

    QueryEngine engine("127.0.0.1", server.port(), 1000);
    CHK(engine.query("example.com", RecordType::A).status == QueryStatus::Ok);
    CHK(engine.query("example.com", RecordType::A).status == QueryStatus::Ok);

    CHK(server.requests() == 1);
}

// Verifies capacity 0 turns caching off.
static void capacity_zero_disables_cache() {
    mockdns::Server server;
    server.answer("example.com", kA, {{kA, 300, mockdns::ipv4Rdata(1, 2, 3, 4)}});

    QueryEngine engine("127.0.0.1", server.port(), 1000, 0);
    CHK(engine.query("example.com", RecordType::A).status == QueryStatus::Ok);
    CHK(engine.query("example.com", RecordType::A).status == QueryStatus::Ok);

    CHK(server.requests() == 2);
}

// Verifies the same name with a different record type is a separate cache entry.
static void record_type_is_part_of_the_key() {
    mockdns::Server server;
    server.answer("example.com", kA, {{kA, 300, mockdns::ipv4Rdata(1, 2, 3, 4)}});
    server.answer("example.com", kTxt, {{kTxt, 300, mockdns::txtRdata("hello")}});

    QueryEngine engine("127.0.0.1", server.port(), 1000);
    CHK(engine.query("example.com", RecordType::A).status == QueryStatus::Ok);
    CHK(engine.query("example.com", RecordType::TXT).status == QueryStatus::Ok);

    CHK(server.requests() == 2);
}

// Verifies a successful empty answer is a valid result and is cached.
static void empty_answer_is_ok_and_cached() {
    mockdns::Server server; // nothing configured: NOERROR with no answers

    QueryEngine engine("127.0.0.1", server.port(), 1000);
    const auto first = engine.query("nothing.example.com", RecordType::A);
    CHK(first.status == QueryStatus::Ok);
    CHK(first.message.answers.size() == 0);

    CHK(engine.query("nothing.example.com", RecordType::A).status == QueryStatus::Ok);
    CHK(server.requests() == 1);
}

// Verifies silence becomes a Timeout, and that a timeout is not remembered.
static void timeout_is_reported_and_not_cached() {
    mockdns::Server server;
    server.answer("example.com", kA, {{kA, 300, mockdns::ipv4Rdata(1, 2, 3, 4)}});
    server.silence(true);

    QueryEngine engine("127.0.0.1", server.port(), 150);
    CHK(engine.query("example.com", RecordType::A).status == QueryStatus::Timeout);

    server.silence(false);
    CHK(engine.query("example.com", RecordType::A).status == QueryStatus::Ok);
}

// Verifies a reply that is not a DNS message is a ParseFailed, not a crash.
static void garbage_reply_is_parse_failed() {
    mockdns::Server server;
    server.rawReply("example.com", kA, {0x01, 0x02, 0x03});

    QueryEngine engine("127.0.0.1", server.port(), 1000);
    CHK(engine.query("example.com", RecordType::A).status == QueryStatus::ParseFailed);
}

// Verifies a domain that cannot be encoded fails to build instead of being sent.
static void oversized_label_is_build_failed() {
    mockdns::Server server;

    QueryEngine engine("127.0.0.1", server.port(), 500);
    const std::string label(64, 'a'); // a label is at most 63 bytes
    CHK(engine.query(label + ".com", RecordType::A).status == QueryStatus::BuildFailed);
    CHK(server.requests() == 0);
}

// Verifies concurrent queries on one engine each get the answer to their own question. The
// server shares a single QueryEngine across its worker threads, so two checks running at once
// must never receive each other's DNS responses.
static void concurrent_queries_get_their_own_answers() {
    constexpr int kThreads = 8;
    constexpr int kQueriesPerThread = 25;

    mockdns::Server server;
    for (int t = 0; t < kThreads; ++t)
        server.answer("host" + std::to_string(t) + ".example.com", kA,
                      {{kA, 300,
                        mockdns::ipv4Rdata(static_cast<std::uint8_t>(t + 1),
                                           static_cast<std::uint8_t>(t + 1),
                                           static_cast<std::uint8_t>(t + 1),
                                           static_cast<std::uint8_t>(t + 1))}});

    // Capacity 0: every call is a real round trip, so nothing is answered from the cache.
    QueryEngine engine("127.0.0.1", server.port(), 2000, 0);

    std::atomic<int> wrong{0};
    std::atomic<int> failed{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            const std::string name = "host" + std::to_string(t) + ".example.com";
            for (int i = 0; i < kQueriesPerThread; ++i) {
                const auto result = engine.query(name, RecordType::A);
                if (result.status != QueryStatus::Ok) {
                    ++failed;
                    continue;
                }
                if (result.message.answers.size() != 1 ||
                    std::to_integer<int>(result.message.answers[0].rdata[0]) != t + 1)
                    ++wrong;
            }
        });
    }
    for (auto& th : threads)
        th.join();

    CHK(failed == 0);
    CHK(wrong == 0);
}

static void run_tests() {
    RUN(returns_parsed_answer);
    RUN(second_query_is_cached);
    RUN(capacity_zero_disables_cache);
    RUN(record_type_is_part_of_the_key);
    RUN(empty_answer_is_ok_and_cached);
    RUN(timeout_is_reported_and_not_cached);
    RUN(garbage_reply_is_parse_failed);
    RUN(oversized_label_is_build_failed);
    RUN(concurrent_queries_get_their_own_answers);
}

REGISTER_TEST_SUITE();
