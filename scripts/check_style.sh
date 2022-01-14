#!/usr/bin/env bash
# Adapted from https://github.com/SerenityOS/serenity/blob/master/Meta/check-style.sh

script_path=$(cd -P -- "$(dirname -- "$0")" && pwd -P)
cd "$script_path/.." || exit 1

# Ensure copyright headers match this format and are followed by a blank line:
# /*
#  * MafiaHub OSS license
#  * Copyright (c) 2021(-yyyy) MafiaHub. All rights reserved.
#  * ... more of these ...
#  * 
#  * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
#  * See LICENSE file in the source repository for information regarding licensing.
#  */
GOOD_LICENSE_HEADER_PATTERN=$'^/\*\n \* MafiaHub OSS license\n( \* Copyright \(c\) [0-9]{4}(-[0-9]{4})?\, .*\n)+ \*\n \* This file comes from MafiaHub, hosted at https://github\.com/MafiaHub/Framework\.\n \* See LICENSE file in the source repository for information regarding licensing\.\n \*/(\n\n|$)'
BAD_LICENSE_HEADER_ERRORS=()
LICENSE_HEADER_CHECK_EXCLUDES=(code/framework/src/utils/hooks/*)

# We check that "#pragma once" is present
PRAGMA_ONCE_PATTERN='#pragma once'
MISSING_PRAGMA_ONCE_ERRORS=()

# We make sure that there's a blank line before and after pragma once
GOOD_PRAGMA_ONCE_PATTERN=$'(^|\n\n)#pragma once(\n\n|$)'
BAD_PRAGMA_ONCE_ERRORS=()

while IFS= read -r f; do
    file_content="$(cat "$f")"
    if [[ ! "${LICENSE_HEADER_CHECK_EXCLUDES[*]} " =~ $f ]]; then
        if [[ ! "$file_content" =~ $GOOD_LICENSE_HEADER_PATTERN ]]; then
            BAD_LICENSE_HEADER_ERRORS+=("$f\n")
        fi
    fi
    if [[ "$f" =~ \.h$ ]]; then
        if [[ ! "$file_content" =~ $PRAGMA_ONCE_PATTERN ]]; then
            MISSING_PRAGMA_ONCE_ERRORS+=("$f\n")
        elif [[ ! "$file_content" =~ $GOOD_PRAGMA_ONCE_PATTERN ]]; then
            BAD_PRAGMA_ONCE_ERRORS+=("$f\n")
        fi
    fi
done < <(git ls-files -- \
    '*.cpp' \
    '*.h' \
    '*.hpp' \
	':!:vendors' \
	':!:code/tests/unit.h' \
	':!:code/framework/utils/hooks' \
)

exit_status=0
if (( ${#BAD_LICENSE_HEADER_ERRORS[@]} )); then
    echo -e "Files with missing or incorrect license header: ${BAD_LICENSE_HEADER_ERRORS[*]}"
    exit_status=1
fi
if (( ${#MISSING_PRAGMA_ONCE_ERRORS[@]} )); then
    echo -e "Header files missing #pragma once: ${MISSING_PRAGMA_ONCE_ERRORS[*]}"
    exit_status=1
fi
if (( ${#BAD_PRAGMA_ONCE_ERRORS[@]} )); then
    echo -e "#pragma once should have a blank line before and after in these files: ${BAD_PRAGMA_ONCE_ERRORS[*]}"
    exit_status=1
fi
exit "$exit_status"
