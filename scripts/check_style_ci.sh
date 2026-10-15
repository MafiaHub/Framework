#!/usr/bin/env bash

RESULT=$(bash scripts/check_style.sh)
CODE=$?

if [ "$CODE" -ne 0 ]; then
  PAYLOAD=$(python3 - "$(git rev-parse --abbrev-ref HEAD)" "$(git rev-parse HEAD)" "$RESULT" <<'PY'
import json
import sys

branch, commit, issues = sys.argv[1:]
print(json.dumps({
    "username": "Framework Code Linter",
    "content": f"**Branch:** {branch}\n**Commit:** https://github.com/MafiaHub/Framework/commit/{commit}\n**Issues:**\n{issues}",
}))
PY
  )
  echo "$PAYLOAD"
  # The webhook URL is unavailable on fork PRs (secrets are not exposed); only
  # notify Discord when it is actually set, but always fail the job.
  if [ -n "${1:-}" ]; then
    curl \
      -H "Accept: application/json" \
      -H "Content-Type: application/json" \
      --data-binary "$PAYLOAD" \
      "$1"
  fi
  exit 1
fi
