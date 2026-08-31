/**
 * @file            QueryEngine.h
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
#include <DnsPro/DnsResolver.h> // DnsPro::Builder, DnsPro::Parser, DnsPro::Packet::Message

#include <CachePro/LRUCache.h> // CachePro::LRUCache

#include <VectorPro/Vector.h> // VectorPro::Vector

#include <chrono>      // std::chrono::steady_clock
#include <cstdint>     // std::uint16_t, std::uint32_t
#include <mutex>       // std::mutex
#include <optional>    // std::optional
#include <string>      // std::string
#include <string_view> // std::string_view
// clang-format on

/**
 * @brief Phase 1: the foundation every Phase 2 check calls into. Given a
 * domain name and a record type, builds a query packet (via
 * `DnsPro::Builder`), sends it over UDP to a resolver, receives the raw
 * response bytes, and parses them (via `DnsPro::Parser`) into a
 * `Message`.
 * @details `DnsPro` (`Builder`/`Parser`) never touches a socket -- it
 * only converts between `Message` structs and raw wire-format bytes
 * (confirmed in DnsResolver's `Parser.h` / `Builder.h`: both are
 * stateless, static-only classes operating on
 * `std::span<const std::byte>` / `VectorPro::Vector<std::byte>`).
 * `DnsPro::Resolver` is NOT used here -- it answers queries from a local
 * `ZoneStore` (authoritative-server-in-a-box), not queries against a
 * real remote nameserver, which is what this tool needs. So
 * `QueryEngine` owns the actual UDP socket lifecycle end to end: open,
 * send, receive-with-timeout, close.
 */

namespace DnsCheckup {

/// @brief Standard DNS record types this tool queries for (RFC 1035 /
/// 1183).
enum class RecordType : std::uint16_t {
    A = 1,
    NS = 2,
    TXT = 16,
    AAAA = 28,
    MX = 15,
};

/// @brief Outcome of `QueryEngine::query()`, covering both
/// transport-level and protocol-level failure so callers get one status
/// to switch on instead of juggling errno/socket errors and
/// `DnsPro::Status` separately.
enum class QueryStatus {
    Ok,          ///< Response received and parsed successfully.
    Timeout,     ///< No response within the configured timeout.
    SendFailed,  ///< The outbound UDP send() itself failed.
    ParseFailed, ///< Response bytes didn't parse (see `DnsPro::Status`
                 ///< detail captured in `QueryResult::parseStatus` for
                 ///< the specific cause).
    BuildFailed, ///< `DnsPro::Builder` couldn't serialize the outbound
                 ///< query (e.g. `Status::LABEL_TOO_LONG` on a malformed
                 ///< input domain).
};

/**
 * @brief Result of a single `query()` call.
 * @details On anything other than `QueryStatus::Ok`, `message` and
 * `rawResponse` are left default-constructed and must not be read --
 * same "don't touch on failure" contract `DnsPro::Parser::parse()` uses
 * for its own `out` param.
 */
struct QueryResult {
    QueryStatus status;
    DnsPro::Status parseStatus{DnsPro::Status::OK}; ///< Only meaningful when
                                                    ///< status == ParseFailed.
    DnsPro::Packet::Message message;

