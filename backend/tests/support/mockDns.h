// ============================================================
// tests/support/mockDns.h
//
// A tiny DNS responder on 127.0.0.1 (UDP, ephemeral port) for tests, so
// QueryEngine and the checks run against real UDP round trips without
// ever touching the network. Answers are canned per (name, type); a
// request for anything else gets an empty NOERROR answer. It can also
// stay silent (to force a timeout) or send raw bytes back (to force a
// parse failure), and it counts the requests it receives (to prove
// caching).
// ============================================================

#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace mockdns {

using Bytes = std::vector<std::uint8_t>;

/// Encodes "a.b.c" as length-prefixed labels ending in a zero byte.
inline Bytes encodeName(const std::string& name) {
    Bytes out;
    std::size_t start = 0;
    while (start <= name.size()) {
        std::size_t dot = name.find('.', start);
        std::size_t end = dot == std::string::npos ? name.size() : dot;
        if (end > start) {
            out.push_back(static_cast<std::uint8_t>(end - start));
            out.insert(out.end(), name.begin() + static_cast<std::ptrdiff_t>(start),
                       name.begin() + static_cast<std::ptrdiff_t>(end));
        }
        if (dot == std::string::npos)
            break;
        start = dot + 1;
    }
    out.push_back(0);
    return out;
}

/// TXT rdata: one character-string per piece (each at most 255 bytes).
inline Bytes txtRdata(const std::string& text) {
    Bytes out;
    out.push_back(static_cast<std::uint8_t>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
    return out;
}

/// MX rdata: 16-bit preference, then the exchange name.
inline Bytes mxRdata(std::uint16_t preference, const std::string& exchange) {
    Bytes out{static_cast<std::uint8_t>(preference >> 8), static_cast<std::uint8_t>(preference)};
    Bytes name = encodeName(exchange);
    out.insert(out.end(), name.begin(), name.end());
    return out;
}

inline Bytes ipv4Rdata(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) {
    return {a, b, c, d};
}

/// AAAA rdata for 2001:db8::1.
inline Bytes ipv6Rdata() {
    return {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01};
}

struct Answer {
    std::uint16_t type;
    std::uint32_t ttl;
    Bytes rdata;
};

class Server {
  public:
    Server() {
        fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        ::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

        socklen_t len = sizeof(addr);
        ::getsockname(fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);

        timeval tv{};
        tv.tv_usec = 20 * 1000; // wake up regularly so stop() is prompt
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        thread_ = std::thread([this] { loop(); });
    }

    ~Server() {
        running_ = false;
        if (thread_.joinable())
            thread_.join();
        ::close(fd_);
    }

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    [[nodiscard]] std::uint16_t port() const { return port_; }
    [[nodiscard]] int requests() const { return requests_; }

    /// Canned answers for (name, type). Name is matched case-insensitively.
    void answer(const std::string& name, std::uint16_t type, std::vector<Answer> answers) {
        std::lock_guard<std::mutex> lock(mutex_);
        answers_[key(name, type)] = std::move(answers);
    }

    /// Reply with exactly these bytes (not a valid DNS message) for (name, type).
    void rawReply(const std::string& name, std::uint16_t type, Bytes bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        raw_[key(name, type)] = std::move(bytes);
    }

    /// Stop answering: every query then times out.
    void silence(bool silent) { silent_ = silent; }

  private:
    int fd_ = -1;
    std::uint16_t port_ = 0;
    std::thread thread_;
    std::atomic<bool> running_{true};
    std::atomic<bool> silent_{false};
    std::atomic<int> requests_{0};
    std::mutex mutex_;
    std::map<std::string, std::vector<Answer>> answers_;
    std::map<std::string, Bytes> raw_;

    static std::string key(std::string name, std::uint16_t type) {
        for (char& c : name)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return name + "|" + std::to_string(type);
    }

    void loop() {
        std::uint8_t buf[1500];
        while (running_) {
            sockaddr_in from{};
            socklen_t fromLen = sizeof(from);
            ssize_t n = ::recvfrom(fd_, buf, sizeof(buf), 0, reinterpret_cast<sockaddr*>(&from),
                                   &fromLen);
            if (n < 17) // header (12) + at least a root label and type/class
                continue;
            ++requests_;
            if (silent_)
                continue;

            // Question name starts right after the 12-byte header.
            std::string name;
            std::size_t pos = 12;
            while (pos < static_cast<std::size_t>(n) && buf[pos] != 0) {
                std::size_t len = buf[pos++];
                if (pos + len > static_cast<std::size_t>(n))
                    break;
                if (!name.empty())
                    name += '.';
                name.append(reinterpret_cast<char*>(buf) + pos, len);
                pos += len;
            }
            ++pos; // the root label
            if (pos + 4 > static_cast<std::size_t>(n))
                continue;
            std::uint16_t type = static_cast<std::uint16_t>((buf[pos] << 8) | buf[pos + 1]);
            std::size_t questionEnd = pos + 4;

            Bytes reply;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto rawIt = raw_.find(key(name, type));
                if (rawIt != raw_.end()) {
                    reply = rawIt->second;
                } else {
                    reply = buildReply(buf, questionEnd, answers_[key(name, type)]);
                }
            }
            ::sendto(fd_, reply.data(), reply.size(), 0, reinterpret_cast<sockaddr*>(&from),
                     fromLen);
        }
    }

    static void put16(Bytes& out, std::uint16_t v) {
        out.push_back(static_cast<std::uint8_t>(v >> 8));
        out.push_back(static_cast<std::uint8_t>(v));
    }

    static Bytes buildReply(const std::uint8_t* query, std::size_t questionEnd,
                            const std::vector<Answer>& answers) {
        Bytes out;
        out.push_back(query[0]); // id, echoed
        out.push_back(query[1]);
        put16(out, 0x8180); // QR, RD, RA, NOERROR
        put16(out, 1);      // QDCOUNT
        put16(out, static_cast<std::uint16_t>(answers.size()));
        put16(out, 0); // NSCOUNT
        put16(out, 0); // ARCOUNT
        out.insert(out.end(), query + 12, query + questionEnd);

        for (const Answer& a : answers) {
            put16(out, 0xC00C); // name: pointer to the question name at offset 12
            put16(out, a.type);
            put16(out, 1); // class IN
            put16(out, static_cast<std::uint16_t>(a.ttl >> 16));
            put16(out, static_cast<std::uint16_t>(a.ttl & 0xFFFF));
            put16(out, static_cast<std::uint16_t>(a.rdata.size()));
            out.insert(out.end(), a.rdata.begin(), a.rdata.end());
        }
        return out;
    }
};

} // namespace mockdns
