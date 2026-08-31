/** Mirrors DnsCheckup::CheckStatus's JSON form (see CheckRoutes.h's statusToString()). */
export type CheckStatus = "pass" | "warn" | "fail";

/** Mirrors DnsCheckup::CheckResult as serialized by CheckRoutes.h's toJson(). */
export interface CheckResult {
  name: string;
  status: CheckStatus;
  message: string;
}

/** The full body of POST /api/check's response. */
export interface CheckReport {
  domain: string;
  checks: CheckResult[];
}
