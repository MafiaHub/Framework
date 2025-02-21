#!/bin/bash
set -e

# Get the PR title from the first argument.
PR_TITLE="$1"
if [[ -z "$PR_TITLE" ]]; then
  echo "Error: PR title must be passed as the first argument."
  exit 1
fi

echo "PR Title: $PR_TITLE"

# Get the list of changed files between the merge commit and its first parent.
mapfile -t changed_files < <(git diff --name-only HEAD^ HEAD)
echo "Changed files: ${changed_files[@]}"

# Default bump type is patch.
BUMP_TYPE="patch"

# Define arrays for directories that trigger a major or minor bump.
major_paths=(
    "code/framework/src/networking/messages"
    "code/framework/src/networking/rpc"
)
minor_paths=(
    "code/framework/src/scripting"
    "code/framework/src/integrations/server/scripting/builtins"
)

pr_title_lower=$(echo "$PR_TITLE" | tr '[:upper:]' '[:lower:]')

# Check if PR title enforces a PATCH bump.
if [[ "$pr_title_lower" == *"[patch]"* ]]; then
  echo "PR title contains [patch]. Enforcing PATCH bump."
  BUMP_TYPE="patch"
else
  # Check for major bump directories.
  for file in "${changed_files[@]}"; do
    for major in "${major_paths[@]}"; do
      if [[ "$file" == "$major"* ]]; then
        BUMP_TYPE="major"
        break 2
      fi
    done
  done

  # If no major changes were found, check for minor bump directories.
  if [[ "$BUMP_TYPE" != "major" ]]; then
    for file in "${changed_files[@]}"; do
      for minor in "${minor_paths[@]}"; do
        if [[ "$file" == "$minor"* ]]; then
          BUMP_TYPE="minor"
          break 2
        fi
      done
    done
  fi
fi

echo "Determined bump type: $BUMP_TYPE"

# Ensure VERSION file exists.
if [ ! -f VERSION ]; then
  echo "VERSION file not found. Creating VERSION with default value 0.0.0."
  echo "0.0.0" > VERSION
fi

# Read the current version from the VERSION file.
CURRENT_VERSION=$(cat VERSION)
echo "Current version: $CURRENT_VERSION"

IFS='.' read -r MAJOR MINOR PATCH <<< "$CURRENT_VERSION"

# Calculate the new version based on the bump type.
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

# Update the VERSION file with the new version.
echo "$NEW_VERSION" > VERSION
echo "Updated version set to: $NEW_VERSION"
