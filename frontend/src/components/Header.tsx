import { NavLink } from "react-router-dom";
import { cn } from "../lib/cn";

const NAV_LINKS = [
  { to: "/", label: "Checker" },
  { to: "/about", label: "About" },
];

export function Header() {
  return (
    <header className="border-b border-line">
      <div className="mx-auto flex max-w-2xl items-center justify-between px-5 py-4">
        <NavLink to="/" className="flex items-center gap-2 font-display text-base font-bold text-paper">
          <span className="h-2 w-2 rounded-full bg-signal-green shadow-[0_0_8px_var(--color-signal-green)]" aria-hidden />
          DnsLint
        </NavLink>
        <nav className="flex items-center gap-1 font-mono text-xs">
          {NAV_LINKS.map((link) => (
            <NavLink
              key={link.to}
              to={link.to}
              end
              className={({ isActive }) =>
                cn("rounded-md px-3 py-1.5 transition-colors", isActive ? "bg-panel-2 text-paper" : "text-ash-dim hover:text-ash")
              }
            >
              {link.label}
            </NavLink>
          ))}
        </nav>
      </div>
    </header>
  );
}
