#!/bin/bash
# scan_secrets.sh — release gate: refuse to ship when a secret is tracked in Git.
# Usage: ./tools/scan_secrets.sh        (exit 0 clean, exit 1 with the offenders)
#
# Why this exists: on 2026-08-16 a device backup carrying the real Wi-Fi
# password was found committed to the public repo, and after purging it the
# same password turned up in cleartext in three bench scripts. Both vectors
# are checked here, because removing a FILE is not the same as removing a
# SECRET.
#
# Known test-only credentials live in tools/.secretscan-allow, one per line.
# Add to it deliberately — every entry is a credential you are choosing to
# publish.

set -u
cd "$(dirname "$0")/.."

ALLOW="tools/.secretscan-allow"
fail=0

# 1. File types that must never be tracked, whatever they hold.
bad=$(git ls-files \
  | grep -iE '\.(bkp|pem|key|p12|pfx|jks)$|(^|/)system\.bin$|(^|/)id_(rsa|ed25519)$' \
  || true)
if [ -n "$bad" ]; then
  echo "SECRET GATE — these file types must not be tracked:"
  printf '%s\n' "$bad" | sed 's/^/    /'
  fail=1
fi

# 2. Private keys by content, whatever the file is called. The [E] keeps this
# pattern from matching the line that defines it once this file is tracked.
bad=$(git grep -I -l -e '-----BEGIN .*PRIVAT[E] KEY-----' -- . 2>/dev/null || true)
if [ -n "$bad" ]; then
  echo "SECRET GATE — private key material inside tracked files:"
  printf '%s\n' "$bad" | sed 's/^/    /'
  fail=1
fi

# 3. Literal credentials, minus the ones we knowingly publish.
# A literal is a value that starts with an alphanumeric: that skips the
# placeholders (<senha>) and the correct form (f'wifi pass {WIFI_PASS}',
# "$WIFI_PASS") without needing an entry in the allowlist for either.
hits=$(git grep -I -n -iE \
  "(pass|passwd|password|senha|secret|token|api_?key)[a-z_]*[[:space:]]*[:=][[:space:]]*['\"][A-Za-z0-9][^'\"]{5,}['\"]|wifi[[:space:]]+pass[[:space:]]+[A-Za-z0-9][A-Za-z0-9!@#%^&*._-]{5,}" \
  -- . 2>/dev/null || true)
if [ -n "$hits" ] && [ -s "$ALLOW" ]; then
  while IFS= read -r ok; do
    [ -z "$ok" ] && continue
    case "$ok" in \#*) continue ;; esac
    hits=$(printf '%s\n' "$hits" | grep -vF -- "$ok" || true)
  done < "$ALLOW"
fi
if [ -n "$hits" ]; then
  echo "SECRET GATE — literal credentials in tracked files:"
  printf '%s\n' "$hits" | sed 's/^/    /'
  echo "    (read from the environment instead, or allowlist in $ALLOW)"
  fail=1
fi

if [ "$fail" -ne 0 ]; then
  echo "SECRET GATE: FAILED"
  exit 1
fi
echo "SECRET GATE: clean"
