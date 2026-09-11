#!/usr/bin/env bash
set -euo pipefail

echo "[pre-commit] Running pre-commit checks..."

# Check not on main/master
BRANCH=$(git rev-parse --abbrev-ref HEAD)
if [ "$BRANCH" = "main" ] || [ "$BRANCH" = "master" ]; then
  echo "[FAIL] pre-commit: you are on $BRANCH. Create a feature branch first."
  exit 1
fi

# Check for unstaged changes
if ! git diff --quiet; then
  echo "[FAIL] pre-commit: unstaged changes. Stage or stash before committing."
  exit 1
fi

# Run lint
echo "[pre-commit] Running clang-format..."
find src tests -name '*.c' -o -name '*.h' 2>/dev/null | while read -r f; do
  if ! clang-format --dry-run --Werror "$f" 2>/dev/null; then
    echo "[FAIL] clang-format: $f needs formatting"
    exit 1
  fi
done

# Build
echo "[pre-commit] Building..."
meson compile -C build 2>&1 | tail -20

# Test
echo "[pre-commit] Running tests..."
meson test -C build --print-errorlogs 2>&1 | tail -30

echo "[pre-commit] All checks passed."
