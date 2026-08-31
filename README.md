<p align="center">
  <img src=".github/assets/banner.svg" alt="DnsLint" width="100%">
</p>

<p align="center">
  <img src="https://img.shields.io/github/v/release/privateMwb/DnsLint?style=for-the-badge&logo=github&color=37E8A0&labelColor=0B0F0D" alt="Version">
  <img src="https://img.shields.io/badge/License-MIT-FF8A3D?style=for-the-badge&labelColor=0B0F0D" alt="License - MIT">
  <img src="https://img.shields.io/badge/C%2B%2B-23-00599C?style=for-the-badge&logo=c%2B%2B&labelColor=0B0F0D" alt="C++ - 23">
  <img src="https://img.shields.io/badge/React-Vite-61DAFB?style=for-the-badge&logo=react&labelColor=0B0F0D" alt="React - Vite">
</p>

<p align="center">
  <a href="https://github.com/privateMwb/DnsLint/actions/workflows/build.yml">
    <img src="https://github.com/privateMwb/DnsLint/actions/workflows/build.yml/badge.svg" alt="Build">
  </a>
  <a href="https://github.com/privateMwb/DnsLint/actions/workflows/frontend.yml">
    <img src="https://github.com/privateMwb/DnsLint/actions/workflows/frontend.yml/badge.svg" alt="Frontend">
  </a>
  <a href="https://github.com/privateMwb/DnsLint/actions/workflows/sanitizers.yml">
    <img src="https://github.com/privateMwb/DnsLint/actions/workflows/sanitizers.yml/badge.svg" alt="Sanitizers">
  </a>
  <a href="https://github.com/privateMwb/DnsLint/actions/workflows/clang-tidy.yml">
    <img src="https://github.com/privateMwb/DnsLint/actions/workflows/clang-tidy.yml/badge.svg" alt="Clang Tidy">
  </a>
  <a href="https://github.com/privateMwb/DnsLint/actions/workflows/clang-format.yml">
    <img src="https://github.com/privateMwb/DnsLint/actions/workflows/clang-format.yml/badge.svg" alt="Clang Format">
  </a>
  <a href="https://github.com/privateMwb/DnsLint/actions/workflows/docs.yml">
    <img src="https://github.com/privateMwb/DnsLint/actions/workflows/docs.yml/badge.svg" alt="Documentation">
  </a>
  <a href="https://github.com/privateMwb/DnsLint/actions/workflows/release.yml">
    <img src="https://github.com/privateMwb/DnsLint/actions/workflows/release.yml/badge.svg" alt="Release">
  </a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/GCC-support-B46F1B?style=flat&logo=gnu" alt="GCC - support">
  <img src="https://img.shields.io/badge/Clang-support-045891?style=flat&logo=llvm" alt="Clang - support">
</p>

<p align="center">
  <img src=".github/assets/divider.svg" alt="" width="100%">
</p>

<p align="center">DnsLint is a DNS health checkup tool: point it at any domain and it runs live DNS queries to flag missing records, misconfigured email security, and TTL issues — showing the actual decoded values (real IPv6 addresses, real mail server and nameserver hostnames), not just pass/fail.</p>

<br>

## 📑 Table of Contents

