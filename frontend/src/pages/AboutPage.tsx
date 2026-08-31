import { checkCode, checkDescription, checkLabel } from "../lib/checkMeta";

const CHECK_NAMES = [
  "missing_aaaa",
  "missing_mx",
  "missing_ns",
  "spf_present",
  "dmarc_present",
  "dkim_present",
  "ttl_a",
  "ttl_mx",
];

export function AboutPage() {
  return (
    <div className="mx-auto max-w-2xl px-5 pb-20 pt-14 sm:pt-20">
      <p className="mb-3 font-mono text-xs font-semibold uppercase tracking-[0.16em] text-signal-orange">
        // how it works
      </p>
      <h1 className="mb-3 font-display text-3xl font-bold leading-tight text-paper sm:text-4xl">What DnsLint checks</h1>
      <p className="mb-10 max-w-lg text-sm leading-relaxed text-ash">
        Every check below runs a live DNS query against a public recursive resolver -- the same records any
        visitor's own DNS lookup would see -- and flags anything worth fixing.
      </p>

      <div className="flex flex-col gap-3">
        {CHECK_NAMES.map((name) => (
          <div key={name} className="rounded-xl border border-line bg-panel px-5 py-4">
            <div className="flex items-baseline gap-2">
              <span className="font-body text-sm font-semibold text-paper">{checkLabel(name)}</span>
              <span className="font-mono text-[11px] text-ash-dim">{checkCode(name)}</span>
            </div>
            <p className="mt-1 text-[13px] leading-relaxed text-ash">{checkDescription(name)}</p>
          </div>
        ))}
      </div>

      <div className="mt-10 rounded-xl border border-line bg-panel px-5 py-4">
        <p className="font-mono text-xs font-semibold uppercase tracking-wide text-signal-green">A note on DKIM</p>
        <p className="mt-2 text-[13px] leading-relaxed text-ash">
          DKIM records don't live at a fixed location -- the selector is chosen by whatever mail provider set it
          up, with no way to discover it from DNS alone. This tool checks a handful of common selectors and
          reports what it finds under those. A "not detected" result doesn't necessarily mean DKIM is missing,
          just that it isn't set up under a selector this tool recognizes.
        </p>
      </div>
    </div>
  );
}
