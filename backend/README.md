# backend

The DnsLint C++ backend: an HTTP API that runs live DNS checks against a domain and keeps the history of
every run. Built with CMake; its dependencies live as git submodules under `external/`:

| Submodule | CMake target | Role |
|---|---|---|
| `dnsresolver` | `DnsPro` | DNS packet building and parsing |
| `falconhttp` | `FalconHTTP` | HTTP server, routing, middleware, CORS |
| `ratethrottle` | `ThrottlePro` | The rate limiter behind `RateLimiterMiddleware` |
| `minidatabase` | `MiniDB` | Persistence for the check history |

See the root [README](../README.md) for the whole project, and [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md)
for how this code fits together.

## Building

Out-of-source build, from `backend/`:

```bash
cmake -B build -S .
cmake --build build
mkdir -p data          # the check history is written to data/dnslint.json
./build/dnslint_backend
```

Never build in-source (`cmake -B . -S .`); see the root `.gitignore` notes on why.

The build produces:

| Target | What it is |
|---|---|
| `dnslint_backend` | The server (port 8080) |
| `dnslint_core` | Everything under `src/` except `main.cpp`, as a static library the other targets link |
| `dnslint_tests` | The unit tests (`-DDNSLINT_BUILD_TESTS=OFF` skips them) |
| `fuzz_*` | libFuzzer harnesses, only with `-DDNSLINT_BUILD_FUZZERS=ON` and a fuzzing engine |

If you rename `dnslint_backend`, also update `BINARY_NAME` in `../Dockerfile`.

## Tests

```bash
cmake --build build --target dnslint_tests
./build/dnslint_tests            # every suite
```

`tests/support/mockDns.h` is a small DNS server on `127.0.0.1` that the QueryEngine and check tests use, so
no test touches the network.

## Layout

```
include/   CheckResult.h, QueryEngine.h
src/
  checks/       MissingRecordChecks.h, EmailSecurityChecks.h, TtlChecks.h, RDataDecode.h
  database/     HistoryStore (MiniDB-backed check history)
  middleware/   RateLimiterMiddleware.h
  routes/       CheckRoutes.h   (POST /api/check, GET /api/history)
  QueryEngine.cpp, main.cpp
tests/     unit suites and the mock DNS server
fuzz/      libFuzzer harnesses and seed corpora
external/  the four submodules
```
