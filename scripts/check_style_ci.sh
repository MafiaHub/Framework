#!/usr/bin/env bash

chmod +x scripts/check_style.sh
RESULT=$(scripts/check_style.sh)
CODE=$?

if [ $CODE -ne 0 ]; then
  gen_data()
  {
    cat <<EOF
{
  "username": "Framework Code Linter",
  "content": "**Branch:** $(git rev-parse --abbrev-ref HEAD)\\n**Commit:** https://github.com/MafiaHub/Framework/commit/$(git rev-parse HEAD)\\n**Issues:**\\n$RESULT"
}
EOF
  }

  PAYLOAD="$(gen_data)"
  echo "$PAYLOAD"
  # The webhook URL is unavailable on fork PRs (secrets are not exposed); only
  # notify Discord when it is actually set, but always fail the job.
  if [ -n "$1" ]; then
    curl \
      -H "Accept: application/json" \
      -H "Content-Type: application/json" \
      --data-binary "$PAYLOAD" \
      "$1"
  fi
  exit 1
fi
