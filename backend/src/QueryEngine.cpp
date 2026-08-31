/**
 * @file QueryEngine.cpp
 * @brief QueryEngine implementation.
 *
 * Contains the implementation of QueryEngine member functions and
 * internal implementation details.
 */

// ============================================================
// Implementation for DnsCheckup::QueryEngine.
// ============================================================
//
//  Sections:
//   1. Constructor & Destructor
//   2. Cache Helpers
//   3. Message Building
//   4. Query Execution
//
// ============================================================

// clang-format off
#include <QueryEngine.h> // QueryEngine (own header)

#include <VectorPro/Vector.h> // VectorPro::Vector

#include <algorithm> // std::min
#include <array>     // std::array
#include <cerrno>    // errno, EAGAIN, EWOULDBLOCK
#include <chrono>    // std::chrono::steady_clock, std::chrono::seconds
#include <cstring>   // std::memset
#include <random>    // std::mt19937, std::uniform_int_distribution, std::random_device

#include <arpa/inet.h> // inet_pton, htons
#include <netdb.h> // sockaddr_in
#include <sys/socket.h> // socket, sendto, recvfrom, setsockopt
#include <sys/types.h> // ssize_t
#include <unistd.h> // close
// clang-format on

namespace DnsCheckup {

using DnsPro::Builder;
using DnsPro::Parser;
using DnsPro::Status;
using DnsPro::Packet::Header;
using DnsPro::Packet::Message;
using DnsPro::Packet::Name;
using DnsPro::Packet::Question;
using VectorPro::Vector;

namespace {

/// @brief Max UDP DNS response size accepted. 512 is the classic no-EDNS
/// limit, but real-world resolvers routinely answer larger over UDP
/// (EDNS0) before a client would even see TC=1 and retry over TCP. 4096
/// covers that without pulling in a TCP fallback path for Phase 1.
constexpr std::size_t kMaxResponseBytes = 4096;

/// @brief Generates a random 16-bit query ID, echoed back by the
/// resolver. Not cryptographically hardened against off-path spoofing --
/// this tool only ever reads public DNS data about domains it doesn't
/// control, so the query ID's job here is collision-avoidance across
/// concurrent requests, not the security hardening a resolver's own
/// outbound queries would need.
std::uint16_t randomQueryId() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    static thread_local std::uniform_int_distribution<std::uint16_t> dist(0, 0xFFFF);
    return dist(rng);
}

} // namespace

// ============================================================
//  Section 1 — Constructor & Destructor
// ============================================================

QueryEngine::QueryEngine(std::string resolverHost, std::uint16_t resolverPort,
                         std::uint32_t timeoutMs, std::size_t cacheCapacity)
    : resolverHost_(std::move(resolverHost)), resolverPort_(resolverPort), timeoutMs_(timeoutMs) {
    if (cacheCapacity > 0)
        cache_.emplace(
            cacheCapacity); // LRUCache's constructor requires >0 -- see cache_'s doc comment.
}

QueryEngine::~QueryEngine() {
    if (socketFd_ >= 0)
        ::close(socketFd_);
}

// ============================================================
//  Section 2 — Cache Helpers
// ============================================================

std::string QueryEngine::cacheKey(std::string_view domain, RecordType type) {
    std::string key(domain);
    key += '|';
    key += std::to_string(static_cast<std::uint16_t>(type));
    return key;
}

std::chrono::steady_clock::duration QueryEngine::cacheDurationFor(const QueryResult& result) {
    // 60s: short enough that a "no record found" result doesn't stay
    // stale for long if the domain owner fixes it, but still long
    // enough to absorb a burst of repeat checks against the same
    // domain within a short window.
    constexpr std::chrono::seconds kDefaultDuration{60};

    if (result.message.answers.empty())
        return kDefaultDuration;

    std::uint32_t minTtl = result.message.answers[0].ttl;
    for (const auto& record : result.message.answers)
        minTtl = std::min(minTtl, record.ttl);

    return std::chrono::seconds(minTtl);
}

// ============================================================
//  Section 3 — Message Building
// ============================================================

Name QueryEngine::toName(std::string_view domain) {
    Name name;

    // Strip a single trailing dot ("example.com." -> "example.com") so
    // both forms produce the same label set.
    if (!domain.empty() && domain.back() == '.')
        domain.remove_suffix(1);

    std::size_t start = 0;
    while (start <= domain.size()) {
        std::size_t dot = domain.find('.', start);
        std::size_t end = (dot == std::string_view::npos) ? domain.size() : dot;
        name.labels.push_back(std::string(domain.substr(start, end - start)));
        if (dot == std::string_view::npos)
            break;
        start = dot + 1;
    }

    return name;
}

