import { cn } from "../lib/cn";

function overallColor(score: number): string {
  if (score >= 80) return "var(--color-signal-green)";
  if (score >= 50) return "var(--color-signal-orange)";
  return "var(--color-signal-red)";
}

/** Signal-strength-style bar chart for the overall pass score, echoing
 * a network signal indicator rather than a generic donut/ring chart. */
export function SignalBars({ score, className }: { score: number; className?: string }) {
  const filled = Math.round((score / 100) * 5);
  const color = overallColor(score);

  return (
    <div className={cn("flex items-end gap-1", className)} aria-hidden>
      {[1, 2, 3, 4, 5].map((bar) => (
        <span
          key={bar}
          className="w-1.5 rounded-sm transition-colors duration-500"
          style={{
            height: `${bar * 5 + 6}px`,
            backgroundColor: bar <= filled ? color : "var(--color-panel-2)",
          }}
        />
      ))}
    </div>
  );
}
