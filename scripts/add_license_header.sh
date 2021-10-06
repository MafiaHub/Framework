#!/usr/bin/env bash

script_path=$(cd -P -- "$(dirname -- "$0")" && pwd -P)
cd "$script_path/.." || exit 1

for i in $(find code/framework -type f \( -iname \*.cpp -o -iname \*.h -o -iname \*.hpp -o -iname \*.c -o -iname *.cc \));
do
  if ! grep -q " * MafiaHub OSS" "$i"
  then
    cat NOTICE.txt <(echo) "$i" >"$i".new && mv "$i".new "$i"
  fi
done
