#!/usr/bin/env bash
# Checks a running DnsLint backend from the outside, the way the frontend uses it: the service
# answers, a real check run returns every check in a valid shape, a pasted URL is reduced to its
# host, bad input is refused, the run is saved to the history (newest first), and CORS is on.
# It runs two real checks (against example.com), which are saved to that backend's history.
#
#   scripts/check-demo.sh <backend-url> [frontend-origin]
#
#   scripts/check-demo.sh https://dnslint.onrender.com https://dns-lint.vercel.app
#   scripts/check-demo.sh http://localhost:8080 http://localhost:5173
#
# With a frontend origin it also checks that the CORS header lets that origin through.
# WAIT_SECONDS (default 120) is how long to wait for a cold start (a sleeping free-tier
# service takes a while). Needs curl. Exits non-zero if any check fails.
#
# A check run sends live DNS queries (UDP to the resolver). If the backend cannot reach its
# resolver the individual checks report "warn", which is still a valid answer, so this script
# validates the shape of the response, not what DNS says about example.com.
#
# Stays well under the server's rate limit (60 requests a minute per client).

set -uo pipefail

BASE="${1:?usage: check-demo.sh <backend-url> [frontend-origin]}"
BASE="${BASE%/}"
ORIGIN="${2:-}"
ORIGIN="${ORIGIN%/}"
WAIT="${WAIT_SECONDS:-120}"

passed=0
failed=0
ok()  { echo "PASS  $1"; passed=$((passed + 1)); }
bad() { echo "FAIL  $1${2:+  ($2)}"; failed=$((failed + 1)); }
check() { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1" "got '$2', expected '$3'"; fi; }
http() { curl -s -m 10 -o /dev/null -w '%{http_code}' "$@" 2>/dev/null || echo 000; }
# A check run does several live DNS queries, so it gets a long timeout.
run_check() { curl -s -m 120 -X POST "$BASE/api/check" -H 'Content-Type: application/json' -d "$1"; }
squash() { tr -d ' \n\r'; }

# 1. The service answers (waiting out a cold start).
up=0
end=$((SECONDS + WAIT))
while [ "$SECONDS" -lt "$end" ]; do
    [ "$(http "$BASE/api/health")" = "200" ] && { up=1; break; }
    sleep 3
done
if [ "$up" != 1 ]; then
    bad "service answers /api/health within ${WAIT}s" "is the URL right, and is the service running?"
    echo; echo "RESULT: $passed passed, $failed failed"; exit 1
fi
ok "service answers /api/health"

# 2. A real check run returns every check, in a valid shape.
body="$(run_check '{"domain": "example.com"}')"
flat="$(printf '%s' "$body" | squash)"
case "$flat" in
    *'"domain":"example.com"'*) ok "check response names the domain" ;;
    *) bad "check response names the domain" ;;
esac
for name in missing_aaaa missing_mx missing_ns spf_present dmarc_present dkim_present ttl_a ttl_mx; do
    case "$flat" in
        *"\"name\":\"$name\""*) ok "check \"$name\" is in the response" ;;
        *) bad "check \"$name\" is in the response" ;;
    esac
done
statuses="$(printf '%s' "$flat" | grep -o '"status":"[a-z]*"' | sort -u | tr '\n' ' ')"
case "$statuses" in
    *'"status":"pass"'*|*'"status":"warn"'*|*'"status":"fail"'*) ok "statuses are pass, warn or fail ($statuses)" ;;
    *) bad "statuses are pass, warn or fail" "got: $statuses" ;;
esac
other="$(printf '%s' "$flat" | grep -o '"status":"[a-z]*"' | grep -vE '"status":"(pass|warn|fail)"' | head -1)"
[ -z "$other" ] && ok "no unknown status value" || bad "no unknown status value" "$other"

# 3. A pasted URL is reduced to its host before it is checked.
body2="$(run_check '{"domain": "https://WWW.Example.com/some/path?x=1"}' | squash)"
case "$body2" in
    *'"domain":"www.example.com"'*) ok "a pasted URL is reduced to the host" ;;
    *) bad "a pasted URL is reduced to the host" "got: ${body2:0:120}" ;;
esac

# 4. Input checks.
check "malformed JSON is refused" "$(http -X POST "$BASE/api/check" -H 'Content-Type: application/json' -d '{not json')" "400"
check "missing domain field is refused" "$(http -X POST "$BASE/api/check" -H 'Content-Type: application/json' -d '{"foo": 1}')" "400"
check "domain without a dot is refused" "$(http -X POST "$BASE/api/check" -H 'Content-Type: application/json' -d '{"domain": "localhost"}')" "400"
check "history without a domain is refused" "$(http "$BASE/api/history")" "400"

# 5. The runs were saved, newest first.
hist="$(curl -s -m 10 "$BASE/api/history?domain=example.com" | squash)"
case "$hist" in
    *'"runs":[{'*) ok "history holds the saved runs" ;;
    *) bad "history holds the saved runs" "got: ${hist:0:120}" ;;
esac
stamps="$(printf '%s' "$hist" | grep -o '"checkedAt":[0-9.]*' | sed 's/.*://' | head -5)"
sorted="$(printf '%s\n' "$stamps" | sort -rn)"
if [ -n "$stamps" ] && [ "$stamps" = "$sorted" ]; then ok "history is newest first"; else bad "history is newest first" "got: $(echo $stamps)"; fi
case "$(curl -s -m 10 "$BASE/api/history?domain=https://Example.com/x" | squash)" in
    *'"domain":"example.com"'*) ok "history lookup normalizes a pasted URL" ;;
    *) bad "history lookup normalizes a pasted URL" ;;
esac

# 6. CORS.
allow="$(curl -s -m 10 -o /dev/null -D - -H "Origin: ${ORIGIN:-https://frontend.example}" "$BASE/api/health" \
    | tr -d '\r' | sed -n 's/^[Aa]ccess-[Cc]ontrol-[Aa]llow-[Oo]rigin: //p')"
if [ "$allow" = "*" ] || [ "$allow" = "${ORIGIN:-https://frontend.example}" ]; then
    ok "CORS lets ${ORIGIN:-a browser origin} through"
else
    bad "CORS lets ${ORIGIN:-a browser origin} through" "Access-Control-Allow-Origin was '$allow'"
fi

echo
echo "RESULT: $passed passed, $failed failed"
[ "$failed" -eq 0 ]
