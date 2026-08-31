import { cn } from "../lib/cn";
import { checkCode, checkLabel } from "../lib/checkMeta";
import type { CheckResult } from "../lib/types";

const DOT_STYLES: Record<string, string> = {
  pass: "bg-signal-green shadow-[0_0_8px_var(--color-signal-green)]",
  warn: "bg-signal-orange shadow-[0_0_8px_var(--color-signal-orange)]",
  fail: "bg-signal-red shadow-[0_0_8px_var(--color-signal-red)]",
};

/**
 * A single check's row. Not expandable -- the backend now puts the
 * actual record value in `message` where one's decodable (AAAA
 * address, SPF/DMARC/DKIM TXT content), so it's already fully visible.
 * There's nothing left to reveal behind a click: a general explanation
 * of what each check means lives on the About page instead of
 * repeating it per-row here.
 */
export function ResultRow({ result }: { result: CheckResult }) {
  return (
    <div className="flex items-start gap-3.5 border-b border-line px-5 py-4 last:border-b-0">
      <span className={cn("mt-1.5 h-2.5 w-2.5 flex-shrink-0 rounded-sm", DOT_STYLES[result.status])} aria-hidden />
      <div className="min-w-0 flex-1">
        <div className="flex flex-wrap items-baseline gap-x-2">
          <span className="font-body text-sm font-semibold text-paper">{checkLabel(result.name)}</span>
          <span className="font-mono text-[11px] tracking-wide text-ash-dim">{checkCode(result.name)}</span>
        </div>
        <p className="mt-0.5 break-words font-mono text-[13px] leading-relaxed text-ash">{result.message}</p>
      </div>
    </div>
  );
}
