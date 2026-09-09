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

# Called with paths, it audits packaged archives instead of the index. The
# release scripts copy from the WORKING TREE, so an untracked secret sitting
# in tools/ rides into a zip that the git-based checks below never see.
if [ "$#" -gt 0 ]; then
  for z in "$@"; do
    [ -f "$z" ] || continue
    names=$(unzip -l "$z" 2>/dev/null \
      | grep -iE '\.(bkp|pem|key|p12|pfx|jks)$|/system\.bin$|/id_(rsa|ed25519)$' || true)
    if [ -n "$names" ]; then
      echo "SECRET GATE — sensitive file packaged in $(basename "$z"):"
      printf '%s\n' "$names" | sed 's/^/    /'
      fail=1
    fi
    if unzip -p "$z" 2>/dev/null | grep -qa -- '-----BEGIN .*PRIVAT[E] KEY-----'; then
      echo "SECRET GATE — private key material inside $(basename "$z")"
      fail=1
    fi
  done
  if [ "$fail" -ne 0 ]; then echo "SECRET GATE: FAILED"; exit 1; fi
  echo "SECRET GATE: archives clean"
  exit 0
fi

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

# 3b. Positional credentials. Step 3 only sees `name = "value"`, so a password
# handed over as an argument slipped past it for a year: setdefault('X_PASS',
# 'v'), ("admin", "v"), requests.get(..., auth=("admin", "v")). Finding V-02
# (2026-09-07) found the rig password in four bench scripts, and step 3 above
# reported the tree clean while three of the four were sitting in the index.
# Same allowlist as step 3 — a bench account we publish on purpose stays
# published, whichever side of the comma it is written on.
hits=$(git grep -I -n -E \
  "(PASS|PASSWD|PASSWORD|SENHA|SECRET|TOKEN|API_?KEY)[A-Za-z_]*['\"][[:space:]]*,[[:space:]]*['\"][A-Za-z0-9][^'\"]{5,}['\"]|\([[:space:]]*['\"]admin['\"][[:space:]]*,[[:space:]]*['\"][A-Za-z0-9][^'\"]{5,}['\"][[:space:]]*\)" \
  -- . 2>/dev/null || true)
if [ -n "$hits" ] && [ -s "$ALLOW" ]; then
  while IFS= read -r ok; do
    [ -z "$ok" ] && continue
    case "$ok" in \#*) continue ;; esac
    hits=$(printf '%s\n' "$hits" | grep -vF -- "$ok" || true)
  done < "$ALLOW"
fi
if [ -n "$hits" ]; then
  echo "SECRET GATE — positional credentials in tracked files:"
  printf '%s\n' "$hits" | sed 's/^/    /'
  echo "    (read from the environment instead, or allowlist in $ALLOW)"
  fail=1
fi

# 3c. The values themselves. Steps 1-3b recognise a SHAPE; this one recognises
# the actual secrets of this bench, which is what catches a password written
# where no pattern expects it — V-02's fourth hit was a bare test vector,
# sha256_frontend('<the rig password>'), matched by nothing above.
#
# The list lives OUTSIDE the repository, one value per line, because a file of
# real secrets is exactly what must never be committed. On CI it does not
# exist and this step is skipped by design: only the developer who owns the
# bench knows those values, and shipping them to a runner to protect them
# would be the same mistake in a new place.
#
#   printf '%s\n' '<rig password>' '<wifi psk>' '<home ssid>' > ~/.simut-secrets-deny
#   chmod 600 ~/.simut-secrets-deny
#
# Keep the retired values in it too: a rotated password is inert on the device
# but still names the bench in a public diff, and it is the value most likely
# to be pasted back by muscle memory.
DENY="${SIMUT_SECRET_DENYLIST:-$HOME/.simut-secrets-deny}"
if [ -f "$DENY" ]; then
  found=""
  while IFS= read -r v; do
    [ -z "$v" ] && continue
    case "$v" in \#*) continue ;; esac
    # Report path:line only. Echoing the match would print the secret into a
    # terminal, a CI log or a screenshot — the gate must not become the leak.
    where=$(git grep -I -n -F -- "$v" -- . 2>/dev/null | cut -d: -f1,2 || true)
    [ -n "$where" ] && found="${found}${where}"$'\n'
  done < "$DENY"
  if [ -n "$found" ]; then
    echo "SECRET GATE — a bench value from $DENY appears in these tracked lines:"
    printf '%s' "$found" | sed 's/^/    /'
    fail=1
  fi
fi

if [ "$fail" -ne 0 ]; then
  echo "SECRET GATE: FAILED"
  exit 1
fi
echo "SECRET GATE: clean"
