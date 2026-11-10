# Demo

A short tour of DnsLint, from a first check to reading it back from the history.

## Run it locally

```bash
# Terminal 1: the backend (port 8080)
git clone --recurse-submodules https://github.com/privateMwb/DnsLint.git && cd DnsLint
cd backend && cmake -B build && cmake --build build && mkdir -p data && ./build/dnslint_backend

# Terminal 2: the frontend (http://localhost:5173)
cd frontend && npm install && cp .env.example .env.local && npm run dev
```

The checks send real DNS queries from the backend to `8.8.8.8`, so it needs outbound UDP on port 53.

## What to look at

1. **Run a check.** Enter `youtube.com` and press *Run check*. A resolution trace plays while the
   queries run, then the score and one row per check appear. Each row shows the *decoded* value: the
   IPv6 address, the mail server, the SPF record text.
2. **Paste a URL.** Enter `https://WWW.Example.com/some/path?x=1`. The result is for
   `www.example.com`: the scheme, path and query are stripped and the name is lowercased. MX, NS
   and DMARC are still looked up on the apex, `example.com`.
3. **Try bad input.** `localhost` (no dot) and an empty form are refused. Directly:

   ```bash
   curl -i -X POST localhost:8080/api/check -H "Content-Type: application/json" \
     -d '{"domain": "localhost"}'                    # 400 Bad Request
   ```
4. **Read the history back.** Every run is saved:

   ```bash
   curl "localhost:8080/api/history?domain=youtube.com"   # newest first, up to 20 runs
   ```
5. **Hit the rate limit.** Without an `X-Forwarded-For` header every caller shares one bucket of
   60 requests per minute:

   ```bash
   for i in $(seq 1 70); do curl -s -o /dev/null -w "%{http_code} " localhost:8080/api/health; done
   ```

   The last few answers are `429`, and the frontend shows "Too many checks in a short time".
6. **Stop the backend** and run another check: the page shows a *Check failed* card saying it
   could not reach the backend.
7. **Check the whole path.** `./scripts/check-demo.sh http://localhost:8080` does the same checks
   from outside, the way a deployed backend is checked.

## The hosted demo

[dns-lint.vercel.app](https://dns-lint.vercel.app) is the frontend on Vercel, talking to a backend on
Render. See [DEPLOYMENT.md](DEPLOYMENT.md) for how that is set up and what to expect from the free plan.
