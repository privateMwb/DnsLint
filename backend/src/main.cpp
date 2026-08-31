/**
 * @file main.cpp
 * @brief DnsLint backend entry point.
 */

// clang-format off
#include <FalconHTTP/FalconHTTP.h>      // FalconHTTP::Core::Server, FalconHTTP::Routing::Router, FalconHTTP::Config::ServerConfig
#include <FalconHTTP/Middleware/Cors.h> // FalconHTTP::Middleware::Cors

#include <QueryEngine.h>                      // DnsCheckup::QueryEngine
#include <middleware/RateLimiterMiddleware.h> // DnsCheckup::Middleware::RateLimiterMiddleware
#include <routes/CheckRoutes.h>               // DnsCheckup::Routes::postCheck

#include <chrono> // std::chrono::milliseconds
#include <cstdio> // std::fprintf, stderr
// clang-format on

int main() {
    // One shared QueryEngine for the process's lifetime. Safe only
    // because Server is thread-per-connection and this is the sole
    // route touching it -- see CheckRoutes.h's postCheck() doc comment
    // for the tradeoff if concurrent load ever makes this a bottleneck.
    // Also does its own transparent, TTL-bounded response caching now
    // (default: up to 500 distinct (domain, RecordType) results kept
    // cached) -- see QueryEngine.h's constructor doc comment.
    DnsCheckup::QueryEngine engine("8.8.8.8", 53, 2000);

    // Persists every check run. "data/" is expected to exist relative
    // to wherever dnslint_backend is actually run from -- if deployed
    // in a container, this path MUST be a mounted persistent volume,
    // not a path inside the container's own writable layer, or every
    // redeploy silently wipes it (see HistoryStore.h's class doc
    // comment for the schema-versioning side of "done right"; this is
    // the deployment-persistence side of it).
    DnsCheckup::HistoryStore history;
    MiniDB::Common::Status historyOpenStatus = history.open("data/dnslint.json");
    if (historyOpenStatus != MiniDB::Common::Status::OK) {
        std::fprintf(stderr,
                     "HistoryStore::open failed: %s -- check history will not be saved this run.\n",
                     MiniDB::Common::statusToString(historyOpenStatus));
        // Not fatal: postCheck() degrades gracefully (checks still run
        // and respond; nothing gets persisted) rather than refusing to
        // serve requests over a history-store problem alone.
    }

    FalconHTTP::Routing::Router router;
    router.post("/api/check", [&engine, &history](const FalconHTTP::HTTP::HttpRequest& request,
                                                  FalconHTTP::HTTP::HttpResponse& response) {
        DnsCheckup::Routes::postCheck(engine, history, request, response);
    });
    router.get("/api/history", [&history](const FalconHTTP::HTTP::HttpRequest& request,
                                          FalconHTTP::HTTP::HttpResponse& response) {
        DnsCheckup::Routes::getHistory(history, request, response);
    });

    FalconHTTP::Config::ServerConfig config;
    config.port = 8080;
    config.threadCount = 4;

    FalconHTTP::Core::Server server(router, config);

    // CORS: the frontend (Vite dev server, e.g. localhost:5173) and this
    // backend (localhost:8080) are different origins even when both run
    // on the same machine -- different port is enough to trigger the
    // browser's same-origin policy. Without this, every fetch() from
    // the frontend fails at the browser level before FalconHTTP even
    // gets to look at the request, surfacing to the frontend as "could
    // not reach the backend" even though the server is up and fine.
    // "*" (allow any origin) is safe here since this API takes no
    // cookies/auth and returns nothing sensitive per-caller.
    server.use(FalconHTTP::Middleware::Cors());

    // RateLimiterMiddleware is not copy-constructible (it holds a
    // rain::RateLimiter, which holds a std::mutex), but
    // FalconHTTP::Middleware::MiddlewareFn requires a copy-constructible
    // callable (see FunctionPro::Function). Registering the object
    // directly does not compile -- keep it as a named, long-lived
    // object and register a lambda that captures it by reference
    // instead; a reference capture is trivially copyable regardless of
    // what it points to.
    DnsCheckup::Middleware::RateLimiterMiddleware rateLimiter(60, std::chrono::milliseconds(60000));
    server.use([&rateLimiter](FalconHTTP::HTTP::HttpRequest& request,
                              FalconHTTP::HTTP::HttpResponse& response,
                              const FalconHTTP::Middleware::NextHandler& next) {
        rateLimiter(request, response, next);
    });

    if (!server.start(config.port)) {
        std::fprintf(stderr, "Failed to bind port %u.\n", config.port);
        return 1;
    }

    std::printf("DnsLint backend listening on port %u.\n", config.port);
    std::fflush(stdout); // stdout is fully buffered when not a tty (e.g. piped
                         // to a log file); without this, the line above can
                         // sit in the buffer indefinitely since server.run()
                         // blocks right after and never triggers a flush.
    server.run();
    return 0;
}
