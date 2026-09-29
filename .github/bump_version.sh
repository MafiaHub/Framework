#!/bin/bash
set -e

# Find the last [skip ci] commit hash
LAST_SKIP_CI=$(git log --grep="\[skip ci\]" -n 1 --pretty=format:"%H")

if [[ -z "$LAST_SKIP_CI" ]]; then
    echo "No previous [skip ci] commit found. Using HEAD^ as reference."
    REFERENCE_COMMIT="HEAD^"
else
    echo "Found last [skip ci] commit: $LAST_SKIP_CI"
    REFERENCE_COMMIT="$LAST_SKIP_CI"
fi

# A manually versioned release may not have an automatic [skip ci] commit.
# Once tagged, use that release as the baseline for subsequent changes.
RELEASE_TAG=$(git describe --tags --match 'v[0-9]*' --abbrev=0 HEAD 2>/dev/null || true)
if [[ -n "$RELEASE_TAG" ]] && git merge-base --is-ancestor "$REFERENCE_COMMIT" "$RELEASE_TAG"; then
    REFERENCE_COMMIT="$RELEASE_TAG"
fi

# Get the list of changed files between the last [skip ci] commit and HEAD
mapfile -t changed_files < <(git diff --name-only "$REFERENCE_COMMIT" HEAD)
echo "Changed files: ${changed_files[@]}"

# Check if there are any changed files
if [ ${#changed_files[@]} -eq 0 ]; then
    echo "No files have been changed. Skipping version bump."
    exit 0
fi

# Default bump type is patch
BUMP_TYPE="patch"

# Define arrays for directories that trigger a major or minor bump.
# Matched as path prefixes against `git diff --name-only`, so a directory entry
# covers everything beneath it and a file entry matches that file exactly.
major_paths=(
    # Replaces the long-gone code/framework/src/networking/messages: replication
    # is where the sync flow lives now.
    "code/framework/src/networking/replication"
    "code/framework/src/networking/rpc"
    # MafiaNet is fetched, not vendored, so its headers are not in this tree and a
    # wire break cannot be detected by path. The pin is the proxy: message ids are
    # positional, so moving MafiaNet can shift every id and break every peer built
    # against the old header.
    "cmake/MafiaNetPin.cmake"
)
minor_paths=(
    "code/framework/src/scripting/builtins"
    "code/framework/src/integrations/server/scripting/builtins"
)

# Check for major bump directories
for file in "${changed_files[@]}"; do
    for major in "${major_paths[@]}"; do
        # Exact file match, or anything genuinely beneath a directory entry. A bare
        # "$major"* prefix would also fire on a sibling that merely starts with the
        # same characters -- cmake/MafiaNetPin.cmake.bak, or .../replication2/foo.
        if [[ "$file" == "$major" || "$file" == "$major/"* ]]; then
            BUMP_TYPE="major"
            break 2
        fi
    done
done

# If no major changes were found, check for minor bump directories
if [[ "$BUMP_TYPE" != "major" ]]; then
    for file in "${changed_files[@]}"; do
        for minor in "${minor_paths[@]}"; do
            if [[ "$file" == "$minor" || "$file" == "$minor/"* ]]; then
                BUMP_TYPE="minor"
                break 2
            fi
        done
    done
fi

echo "Determined bump type: $BUMP_TYPE"

# Ensure VERSION file exists
if [ ! -f VERSION ]; then
    echo "VERSION file not found. Creating VERSION with default value 0.0.0."
    echo "0.0.0" > VERSION
fi

# Read the current version from the VERSION file
CURRENT_VERSION=$(cat VERSION)
echo "Current version: $CURRENT_VERSION"

# Compute the required bump from the baseline, not from a version already
# raised by the PR. Otherwise an explicit protocol major gets bumped twice.
BASE_VERSION=$(git show "${REFERENCE_COMMIT}:VERSION" 2>/dev/null || printf '%s' "$CURRENT_VERSION")
IFS='.' read -r MAJOR MINOR PATCH <<< "$BASE_VERSION"

# Calculate the new version based on the bump type
case "$BUMP_TYPE" in
    major)
        NEW_MAJOR=$((MAJOR + 1))
        NEW_VERSION="$NEW_MAJOR.0.0"
        ;;
    minor)
        NEW_MINOR=$((MINOR + 1))
        NEW_VERSION="$MAJOR.$NEW_MINOR.0"
        ;;
    patch)
        NEW_PATCH=$((PATCH + 1))
        NEW_VERSION="$MAJOR.$MINOR.$NEW_PATCH"
        ;;
esac

IFS='.' read -r CURRENT_MAJOR CURRENT_MINOR CURRENT_PATCH <<< "$CURRENT_VERSION"
IFS='.' read -r NEW_MAJOR NEW_MINOR NEW_PATCH <<< "$NEW_VERSION"
if (( CURRENT_MAJOR > NEW_MAJOR ||
      (CURRENT_MAJOR == NEW_MAJOR && CURRENT_MINOR > NEW_MINOR) ||
      (CURRENT_MAJOR == NEW_MAJOR && CURRENT_MINOR == NEW_MINOR && CURRENT_PATCH >= NEW_PATCH) )); then
    echo "Keeping explicit version: $CURRENT_VERSION (required: $NEW_VERSION)"
    exit 0
fi

# Update the VERSION file with the new version
echo "$NEW_VERSION" > VERSION
echo "Updated version set to: $NEW_VERSION"
