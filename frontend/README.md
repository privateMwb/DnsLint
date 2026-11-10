# frontend

The DnsLint web app: React 19, Vite 8, TypeScript and Tailwind CSS 4, with `react-router-dom` for its two
pages (the checker and About). See the root [README](../README.md) for the whole project.

```bash
npm install
cp .env.example .env.local   # VITE_API_BASE_URL, if the backend isn't on http://localhost:8080
npm run dev                  # http://localhost:5173
```

| Script | What it does |
|---|---|
| `npm run dev` | Dev server with hot reload |
| `npm run build` | Type-check and production build into `dist/` |
| `npm run lint` | ESLint (CI runs this) |
| `npm run format:check` | Prettier check |

`VITE_API_BASE_URL` is read at build time, so changing it on a host such as Vercel needs a redeploy. For
deployment see [docs/DEPLOYMENT.md](../docs/DEPLOYMENT.md).

Colors and fonts are Tailwind theme tokens in `src/index.css`. Pass, warn and fail use LED-style green,
orange and red; the mint-to-ember gradient (`text-flow`) is decorative only.