Message QueryEngine::buildQuery(std::string_view domain, RecordType type) {
    Message message;

    message.header.id = randomQueryId();
    message.header.qr = 0;     // Query, not response.
    message.header.opcode = 0; // Standard query.
    message.header.aa = 0;
    message.header.tc = 0;
    message.header.rd = 1; // Recursion desired -- we're querying a recursive resolver.
    message.header.ra = 0;
    message.header.z = 0;
    message.header.rcode = 0;
    // qdcount/ancount/nscount/arcount are ignored by Builder::build() --
    // it derives counts from the section vectors below (see Builder.h).
    // Left zero-initialized here for clarity, not correctness.

    Question question;
    question.qname = toName(domain);
    question.qtype = static_cast<std::uint16_t>(type);
    question.qclass = 1; // IN.
    message.questions.push_back(std::move(question));

    return message;
}

// ============================================================
//  Section 4 — Query Execution
// ============================================================

QueryResult QueryEngine::query(std::string_view domain, RecordType type) {
    std::string key = cacheKey(domain, type);

    if (cache_.has_value()) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (CacheEntry* entry = cache_->get(key)) {
            if (std::chrono::steady_clock::now() < entry->expiresAt)
                return entry->result; // Fresh hit -- no UDP round trip.
            // Expired: fall through and re-query for real. The stale
            // entry gets overwritten below once the fresh result is in.
        }
    }

    QueryResult result;

    // Build outbound query bytes.
    Message queryMessage = buildQuery(domain, type);
    Vector<std::byte> queryBytes;
    Status buildStatus = Builder::build(queryMessage, queryBytes);
    if (buildStatus != Status::OK) {
        result.status = QueryStatus::BuildFailed;
        return result;
    }

    // Lazily open the socket.
    if (socketFd_ < 0) {
        socketFd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (socketFd_ < 0) {
            result.status = QueryStatus::SendFailed;
            return result;
        }

        struct timeval tv {};
        tv.tv_sec = static_cast<time_t>(timeoutMs_ / 1000);
        tv.tv_usec = static_cast<suseconds_t>((timeoutMs_ % 1000) * 1000);
        ::setsockopt(socketFd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    // Resolve resolverHost_ to a sockaddr. Supports a literal IPv4
    // address (the expected input, e.g. "8.8.8.8") without pulling in
    // getaddrinfo()'s own DNS lookup for what should be a fixed,
    // pre-resolved resolver endpoint.
    struct sockaddr_in resolverAddr {};
    resolverAddr.sin_family = AF_INET;
    resolverAddr.sin_port = htons(resolverPort_);
    if (::inet_pton(AF_INET, resolverHost_.c_str(), &resolverAddr.sin_addr) != 1) {
        result.status = QueryStatus::SendFailed;
        return result;
    }

    // Send. Vector<std::byte> has no data(): operator[] returns a
    // reference to contiguous backing storage, so &queryBytes[0] gets
    // the same raw pointer data() would.
    ssize_t sent =
        ::sendto(socketFd_, &queryBytes[0], queryBytes.size(), 0,
                 reinterpret_cast<struct sockaddr*>(&resolverAddr), sizeof(resolverAddr));
    if (sent < 0 || static_cast<std::size_t>(sent) != queryBytes.size()) {
        result.status = QueryStatus::SendFailed;
        return result;
    }

    // Receive.
    std::array<std::byte, kMaxResponseBytes> responseBuffer{};
    ssize_t received =
        ::recvfrom(socketFd_, responseBuffer.data(), responseBuffer.size(), 0, nullptr, nullptr);
    if (received < 0) {
        result.status = (errno == EAGAIN || errno == EWOULDBLOCK) ? QueryStatus::Timeout
                                                                  : QueryStatus::SendFailed;
        return result;
    }

    // Parse.
    std::span<const std::byte> responseSpan(responseBuffer.data(),
                                            static_cast<std::size_t>(received));
    Status parseStatus = Parser::parse(responseSpan, result.message);
    if (parseStatus != Status::OK) {
        result.status = QueryStatus::ParseFailed;
        result.parseStatus = parseStatus;
        return result;
    }

    // Retained alongside `message` so a caller can later decode a
    // compressed domain name embedded in a record's rdata -- see
    // QueryResult::rawResponse's doc comment.
    result.rawResponse.reserve(responseSpan.size());
    for (std::byte b : responseSpan)
        result.rawResponse.push_back(b);

    result.status = QueryStatus::Ok;

    if (cache_.has_value()) {
        CacheEntry entry{result, std::chrono::steady_clock::now() + cacheDurationFor(result)};
        std::lock_guard<std::mutex> lock(cacheMutex_);
        cache_->put(key, std::move(entry));
    }

    return result;
}

} // namespace DnsCheckup
