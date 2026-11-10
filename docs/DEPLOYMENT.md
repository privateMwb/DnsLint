# Deployment

DnsLint deploys in two independent pieces: the **frontend** on Vercel and the **backend** on
Render. They are different kinds of workload, so one platform can't host both.

## What gets deployed

| Piece | Where | What it is |
|---|---|---|
| Frontend | Vercel | The static Vite build of `frontend/` |
| Backend | Render web service | The repo's `Dockerfile`: `dnslint_backend` on port 8080 |

**Why not Vercel for the backend.** `backend/` is a long-running process that binds a persistent TCP
port, sends live UDP DNS queries, and writes `data/dnslint.json` to disk. Vercel runs short-lived
serverless functions with no persistent port and no durable filesystem.

## 1. Check the image locally (optional, needs Docker)

```bash
docker build -t dnslint .
docker run --rm -p 8080:8080 -v dnslint-data:/app/data dnslint
./scripts/check-demo.sh http://localhost:8080
```

## 2. Render (backend)

The repo's `render.yaml` is a Blueprint describing the service, so you can either use it
(**New → Blueprint**, pick the repo) or set it up by hand:

1. Push the repo to GitHub.
2. In Render: **New → Web Service**, pick the repo, runtime **Docker** (it uses `./Dockerfile`).
3. Set the health check path to `/api/health`.
4. Wait for the first build (a few minutes; it compiles the C++ backend).
5. Copy the service URL and open `<url>/api/health`: it should return `ok`.
6. Check it end to end from outside (it runs two real checks, saved to that backend's history):

   ```bash
   scripts/check-demo.sh https://<your-backend>.onrender.com https://<your-frontend>.vercel.app
   ```

   The *Demo image* workflow runs the same script against the Docker image on every change to the
   backend, the Dockerfile or `scripts/`.

**Nested submodules.** Render clones this repo's submodules but doesn't recurse into theirs.
`init-nested-submodules.sh` walks the tree for every `.gitmodules` file and clones anything still
empty. It works without `.git` metadata, so it runs inside the Docker build before CMake configures.

**The image.** A multi-stage build: an `ubuntu:24.04` stage with the toolchain compiles
`dnslint_backend` in Release mode (the unit tests and the submodules' tests, benchmarks and examples
switched off), and the final stage is a clean `ubuntu:24.04` with only `libstdc++6` and the binary.

## 3. Vercel (frontend)

1. **Add New → Project**, import the repo.
2. **Root Directory:** `frontend`. Framework preset: Vite.
3. Environment variable:

   ```
   VITE_API_BASE_URL = https://<your-render-url>
   ```

   No trailing slash. It is read at build time, so changing it needs a redeploy. For local
   development, copy `frontend/.env.example` to `frontend/.env.local`.
4. Deploy.

`frontend/vercel.json` rewrites every path to `index.html` (so `/about` works on a reload) and adds
`nosniff`, `Referrer-Policy` and `X-Frame-Options: DENY` headers.

## Behavior to expect

- **Check history is lost on redeploy unless `/app/data` is a persistent volume.** `data/` is created
  inside the image layer, so by default it is not one. On Render's free plan every redeploy and every
  spin-down after 15 minutes of inactivity rebuilds the container and empties the history. Attaching a
  Render persistent disk mounted at `/app/data` fixes this, at the paid tier's cost. Everything else
  (running checks) works without it.
- **Free plan sleep.** The first request after a quiet spell takes about a minute.
- **Outbound UDP.** Each check sends live queries to `8.8.8.8:53`. A host that blocks that makes every
  check report `warn` ("query timed out") rather than fail.
- **Rate limiting behind a proxy.** The limiter reads the client from `X-Forwarded-For`. Render's proxy
  sets it, so each visitor gets their own 60-requests-per-minute bucket. A caller who can reach the
  container without the proxy could spoof it.
- **CORS.** The `Cors` middleware is constructed with no argument, so it allows every origin (`*`).

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Build fails with an empty `external/` directory | Nested submodules weren't cloned; check the output of `init-nested-submodules.sh` in the build log |
| Frontend loads but says it can't reach the backend | `VITE_API_BASE_URL` is wrong, has a trailing slash, or the frontend wasn't redeployed after changing it |
| Every check row says "timed out" | The backend can't reach `8.8.8.8:53` over UDP |
| History is empty after a deploy | `/app/data` isn't a persistent volume; see above |
| `429 Too Many Requests` for everyone at once | The proxy isn't sending `X-Forwarded-For`, so all callers share one bucket |
| A reload of `/about` gives a 404 | `frontend/vercel.json` is missing from the deployed root directory |

## Updating

Merging to `main` redeploys both: Render rebuilds the image and Vercel rebuilds the frontend.
