# Contributing to DnsLint

Issues and pull requests are welcome. This guide covers how to set up, what CI checks, and how a
change gets merged.

## Setting up

```bash
git clone --recurse-submodules https://github.com/privateMwb/DnsLint.git
cd DnsLint

cd backend
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
mkdir -p data              # the check history is written here
./build/dnslint_backend    # http://localhost:8080

cd ../frontend
npm install
cp .env.example .env.local # VITE_API_BASE_URL, if the backend isn't on localhost:8080
npm run dev                # http://localhost:5173
```

FalconHTTP, DnsResolver, MiniDatabase and RateThrottle are git submodules under `backend/external/`,
and several carry nested submodules of their own, so clone with `--recurse-submodules` (or run
`git submodule update --init --recursive`). Requirements: a C++23 compiler (GCC or Clang, Linux),
CMake 3.20+, and Node.js 22+ (Vite 8 needs 20.19 or newer).

## The workflow

1. Create a branch from `main`.
2. Make your change.
3. Run the checks below.
4. Open a pull request. CI runs on it; merge once everything passes.

## Checks to run locally

CI enforces all of these:

```bash
# Backend: formatting (backend/include and backend/src only; external/ is upstream code)
find backend/include backend/src \( -name "*.h" -o -name "*.cpp" \) -exec clang-format --dry-run --Werror {} +

# Backend: static analysis
clang-tidy -p backend/build $(find backend/include backend/src -name "*.cpp")

# Backend: unit tests
cmake --build backend/build --target dnslint_tests && ./backend/build/dnslint_tests

# Backend: end-to-end check against a running backend (runs two real checks)
./scripts/check-demo.sh http://localhost:8080

# Frontend
cd frontend && npm run lint && npm run format:check && npm run build
```

## What to test

`backend/tests/` holds the unit tests (`dnslint_tests`), which CI runs after every build and again under
the sanitizers. Each file in `backend/tests/suite/unit/` is one suite and registers itself, so a new
`.cpp` file there is picked up with no build-file edit.

- **Anything that asks DNS a question** (`QueryEngine`, the checks): test it against the in-process mock
  server in `tests/support/mockDns.h`. It answers over a real UDP socket on `127.0.0.1`, so the test
  covers the real parsing path and never touches the network.
- **`HistoryStore`**: use a `TempDir` and a real file; see `historyStore.cpp`.
- **Threading changes** (the engine is shared across the server's worker threads): add a test that
  hammers it from several threads, as `concurrent_queries_get_their_own_answers` does. The TSan job
  in `sanitizers.yml` runs the suites and the real server concurrently.
- **New code that parses or validates text or bytes** (domain input, DNS responses): add a harness in
  `backend/fuzz/`. Any `fuzz_<name>.cpp` file there is built automatically; put a few seed inputs in
  `backend/fuzz/corpus/fuzz_<name>/`.
- **API changes** (`CheckRoutes.h`): update `scripts/check-demo.sh` and the frontend's `lib/api.ts` and
  `lib/types.ts` together.

## Style

- C++ follows `.clang-format` and the naming rules in `.clang-tidy`. clang-format output differs a
  little between versions, so prefer a construct that formats the same everywhere over fighting the
  formatter (for example `timeval tv{};`, not `struct timeval tv {};`).
- The frontend uses ESLint and Prettier (`npm run lint`, `npm run format:check`).
- Comments explain why, not what.
- Keep the README, `CHANGELOG.md` and `docs/` in step with behavior changes. Add an entry under
  `[Unreleased]` in the changelog.

## Reporting bugs and vulnerabilities

For bugs, open an issue with the steps to reproduce, the commit you tested, and what you expected. **Do
not** report suspected vulnerabilities in public; see [SECURITY.md](SECURITY.md).
