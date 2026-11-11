// ============================================================
// scripts/capture-screenshots.mjs
//
// Takes the README screenshots of the DnsLint frontend with Playwright,
// on a phone-sized viewport:
//
//   dnslint.jpg               a finished check of a well-configured domain:
//                             the score, the resolution trace, and each result
//   dnslint-issues.png        a check of a domain with problems: failing and
//                             warning rows
//   dnslint-about.png         the About page
//   dnslint-backend-down.png  the backend unreachable: the "Check failed" card
//
// Run by .github/workflows/screenshots.yml, which installs Playwright next to
// this script and starts the backend and the frontend first. Config comes from
// the environment:
//
//   APP_URL        frontend to open                     (default http://localhost:4173)
//   GOOD_DOMAIN    domain for the main screenshot       (default youtube.com)
//   ISSUES_DOMAIN  domain for the issues screenshot     (default example.com)
//   OUT_DIR        where PNGs go                        (default ./docs/assets)
//
// Each check sends live DNS queries from the backend, so what the rows say is
// whatever those domains' DNS says on the day. Every run is also saved to the
// backend's check history; nothing is changed or deleted.
// ============================================================

import { chromium } from "playwright";
import { mkdir } from "node:fs/promises";
import path from "node:path";

const APP_URL = process.env.APP_URL ?? "http://localhost:4173";
const GOOD = process.env.GOOD_DOMAIN ?? "youtube.com";
const ISSUES = process.env.ISSUES_DOMAIN ?? "example.com";
const OUT = process.env.OUT_DIR ?? "docs/assets";

await mkdir(OUT, { recursive: true });

const browser = await chromium.launch();
const context = await browser.newContext({
  viewport: { width: 390, height: 844 },
  deviceScaleFactor: 2,
  colorScheme: "dark",
  reducedMotion: "reduce",
});
const page = await context.newPage();

// The type follows the extension: the README's main image is docs/assets/dnslint.jpg (it
// already is one), so capturing to that name replaces it without touching the README.
const shot = async (name) => {
  const file = path.join(OUT, name);
  const options = name.endsWith(".jpg") ? { type: "jpeg", quality: 92 } : { type: "png" };
  await page.screenshot({ path: file, fullPage: true, ...options });
  console.log("saved", file);
};

const domainInput = () => page.getByPlaceholder("example.com");
const runButton = () => page.getByRole("button", { name: "Run check" });

// Types a domain, runs the check, and waits for the "resolving" state to come and go.
const runCheck = async (domain) => {
  await domainInput().fill(domain);
  await runButton().click();
  await page.getByText("resolving…").waitFor({ timeout: 20_000 });
  // A real check sends several live DNS queries (and a sleeping free-tier backend adds a cold start).
  await page.getByText("resolving…").waitFor({ state: "detached", timeout: 150_000 });
  await page.waitForTimeout(800);
};

try {
  console.log("opening", APP_URL);
  await page.goto(APP_URL, { waitUntil: "domcontentloaded" });
  await domainInput().waitFor({ timeout: 120_000 });

  // 1. A finished check of a well-configured domain.
  await runCheck(GOOD);
  await page.getByText("Check failed").waitFor({ state: "detached", timeout: 2_000 }).catch(() => {});
  await shot("dnslint.jpg");

  // 2. A domain with problems.
  await runCheck(ISSUES);
  await shot("dnslint-issues.png");

  // 3. The About page.
  await page.getByRole("link", { name: "About" }).first().click();
  await page.waitForLoadState("domcontentloaded");
  await page.waitForTimeout(800);
  await shot("dnslint-about.png");

  // 4. Backend unreachable: refuse the check request and run one.
  await page.getByRole("link", { name: "Checker" }).first().click();
  await domainInput().waitFor({ timeout: 10_000 });
  await page.route("**/api/check", (route) => route.abort());
  await domainInput().fill(GOOD);
  await runButton().click();
  await page.getByText("Check failed").waitFor({ timeout: 20_000 });
  await page.waitForTimeout(500);
  await shot("dnslint-backend-down.png");
} catch (error) {
  // Keep a picture of whatever the page showed, to debug from.
  await page.screenshot({ path: path.join(OUT, "_debug-failure.png"), fullPage: true });
  console.error(error);
  process.exitCode = 1;
} finally {
  await browser.close();
}