- [Features](#features)
- [Requirements](#requirements)
- [Getting Started](#getting-started)
- [Project Structure](#project-structure)
- [Development](#development)
- [Documentation](#documentation)
- [Deployment](#deployment)
- [Contributing](#contributing)
- [Changelog](#changelog)
- [License](#license)

<br>

## <a id="features"></a>✨ Features

- **Live DNS decoding, not just pass/fail** — AAAA, MX, and NS checks
  show the actual resolved values: real IPv6 addresses, real mail
  server and nameserver hostnames, decoded straight from the DNS wire
  format including compression-pointer-encoded names.
- **Email security checks** — SPF and DMARC checks show the actual
  matched TXT record content on a pass, not a generic "found" message.
  DKIM is checked best-effort against a handful of common selectors and
  reports "undetected" rather than a false "missing" when a
  non-standard selector is in use.
- **Apex-domain-aware routing** — MX, NS, and DMARC are checked
  against the domain's apex regardless of what subdomain was
  submitted (checking `www.example.com` still correctly evaluates
  `example.com`'s MX/NS/DMARC), since those records are apex-domain
  concepts by nature for any site.
- **TTL sanity checks** — flags A/MX record TTLs that are unusually
  low (needless query load) or unusually high (slow to propagate a
  future fix).
- **Persisted check history** — every run is saved via MiniDB and
  readable back via `GET /api/history?domain=...`, newest first.
- **Response caching** — repeat checks against the same domain skip
  the DNS round trip entirely while cached; cache duration is derived
  from the record's own real TTL, never held longer than a real
  resolver would.
- **Rate-limited API** — every check fans out into several real UDP
  queries against third-party nameservers on a visitor's behalf; the
  API is rate-limited per client to keep that from being abused.

<div align="right"><a href="#-table-of-contents"><img src=".github/assets/back-to-top.svg" alt="Back to top" height="28"></a></div>

## <a id="requirements"></a>📋 Requirements

- A C++23-conformant compiler (tested: GCC, Clang — see
  [backend/README.md](backend/README.md))
- CMake 3.20+
- Node.js 20+ and npm
- Docker, if building the backend image (optional for local dev)

<div align="right"><a href="#-table-of-contents"><img src=".github/assets/back-to-top.svg" alt="Back to top" height="28"></a></div>

## <a id="getting-started"></a>🚀 Getting Started

```bash
git clone https://github.com/privateMwb/DnsLint.git
cd DnsLint
```

**Backend:**

```bash
cd backend
cmake -B build -S .
cmake --build build
./build/dnslint_backend
```

**Frontend** (separate terminal):

```bash
cd frontend
npm install
cp .env.example .env.local   # set VITE_API_BASE_URL if the backend isn't on localhost:8080
npm run dev
```

See [backend/README.md](backend/README.md) and
[frontend/README.md](frontend/README.md) for details on either side.

<div align="right"><a href="#-table-of-contents"><img src=".github/assets/back-to-top.svg" alt="Back to top" height="28"></a></div>

## <a id="project-structure"></a>🗂️ Project Structure

<details>
<summary>Expand full tree</summary>

```
DnsLint/
├── backend/
│   ├── data/                         # runtime data -- MUST be a mounted
│   │                                  # persistent volume in any deployment
│   │                                  # (see Deployment below); dnslint.json
│   │                                  # (check history) lives here
│   ├── external/                     # vendored dependencies, as git submodules
│   │   ├── dnsresolver/              # DnsPro -- DNS packet parsing/building
│   │   ├── falconhttp/               # HTTP server, routing, middleware, CORS
│   │   ├── ratethrottle/             # ThrottlePro -- the rate limiter
│   │   └── minidatabase/             # MiniDB -- check-history persistence
│   ├── include/
│   │   ├── CheckResult.h
│   │   └── QueryEngine.h
│   ├── src/
│   │   ├── checks/
│   │   │   ├── EmailSecurityChecks.h
│   │   │   ├── MissingRecordChecks.h
│   │   │   ├── RDataDecode.h
│   │   │   └── TtlChecks.h
│   │   ├── database/                 # MiniDB-backed check history
│   │   │   ├── HistoryStore.cpp
│   │   │   └── HistoryStore.h
│   │   ├── middleware/
│   │   │   └── RateLimiterMiddleware.h
│   │   ├── routes/
│   │   │   └── CheckRoutes.h         # POST /api/check, GET /api/history
│   │   ├── main.cpp
│   │   └── QueryEngine.cpp
│   ├── CMakeLists.txt
│   └── README.md
│
├── frontend/
│   ├── public/
│   ├── src/
│   │   ├── components/               # CheckForm, ResultRow, ResolutionTrace, SummaryPanel, ...
│   │   ├── lib/                      # api client, checkMeta, cn() helper, types
│   │   ├── pages/                    # CheckerPage, AboutPage
│   │   ├── App.tsx
│   │   └── main.tsx
│   ├── .env.example
│   ├── .gitignore
│   ├── .prettierignore
│   ├── .prettierrc.json
│   ├── eslint.config.js
│   ├── index.html
│   ├── package-lock.json
│   ├── package.json
│   ├── tsconfig.app.json
│   ├── tsconfig.json
│   ├── tsconfig.node.json
│   ├── vite.config.ts
│   └── README.md
│
├── docs/
│   ├── assets/                       # diagrams/screenshots for docs
│   ├── Doxyfile
│   └── README.md
│
├── .github/
│   ├── assets/                       # banner, divider, back-to-top for README
│   ├── releases/
│   ├── workflows/                    # build, lint, sanitizers, docs, release, CodeQL
│   └── dependabot.yml
│
├── .clang-format
├── .clang-tidy
├── .gitignore
├── Dockerfile
├── init-nested-submodules.sh
├── LICENSE
└── README.md
```

</details>

<div align="right"><a href="#-table-of-contents"><img src=".github/assets/back-to-top.svg" alt="Back to top" height="28"></a></div>

## <a id="development"></a>🛠️ Development

**Formatting & linting (backend):** `.clang-format` / `.clang-tidy`
live at the repo root — see the comments in `.clang-tidy` for which
checks are disabled and why.

**Formatting & linting (frontend):**

```bash
cd frontend
npm run lint
npm run format:check
```

**Sanitizers:** boot the backend under AddressSanitizer+UBSan or
ThreadSanitizer — see `.github/workflows/sanitizers.yml`, and its
`TODO`s for wiring in real endpoint checks once the app has real
handlers.

<div align="right"><a href="#-table-of-contents"><img src=".github/assets/back-to-top.svg" alt="Back to top" height="28"></a></div>

## <a id="documentation"></a>📖 Documentation

Full API reference for the backend, generated with Doxygen from
`docs/Doxyfile`:

**https://privatemwb.github.io/DnsLint/**

<div align="right"><a href="#-table-of-contents"><img src=".github/assets/back-to-top.svg" alt="Back to top" height="28"></a></div>

## <a id="deployment"></a>☁️ Deployment

**Backend:** builds via the repo's `Dockerfile` — multi-stage
`ubuntu:24.04` build, parameterized by `BINARY_NAME` (see the
comments in `Dockerfile`). `init-nested-submodules.sh` runs first
to clone any submodules-of-submodules a host's own automatic
submodule init doesn't recurse into.

```bash
docker build -t dnslint-backend .
docker run -p 8080:8080 -v dnslint-data:/app/data dnslint-backend
```

**`backend/data/` must be a mounted persistent volume** (as shown
above), not a path inside the container's own writable layer —
`HistoryStore` (see `backend/src/database/`) writes check history to
`data/dnslint.json` on every request, and without a real volume
mounted there, every redeploy silently wipes it.

**Frontend:** a static Vite build (`npm run build` → `frontend/dist/`),
deployable to any static host. Set `VITE_API_BASE_URL` to the
backend's deployed URL at build time or via the host's environment
variables.

<div align="right"><a href="#-table-of-contents"><img src=".github/assets/back-to-top.svg" alt="Back to top" height="28"></a></div>

## <a id="contributing"></a>🤝 Contributing

Issues and pull requests are welcome. Before submitting a PR:

- Run `clang-format`/`clang-tidy` on any changed backend files
- Run `npm run lint` and `npm run format:check` on any changed
  frontend files
- Make sure `docker build .` still succeeds if you touched the
  backend or `Dockerfile`

<div align="right"><a href="#-table-of-contents"><img src=".github/assets/back-to-top.svg" alt="Back to top" height="28"></a></div>

## <a id="changelog"></a>📝 Changelog

See the [Releases](https://github.com/privateMwb/DnsLint/releases)
page for version history and release notes.

## <a id="license"></a>📄 License

MIT — see [LICENSE](LICENSE) for details.

<p align="center">
  <sub>Built with C++23 &amp; React</sub>
</p>

<p align="center">
  <a href="#-table-of-contents"><img src=".github/assets/back-to-top.svg" alt="Back to top" height="28"></a>
</p>
