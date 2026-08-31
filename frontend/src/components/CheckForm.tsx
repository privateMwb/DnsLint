import { useState, type FormEvent } from "react";

interface CheckFormProps {
  onSubmit: (domain: string) => void;
  pending: boolean;
}

export function CheckForm({ onSubmit, pending }: CheckFormProps) {
  const [value, setValue] = useState("");

  function handleSubmit(e: FormEvent) {
    e.preventDefault();
    const trimmed = value.trim();
    if (!trimmed || pending) return;
    onSubmit(trimmed);
  }

  return (
    <form onSubmit={handleSubmit} className="flex flex-col gap-3 sm:flex-row">
      <label className="flex flex-1 items-center gap-2 rounded-xl border border-line bg-panel px-4 py-3.5 transition-colors focus-within:border-signal-green/50 focus-within:ring-2 focus-within:ring-signal-green/15">
        <span className="font-mono text-sm text-signal-green" aria-hidden>
          $
        </span>
        <input
          type="text"
          value={value}
          onChange={(e) => setValue(e.target.value)}
          placeholder="example.com"
          spellCheck={false}
          autoCapitalize="off"
          autoCorrect="off"
          className="w-full bg-transparent font-mono text-sm text-paper placeholder:text-ash-dim focus:outline-none"
        />
      </label>
      <button
        type="submit"
        disabled={pending || !value.trim()}
        className="inline-flex items-center justify-center gap-2 rounded-xl bg-signal-orange px-6 py-3.5 font-body text-sm font-semibold text-ink transition-transform hover:brightness-110 active:scale-[0.98] disabled:cursor-not-allowed disabled:opacity-50"
      >
        {pending ? (
          <>
            <i className="bx bx-loader-alt animate-spin" aria-hidden />
            Checking…
          </>
        ) : (
          "Run check"
        )}
      </button>
    </form>
  );
}
