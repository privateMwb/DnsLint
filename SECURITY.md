# Security Policy

## Supported versions

DnsLint is a small, individually maintained project. Security fixes are made on the `main` branch and
shipped in the next release; older releases are not patched.

| Version | Supported |
|---|:-:|
| `main` and the latest release | ✅ |
| Anything older | ❌ |

## Reporting a vulnerability

**Please do not report a suspected vulnerability in a public issue, pull request, or discussion.** Use
GitHub's private reporting instead, so a fix can be prepared before the details are public:

1. Open this repository's **Security** tab.
2. Choose **Report a vulnerability**.
3. Describe the problem.

A useful report includes:

- what is affected (the backend, a specific route, the frontend, the Docker image) and the commit or
  release you tested;
- steps to reproduce, ideally the exact `curl` request or input bytes;
- what you expected and what happened (a crash, a sanitizer report, wrong data returned to a visitor);
- any suggested fix.

This is a spare-time project, so responses are best-effort. You will get an acknowledgement and an honest
assessment, and credit in the fix if you want it.

## Security model: read this before deploying

DnsLint is a portfolio-scale tool. It has **no accounts, no authentication, and no authorization**, and
these limits are deliberate scope decisions, not oversights:

- **Every check sends live DNS queries on a visitor's behalf.** One `POST /api/check` is roughly a dozen
  UDP queries to the resolver (`8.8.8.8`). The API is rate limited to 60 requests per minute per client
  to keep that from being abused, but see the next point.
- **Rate limiting trusts `X-Forwarded-For`.** The limiter keys on the first address in that header, which
  a caller can set to anything if they can reach the backend directly rather than through the trusted
  proxy. When the header is missing, every caller shares one bucket.
- **The queried domain is disclosed to the resolver.** Checked names are sent in clear text over UDP to
  `8.8.8.8`, so Google's resolver sees every domain a visitor checks. Don't use a shared instance for
  domains you consider private.
- **The check history is public and shared.** `GET /api/history?domain=...` returns the past runs for
  any domain to anyone who asks, including when each run happened. History is never pruned.
- **The resolver is fixed.** The backend only ever sends UDP to its configured resolver; user input
  becomes the *name being asked about*, never an address to connect to. The name is length-limited and
  must contain a dot, and a label over 63 bytes is refused before anything is sent.
- **DNS answers are untrusted input.** Responses are parsed and decoded defensively (overrunning
  lengths, compression-pointer loops and bad offsets end in an empty result) and are fuzzed, but there
  is no DNSSEC validation: a spoofed or poisoned answer is reported as if it were real.
- **CORS allows every origin.** The backend sends `Access-Control-Allow-Origin: *`. That is safe here
  because the API takes no cookies and returns nothing caller-specific, and CORS is a browser rule, not
  access control, either way.
- **No TLS.** Traffic is plaintext HTTP unless a proxy such as Render terminates it.
- **Storage is one JSON file** (`data/dnslint.json`) with a schema version and a migration hook.

## Automated checks

CodeQL, clang-tidy, sanitizer runs (ASan, UBSan, TSan) over both the unit tests and the real server, and
ClusterFuzzLite fuzzing of the domain handling, the DNS rdata decoders and the whole response-parsing path
run in CI. They reduce risk; they do not prove the absence of bugs.
