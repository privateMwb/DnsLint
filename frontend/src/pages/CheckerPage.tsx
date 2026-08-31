import { useState } from "react";
import { ApiError, checkDomain } from "../lib/api";
import type { CheckReport } from "../lib/types";
import { CheckForm } from "../components/CheckForm";
import { ResolutionTrace } from "../components/ResolutionTrace";
import { SummaryPanel } from "../components/SummaryPanel";
import { ResultRow } from "../components/ResultRow";

export function CheckerPage() {
  const [pendingDomain, setPendingDomain] = useState<string | null>(null);
  const [runId, setRunId] = useState(0);
  const [report, setReport] = useState<CheckReport | null>(null);
  const [checkedAt, setCheckedAt] = useState("");
  const [error, setError] = useState<string | null>(null);

  async function handleSubmit(domain: string) {
    setPendingDomain(domain);
    setRunId((n) => n + 1); // Forces ResolutionTrace to remount even on a repeat of the same domain -- see its key below.
    setError(null);

    try {
      const result = await checkDomain(domain);
      setReport(result);
      setCheckedAt(new Date().toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" }));
    } catch (err) {
      setReport(null);
      setError(err instanceof ApiError ? err.message : "Something went wrong. Try again.");
    } finally {
      setPendingDomain(null);
    }
  }

  return (
    <div className="mx-auto max-w-2xl px-5 pb-20 pt-14 sm:pt-20">
      <p className="mb-3 font-mono text-xs font-semibold uppercase tracking-[0.16em] text-signal-orange">
        // dns health checkup
      </p>
      <h1 className="mb-3 font-display text-3xl font-bold leading-tight text-paper sm:text-4xl">
        Check any domain's DNS setup
      </h1>
      <p className="mb-8 max-w-lg text-sm leading-relaxed text-ash">
        Looks up the records that matter for uptime and email deliverability, and flags anything worth fixing.
      </p>

      <CheckForm onSubmit={handleSubmit} pending={pendingDomain !== null} />

      {pendingDomain && (
        <div className="mt-6 rounded-2xl border border-line bg-panel px-6 py-5">
          <ResolutionTrace key={runId} domain={pendingDomain} active />
          <p className="mt-3 font-mono text-xs text-ash-dim">resolving…</p>
        </div>
      )}

      {error && !pendingDomain && (
        <div className="mt-6 rounded-2xl border border-signal-red/25 bg-signal-red/5 px-6 py-5">
          <p className="font-mono text-sm font-semibold text-signal-red">Check failed</p>
          <p className="mt-1 text-sm text-ash">{error}</p>
        </div>
      )}

      {report && !pendingDomain && (
        <div className="mt-6 flex flex-col gap-5">
          <SummaryPanel report={report} checkedAt={checkedAt} />
          <div className="overflow-hidden rounded-2xl border border-line bg-panel">
            {report.checks.map((c) => (
              <ResultRow key={c.name} result={c} />
            ))}
          </div>
        </div>
      )}

      {!report && !pendingDomain && !error && (
        <div className="mt-10 rounded-2xl border border-dashed border-line px-6 py-10 text-center">
          <i className="bx bx-network-chart mb-3 text-2xl text-ash-dim" aria-hidden />
          <p className="text-sm text-ash-dim">Enter a domain above to run its checkup.</p>
        </div>
      )}
    </div>
  );
}
