#!/usr/bin/env bash

script_path=$(cd -P -- "$(dirname -- "$0")" && pwd -P)
cd "$script_path/.." || exit 1

while IFS= read -r -d '' i; do
  if ! grep -Eq '^[[:space:]]*\* MafiaHub (source-available|OSS) license$' "$i"
  then
    # NOTICE.txt also contains a plain-text form. Only prepend the C/C++ comment.
    { sed -n '/^\/\*/,/^ \*\//p' NOTICE.txt; printf '\n'; cat "$i"; } >"$i".new && mv "$i".new "$i"
  fi
done < <(find code/framework -type f \( -iname '*.cpp' -o -iname '*.h' -o -iname '*.hpp' -o -iname '*.c' -o -iname '*.cc' \) -print0)
