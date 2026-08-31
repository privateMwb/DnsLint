/**
 * @file            RateLimiterMiddleware.h
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
#include <FalconHTTP/HTTP/HttpRequest.h>      // HttpRequest
#include <FalconHTTP/HTTP/HttpResponse.h>     // HttpResponse
#include <FalconHTTP/HTTP/HttpStatus.h>       // HttpStatus
#include <FalconHTTP/Middleware/Middleware.h> // NextHandler

#include <ThrottlePro/RateLimiter.h> // rain::RateLimiter (== ThrottlePro::RateLimiter)

#include <chrono>  // std::chrono::milliseconds
#include <cstddef> // std::size_t
#include <string>  // std::string
// clang-format on

// FalconHTTP integration wrapper for ThrottlePro::RateLimiter -- same
// adapter pattern as Shrtn's RateLimiterMiddleware.h. Limits per visitor
// (by client IP), not per submitted domain. This does not, by itself,
// stop one visitor from cycling through many different domains and
// still generating one outbound UDP query to a real nameserver per
// check -- see this file's class-level note if that ever needs its own
// per-domain throttle on top of this.

namespace DnsCheckup::Middleware {

namespace detail {

inline constexpr std::string_view kUnknownClientKey = "unknown";
inline constexpr std::string_view kForwardedForHeader = "X-Forwarded-For";

/// @brief Derives the rate-limit key for a request.
/// @details X-Forwarded-For may be a comma-separated chain
/// ("client, proxy1, proxy2"); the first entry is the original client.
/// Not defended against spoofing -- a caller can set this header to
/// anything if it reaches DnsCheckup directly rather than through a
/// trusted proxy. HttpRequest has no client-IP accessor of its own (see
/// HttpRequest.h), same limitation Shrtn hit -- confirm at deployment
/// time whether DnsCheckup actually sits behind a proxy that sets this
/// header; if not, every caller collapses onto the shared "unknown"
/// bucket (a known, accepted degradation, not a crash).
inline std::string clientKey(const FalconHTTP::HTTP::HttpRequest& request) {
    if (!request.hasHeader(std::string(kForwardedForHeader))) {
        return std::string(kUnknownClientKey);
    }

    const std::string forwarded = request.header(std::string(kForwardedForHeader));
    const std::size_t comma = forwarded.find(',');
    return comma == std::string::npos ? forwarded : forwarded.substr(0, comma);
}

} // namespace detail

/**
 * @class RateLimiterMiddleware
 * @brief Adapts ThrottlePro::RateLimiter to FalconHTTP's middleware chain.
 * @details Keys on the caller's X-Forwarded-For address (see file
 * comment). On a denied request, short-circuits with `429 Too Many
 * Requests` and does NOT call `next`. `operator()` is intentionally not
 * `const`: RateLimiter::allow() mutates its internal cache.
 *
 * This limits how often one visitor can call `POST /api/check` at all;
 * it does not limit how many distinct domains one visitor checks within
 * their allowance, and each check fans out into several real UDP queries
 * against third-party nameservers (see QueryEngine.h). If that outbound
 * query volume ever needs its own cap independent of visitor request
 * count, that's a second, separate limiter keyed on the submitted domain
 * rather than the client -- not something this class currently does.
 */
class RateLimiterMiddleware {
  public:
    /**
     * @param requestsPerWindow Maximum allowed requests per client, per window.
     * @param windowDuration Length of the fixed window.
     * @param cacheCapacity Max distinct clients tracked before LRU eviction.
     * Defaults to 10,000 -- revisit if DnsCheckup's expected distinct-client
     * volume per window is likely to exceed that.
     */
    RateLimiterMiddleware(std::size_t requestsPerWindow, std::chrono::milliseconds windowDuration,
                          std::size_t cacheCapacity = 10000)
        : limiter_(requestsPerWindow, windowDuration, cacheCapacity) {}

    void operator()(FalconHTTP::HTTP::HttpRequest& request,
                    FalconHTTP::HTTP::HttpResponse& response,
                    const FalconHTTP::Middleware::NextHandler& next) {
        const std::string key = detail::clientKey(request);

        if (!limiter_.allow(key)) {
            response.setStatus(FalconHTTP::HTTP::HttpStatus::TooManyRequests);
            response.setBody("Rate limit exceeded. Please slow down and try again shortly.");
            return;
        }

        next(request, response);
    }

  private:
    rain::RateLimiter limiter_;
};

} // namespace DnsCheckup::Middleware