    /// @brief The exact bytes the resolver sent back, kept alongside
    /// `message` so a caller can decode a compressed domain name
    /// embedded in a record's rdata (MX exchange, NS nsdname, ...) via
    /// `DnsPro::Parser::parseName(rawResponse, someRecord.rdataOffset
    /// [+ any fixed-size fields before the name], out)`. A compression
    /// pointer only resolves against the *original* buffer it was
    /// parsed from (RFC 1035 S4.1.4) -- `message` alone, with rdata
    /// already copied out as opaque bytes, can't be decompressed on
    /// its own; this is what makes that possible without re-querying.
    VectorPro::Vector<std::byte> rawResponse;
};

/**
 * @brief Sends one DNS query over UDP and parses the response.
 * @details Not thread-safe to share a single instance's socket across
 * concurrent calls -- see the open question below on whether one
 * `QueryEngine` is constructed per request or reused across requests.
 */
class QueryEngine {
  public:
    /**
     * @brief Constructs a `QueryEngine` pointed at a specific resolver.
     * @param resolverHost IPv4/IPv6 address of the resolver to query,
     * e.g. "8.8.8.8" or "1.1.1.1" (recursive), or a domain's own
     * authoritative nameserver IP for authoritative-only lookups.
     * @param resolverPort UDP port, almost always 53.
     * @param timeoutMs How long to wait for a response before returning
     * `QueryStatus::Timeout`. No retry inside `QueryEngine` itself -- a
     * check that wants a retry-on-timeout policy does so at its own call
     * site, since not every check may want the same policy (e.g. TTL
     * checks may want to fail fast rather than retry).
     * @param cacheCapacity Max distinct (domain, RecordType) query
     * results kept cached at once (LRU-evicted beyond that). Set to `0`
     * to disable caching entirely -- every call does a real UDP round
     * trip.
     *
     * Resolver strategy decided for Phase 1: recursive (query a fixed
     * `resolverHost`/`resolverPort`, e.g. 8.8.8.8:53) rather than
     * resolving the domain's own authoritative NS first. Simpler --
     * one UDP round trip per check instead of two -- and matches what
     * most visitors' own DNS already sees. Trade-off: this can show
     * cached/propagating state rather than a domain owner's very latest
     * change. Authoritative-NS resolution is a documented follow-up;
     * swapping to it later only changes what `query()` does internally,
     * not this constructor's signature.
     *
     * Response caching: a successful result is cached for the *shortest*
     * TTL among its answer records (matching how a real resolver caches
     * a set -- never longer than the record that expires soonest), or a
     * fixed 60s for a successful-but-empty answer set (e.g. "no MX
     * record"), so a burst of repeat checks against the same domain
     * doesn't each cost a fresh UDP round trip. Only `QueryStatus::Ok`
     * results are cached -- a timeout or parse failure is never
     * remembered as if it were a real, stable answer.
     */
    QueryEngine(std::string resolverHost, std::uint16_t resolverPort, std::uint32_t timeoutMs,
                std::size_t cacheCapacity = 500);

    ~QueryEngine();

    QueryEngine(const QueryEngine&) = delete;
    QueryEngine& operator=(const QueryEngine&) = delete;

    /**
     * @brief Queries `domain` for records of `type`.
     * @param domain Domain name, e.g. "example.com" (no trailing dot
     * required -- normalized internally before building the `Question`).
     * @param type Record type to query for.
     * @return A `QueryResult` describing what happened; see
     * `QueryStatus`.
     */
    [[nodiscard]] QueryResult query(std::string_view domain, RecordType type);

  private:
    std::string resolverHost_;
    std::uint16_t resolverPort_;
    std::uint32_t timeoutMs_;
    int socketFd_ = -1; ///< POSIX socket fd; opened lazily on first `query()`.

    /// @brief One cached result plus when it stops being valid.
    struct CacheEntry {
        QueryResult result;
        std::chrono::steady_clock::time_point expiresAt;
    };

    /// @brief Keyed on "<domain>|<recordTypeValue>" -- see `cacheKey()`.
    /// Not thread-safe on its own (confirmed: `CachePro::LRUCache` has
    /// no internal locking), so every access goes through `cacheMutex_`.
    /// A `size_t` capacity of `0` isn't representable by `LRUCache`
    /// itself (its constructor requires >0), so caching-disabled
    /// (`cacheCapacity == 0`) is handled by skipping the cache entirely
    /// in `query()` rather than trying to construct a zero-size cache.
    std::optional<CachePro::LRUCache<std::string, CacheEntry>> cache_;
    std::mutex cacheMutex_;

    /// @brief Builds this query's cache key.
    [[nodiscard]] static std::string cacheKey(std::string_view domain, RecordType type);

    /// @brief Computes how long to cache a successful `result` for --
    /// the shortest TTL among its answers, or a fixed default if the
    /// answer set is empty. See the constructor's doc comment.
    [[nodiscard]] static std::chrono::steady_clock::duration
    cacheDurationFor(const QueryResult& result);

    /// @brief Converts a plain "www.example.com" string into a
    /// `DnsPro::Packet::Name`.
    [[nodiscard]] static DnsPro::Packet::Name toName(std::string_view domain);

    /// @brief Builds the outbound query `Message` (single question,
    /// RD=1).
    [[nodiscard]] static DnsPro::Packet::Message buildQuery(std::string_view domain,
                                                            RecordType type);
};

} // namespace DnsCheckup
