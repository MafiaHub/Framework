#!/usr/bin/env bash

script_path=$(cd -P -- "$(dirname -- "$0")" && pwd -P)
cd "$script_path/.." || exit 1

find code/framework/ -iname *.h -o -iname *.cpp -o -iname *.c -o -iname *.cc -o -iname *.hpp | xargs clang-format -i