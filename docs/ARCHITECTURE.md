# Architecture

DnsLint has two halves: a C++ backend (FalconHTTP for HTTP, DnsResolver for DNS packets, MiniDB
for the check history, RateThrottle for rate limiting) and a React frontend that talks to it
over JSON.

```
┌──────────────────────────────── backend/ ─────────────────────────────────┐
│ FalconHTTP Server (4 worker threads)                                       │
│   Cors → RateLimiter → Router                                              │
│                                  │                                         │
│        POST /api/check ──► postCheck ──► 8 checks ──► QueryEngine ──UDP──► resolver (8.8.8.8:53)
│                                  │                       │ response cache  │
│                                  ▼                       │ (mutex, LRU)    │
│                            HistoryStore ──► MiniDB ──► data/dnslint.json   │
│        GET /api/history ──► getHistory ──┘                                 │
└────────────────────────────────┬──────────────────────────────────────────┘
                                 │  JSON
┌────────────────────────────────▼──────── frontend/ ───────────────────────┐
│ React + Vite + Tailwind: check form, resolution trace, score, result rows  │
└────────────────────────────────────────────────────────────────────────────┘
```

## The request path

1. A request enters FalconHTTP's middleware chain. `Cors` adds its headers (it is constructed
   with no argument, so it allows every origin, `*`).
2. `RateLimiterMiddleware` derives a client key from the first address in `X-Forwarded-For` and
   asks RateThrottle's `RateLimiter` for permission (60 requests per minute, up to 10,000 clients
   tracked before LRU eviction). Over the limit it answers `429` and does not call `next()`.
3. The router matches one of three routes:

| Route | Handler | What it does |
|---|---|---|
| `POST /api/check` | `postCheck` | Runs all eight checks for a domain and saves the run |
| `GET /api/history?domain=...` | `getHistory` | Returns the most recent saved runs for a domain |
| `GET /api/health` | inline in `main.cpp` | Liveness: `ok` |

4. `postCheck` parses the JSON body, **normalizes** the domain, rejects it with `400` if it isn't
   acceptable, runs the checks, records the run, and answers with the domain and its checks.

## Domain handling

`normalizeDomain()` cleans what a visitor pastes: it trims whitespace, strips a leading
`https://`, `http://` or `//`, drops everything from the first `/`, `?` or `#`, drops a `:port`,
trims again (so `"example.com /x"` doesn't leave a trailing space behind), and lowercases.
`isAcceptableDomain()` then requires a non-empty name of at most 255 characters containing a dot.

Some records are properties of a domain's **apex**, not of a subdomain, so `apexOf()` reduces the
name to its last two labels (a deliberately naive rule: `co.uk`-style public suffixes are not
handled) and the checks use whichever name makes sense:

| Check | Record | Queried name |
|---|---|---|
| `missing_aaaa` | AAAA | the submitted domain |
| `missing_mx` | MX | the apex |
| `missing_ns` | NS | the apex |
| `spf_present` | TXT (`v=spf1`) | the submitted domain |
| `dmarc_present` | TXT at `_dmarc.<apex>` (`v=DMARC1`) | the apex |
| `dkim_present` | TXT at `<selector>._domainkey.<domain>` | the submitted domain |
| `ttl_a` | A TTL | the submitted domain |
| `ttl_mx` | MX TTL | the apex |

Each check returns `pass`, `warn` or `fail` with a message that shows the **decoded value**: the
IPv6 address, the mail server and nameserver names, the SPF/DMARC text. A resolver that doesn't
answer is a `warn`, never a `fail`, because "couldn't ask" is not "missing". DKIM tries four common
selectors (`google`, `selector1`, `default`, `k1`) and warns rather than fails when none match,
since a different selector can't be told apart from none. TTL checks warn below 300 s (needless
query load) and above 48 hours (slow to propagate a fix).

## QueryEngine

`QueryEngine` sends one UDP query to a fixed recursive resolver (`8.8.8.8:53`, 2 s timeout) and
parses the reply with DnsResolver. Its design points:

- **One socket per query.** The server shares a single engine across its worker threads. An earlier
  version kept one socket on the engine, so two checks running at once could read each other's DNS
  responses off it (and the lazy socket creation was itself a data race). Each `query()` now opens
  and closes its own socket, which makes every call independent. `concurrent_queries_get_their_own_answers`
  in the test suite covers this.
- **Cache keyed on name and record type**, capacity 500 (LRU), guarded by a mutex. A successful
  answer is cached for the *shortest* TTL among its records (like a real resolver); a successful
  but empty answer for a fixed 60 s. A timeout or a parse failure is never cached.
- **The raw response is kept** next to the parsed message, because a compressed name inside an
  answer (an MX exchange, an NS name) is a pointer into the *original* packet and can only be
  decoded against it. `RDataDecode.h` does that decoding and treats everything as hostile: length
  bytes that overrun, pointer loops and wild offsets all end in an empty result.

## HistoryStore

Every completed run is saved through MiniDB to `data/dnslint.json`: the normalized domain, the
time, and the checks array. `getHistory()` returns the newest runs for a domain, newest first.
Two details worth knowing:

- MiniDB's `select()` stops scanning as soon as it has `limit` matches and *then* sorts, which
  returns the oldest runs rather than the newest. `getHistory()` therefore sorts everything for the
  domain and cuts to the limit itself.
- On startup the store scans for the highest existing run id, so ids never repeat after a restart.

A store that fails to open degrades the app (no history saved or served) without failing
`POST /api/check`: the visitor is still owed their result.

## The frontend

A Vite + React + TypeScript app with Tailwind and `react-router-dom`: a checker page (form, a
resolution trace while it runs, a score summary, one row per check) and an About page. It reads the
backend's address from `VITE_API_BASE_URL` (default `http://localhost:8080`) and turns a refused
connection and a `429` into readable messages.

## Tests and fuzzing

`backend/tests/` holds the unit suites (`dnslint_tests`), which CI runs after every build and again
under the sanitizers. QueryEngine and every check run over real UDP against an in-process mock DNS
server (`tests/support/mockDns.h`), so nothing touches the network.

`backend/fuzz/` holds libFuzzer harnesses that ClusterFuzzLite builds under ASan and UBSan:

| Harness | Checks |
|---|---|
| `fuzz_domain_input` | `normalizeDomain()` never leaves whitespace, uppercase, a scheme or `/ ? # :` behind; an accepted domain meets the rules; `apexOf()` returns a short suffix |
| `fuzz_rdata_decode` | `decodeTxt()`, `decodeAaaa()` and `decodeName()` never read out of bounds or loop, for any bytes and any offset |
| `fuzz_dns_response` | A hostile datagram through `Parser::parse()` and every check decoder, as the live server runs them |

Each runs for 3 minutes on pull requests that touch the code involved, and for 30 minutes every night.
