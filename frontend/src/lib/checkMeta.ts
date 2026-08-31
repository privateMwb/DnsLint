/**
 * Human-facing label, short DNS-record code, and plain-language
 * explanation for each backend check `name` (see backend/src/checks/*.h
 * -- "missing_aaaa", "spf_present", etc. are the literal
 * CheckResult::name values the API returns).
 *
 * `description` is real explanatory copy, not fabricated record data --
 * the API response only carries {name, status, message}, no raw DNS
 * record content, so the expandable row in ResultRow.tsx shows this
 * instead of inventing a fake "dig" output.
 *
 * Anything not in this table still renders -- every lookup falls back
 * to the raw name -- so a new backend check shows up immediately
 * without a frontend deploy, just without friendly copy yet.
 */
interface CheckMeta {
  label: string;
  code: string;
  description: string;
}

const CHECK_META: Record<string, CheckMeta> = {
  missing_aaaa: {
    label: "IPv6 address",
    code: "AAAA",
    description: "An AAAA record lets visitors reach your site over IPv6. Most networks and providers support it today, and some connections are IPv6-only.",
  },
  missing_mx: {
    label: "Mail server",
    code: "MX",
    description: "MX records tell other mail servers where to deliver email for your domain. Without one, mail addressed to this domain can't be delivered.",
  },
  missing_ns: {
    label: "Nameservers",
    code: "NS",
    description: "NS records say which servers are authoritative for this domain. Every domain needs at least one to be reachable at all.",
  },
  spf_present: {
    label: "Sender policy",
    code: "SPF",
    description: "An SPF record lists which mail servers are allowed to send email as this domain. It helps receiving servers reject spoofed mail.",
  },
  dmarc_present: {
    label: "Email policy",
    code: "DMARC",
    description: "A DMARC record tells receiving mail servers what to do with messages that fail SPF or DKIM -- reject, quarantine, or allow them through.",
  },
  dkim_present: {
    label: "Email signing",
    code: "DKIM",
    description: "DKIM signs outgoing mail so receivers can verify it wasn't altered in transit. It's checked here only under a few common selectors -- see the message for details.",
  },
  ttl_a: {
    label: "A record TTL",
    code: "TTL",
    description: "TTL controls how long resolvers cache this record before re-checking it. Too low adds needless query load; too high slows down future changes.",
  },
  ttl_mx: {
    label: "MX record TTL",
    code: "TTL",
    description: "TTL controls how long resolvers cache this record before re-checking it. Too low adds needless query load; too high slows down future changes.",
  },
};

export function checkLabel(name: string): string {
  return CHECK_META[name]?.label ?? name;
}

export function checkCode(name: string): string {
  return CHECK_META[name]?.code ?? name.toUpperCase();
}

export function checkDescription(name: string): string | undefined {
  return CHECK_META[name]?.description;
}
