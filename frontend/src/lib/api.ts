import type { CheckReport } from "./types";

const API_BASE_URL: string = (import.meta.env.VITE_API_BASE_URL as string | undefined) ?? "http://localhost:8080";

export class ApiError extends Error {
  status?: number;

  constructor(message: string, status?: number) {
    super(message);
    this.name = "ApiError";
    this.status = status;
  }
}

/**
 * Calls POST /api/check (see backend/src/routes/CheckRoutes.h::postCheck).
 * Throws ApiError for anything that stops a report from rendering:
 * network failure, the rate limiter's 429, or any non-2xx response.
 */
export async function checkDomain(domain: string, signal?: AbortSignal): Promise<CheckReport> {
  let response: Response;

  try {
    response = await fetch(`${API_BASE_URL}/api/check`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ domain }),
      signal,
    });
  } catch {
    throw new ApiError("Could not reach the DnsLint backend. Is it running?");
  }

  if (response.status === 429) {
    throw new ApiError("Too many checks in a short time. Wait a moment and try again.", 429);
  }

  if (!response.ok) {
    const text = await response.text().catch(() => "");
    throw new ApiError(text || `Request failed (${response.status}).`, response.status);
  }

  return (await response.json()) as CheckReport;
}
