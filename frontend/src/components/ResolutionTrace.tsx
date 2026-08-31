import { useEffect, useState } from "react";
import { cn } from "../lib/cn";

interface ResolutionTraceProps {
  domain: string;
  /** True while a check is in flight -- hops light up in sequence, root to leaf. */
  active: boolean;
  className?: string;
}

/**
 * Splits a domain into its delegation chain, root first --
 * "mail.example.co.uk" -> [".", "uk", "co.uk", "example.co.uk",
 * "mail.example.co.uk"] -- the order a resolver actually walks down
 * through NS delegation to reach an answer. A simplified illustration
 * of that idea for the UI, not a literal packet trace.
 */
function buildHops(domain: string): string[] {
  const parts = domain.split(".").filter(Boolean);
  const hops: string[] = ["."];
  for (let i = parts.length - 1; i >= 0; i--) {
    hops.push(parts.slice(i).join("."));
  }
  return hops;
}

export function ResolutionTrace({ domain, active, className }: ResolutionTraceProps) {
  const hops = buildHops(domain || "example.com");
  const [litCount, setLitCount] = useState(active ? 0 : hops.length);

  useEffect(() => {
    if (!active) {
      setLitCount(hops.length);
      return;
    }

    setLitCount(0);
    const timers = hops.map((_, i) => setTimeout(() => setLitCount((n) => Math.max(n, i + 1)), i * 200));
    return () => timers.forEach(clearTimeout);
    // hops is derived from domain each render; re-running the hop-light
    // sequence keys off (active, domain) rather than the derived array
    // identity, which would re-trigger every render.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [active, domain]);

  return (
    <div className={cn("flex flex-wrap items-center gap-1.5 font-mono text-xs", className)}>
      {hops.map((hop, i) => (
        <div key={hop + i} className="flex items-center gap-1.5">
          <span
            className={cn(
              "rounded px-1.5 py-0.5 transition-colors duration-300",
              i < litCount ? "bg-signal-green/15 text-signal-green" : "bg-panel-2 text-ash-dim",
            )}
          >
            {hop}
          </span>
          {i < hops.length - 1 && (
            <span
              className={cn(
                "h-px w-3 transition-colors duration-300",
                i < litCount - 1 ? "bg-signal-green/40" : "bg-line",
              )}
              aria-hidden
            />
          )}
        </div>
      ))}
    </div>
  );
}
