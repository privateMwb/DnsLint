# Changelog

All notable changes to DnsLint are documented in this file.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed

- **Concurrent checks could receive each other's DNS responses.** The server shares one `QueryEngine`
  across its worker threads, and the engine kept a single UDP socket (created lazily, without a lock), so
  two checks running at once could read each other's replies off it and report results for the wrong
  domain or record. Every query now uses its own socket. Regression test:
  `concurrent_queries_get_their_own_answers`.
- **`GET /api/history` returned the oldest runs, not the newest, once a domain had more than the limit.**
  MiniDB's `select()` stops scanning when it reaches `limit` and only then sorts, so asking for the newest
  20 gave the first 20 stored, sorted newest-first. `HistoryStore::getHistory()` now sorts everything for
  the domain and cuts to the limit itself.
- **SPF and DMARC reported a hard `fail` ("No SPF record found") when the resolver couldn't be reached.**
  They now warn ("Query for TXT records timed out"), like the other checks.
- **`normalizeDomain()` left whitespace inside the domain** when it sat before a path, query or port
  (`"example.com /x"` became `"example.com "`), which was then queried and saved under that key. It now
  trims again after stripping.
- **The Sanitizers workflow tested nothing.** It ran `./build/app_backend`, a binary that doesn't exist
  here, so both jobs passed vacuously. It now builds and runs `dnslint_backend`, with real requests.

### Added

- Unit test suite (`backend/tests/`, target `dnslint_tests`): domain handling, the rdata decoders,
  `QueryEngine` (answers, caching, timeouts, garbage replies, concurrency) and every check, all against an
  in-process mock DNS server, plus `HistoryStore`. CI runs it after every build and under the sanitizers.
  CMake option `DNSLINT_BUILD_TESTS`, on by default.
- ClusterFuzzLite fuzzing (`.clusterfuzzlite/`, `backend/fuzz/`): harnesses for the domain input, the rdata
  decoders and the whole DNS response path, with seed inputs, a short run on pull requests and a longer
  nightly run. Harnesses are picked up automatically from `backend/fuzz/fuzz_*.cpp` (CMake option
  `DNSLINT_BUILD_FUZZERS`, off by default).
- `GET /api/health`, a liveness endpoint for Render's health check and `scripts/check-demo.sh`.
- Sanitizer jobs now run the unit tests and boot the real backend, with real and concurrent requests.
- Screenshots workflow (`screenshots.yml`, `scripts/capture-screenshots.mjs`): captures the frontend in a
  headless browser, locally on the runner or from a live deployment, and opens a PR with the images.
- Demo image workflow (`demo-image.yml`) and `scripts/check-demo.sh`: build the Docker image and check the
  running container from outside. The script also works against a deployed service.
- `render.yaml` (Render Blueprint), `frontend/vercel.json`, and the `main-protection` branch ruleset
  (`.github/rulesets/main-protection.json`).
- CodeQL stays; `CODEOWNERS`, `.dockerignore`, and Dependabot for GitHub Actions, npm, Docker and the git
  submodules, with updates grouped into one PR per ecosystem.
- `CHANGELOG.md`, `CONTRIBUTING.md`, `SECURITY.md`, and guides under `docs/` (`ARCHITECTURE.md`,
  `DEMO.md`, `DEPLOYMENT.md`).

### Changed

- Everything under `backend/src/` except `main.cpp` is built once as a static library (`dnslint_core`) that
  the backend, the tests and the fuzz harnesses all link.
- The frontend CI job runs on Node 22.
- The Docker build turns the test target off (`-DDNSLINT_BUILD_TESTS=OFF`).
- The root README is restructured to match FalconEye's: badge groups, a table of contents with the same
  sections, diagrams, a Design Notes section, and a collapsible gallery of screenshots.

## [1.0.0]

First release: a DNS health checkup tool built on DnsResolver, FalconHTTP, RateThrottle and MiniDatabase.

### Added

- `POST /api/check`: eight checks (AAAA, MX and NS presence; SPF, DMARC and DKIM; A and MX TTL) with the
  actual decoded values in each message.
- `GET /api/history?domain=...`: past runs for a domain, newest first, persisted through MiniDB.
- Apex-domain-aware routing for MX, NS, DMARC and MX TTL; domain input normalization (scheme, path, query
  and port stripped); best-effort DKIM against four common selectors.
- Response caching keyed on name and record type, with a lifetime taken from the records' own TTLs.
- Per-client rate limiting (60 requests a minute, keyed on `X-Forwarded-For`) and CORS.
- A React frontend: check form, resolution trace, score summary and result rows, and an About page.
- CI: Build, Clang Format, Clang Tidy, Sanitizers, CodeQL, Frontend, Docs (Doxygen to GitHub Pages) and
  Release workflows.

[Unreleased]: https://github.com/privateMwb/DnsLint/compare/v1.0.0...HEAD
[1.0.0]: https://github.com/privateMwb/DnsLint/releases/tag/v1.0.0
