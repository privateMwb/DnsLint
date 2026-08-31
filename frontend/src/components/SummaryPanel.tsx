import { cn } from "../lib/cn";
import type { CheckReport, CheckStatus } from "../lib/types";
import { ResolutionTrace } from "./ResolutionTrace";
import { SignalBars } from "./SignalBars";

const PILL_STYLES: Record<CheckStatus, string> = {
  pass: "border-signal-green/25 bg-signal-green/10 text-signal-green",
  warn: "border-signal-orange/25 bg-signal-orange/10 text-signal-orange",
  fail: "border-signal-red/25 bg-signal-red/10 text-signal-red",
};

function CountPill({ status, count, label }: { status: CheckStatus; count: number; label: string }) {
  return (
    <span className={cn("rounded-full border px-2.5 py-0.5 font-mono text-[11px] font-semibold", PILL_STYLES[status])}>
      {count} {label}
    </span>
  );
}

export function SummaryPanel({ report, checkedAt }: { report: CheckReport; checkedAt: string }) {
  const counts = report.checks.reduce(
    (acc, c) => {
      acc[c.status] += 1;
      return acc;
    },
    { pass: 0, warn: 0, fail: 0 } as Record<CheckStatus, number>,
  );
  const total = report.checks.length || 1;
  const score = Math.round((counts.pass / total) * 100);

  return (
    <div className="rounded-2xl border border-line bg-panel px-6 py-5">
      <ResolutionTrace domain={report.domain} active={false} className="mb-4" />
      <div className="flex items-center gap-5">
        <SignalBars score={score} />
        <div className="min-w-0 flex-1">
          <div className="flex flex-wrap items-baseline gap-x-2">
            <span className="truncate font-mono text-lg font-semibold text-paper">{report.domain}</span>
            <span className="font-display text-2xl font-bold text-paper">{score}</span>
            <span className="font-mono text-[11px] text-ash-dim">/ 100</span>
          </div>
          <div className="mt-0.5 text-xs text-ash-dim">checked {checkedAt}</div>
          <div className="mt-3 flex flex-wrap gap-2">
            <CountPill status="pass" count={counts.pass} label="pass" />
            <CountPill status="warn" count={counts.warn} label="warn" />
            <CountPill status="fail" count={counts.fail} label="issue" />
          </div>
        </div>
      </div>
    </div>
  );
}
