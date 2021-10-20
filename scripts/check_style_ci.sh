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
      "content": "**Branch:** $(git rev-parse --abbrev-ref HEAD)\n**Commit:** https://github.com/MafiaHub/Framework/commit/$(git rev-parse HEAD)\n**Issues:**\n$RESULT"
    }
EOF
  }

  PAYLOAD="$(gen_data)"
  echo "$PAYLOAD"
  curl \
    -H "Accept:application/json" \
    -H "Content-Type:application/json" \
    --data-binary "$PAYLOAD" \
    "$1"
  exit 1
fi
