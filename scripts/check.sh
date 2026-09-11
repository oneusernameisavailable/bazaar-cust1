#!/usr/bin/env bash
set -euo pipefail

echo "=== LINT: clang-format ==="
find src tests -name '*.c' -o -name '*.h' | while read -r f; do
  if ! clang-format --dry-run --Werror "$f" 2>/dev/null; then
    echo "[FAIL] format at $f"
    clang-format --dry-run "$f" 2>&1 || true
    fail=1
  fi
done

echo "=== LINT: clang-tidy (if available) ==="
if command -v clang-tidy &>/dev/null && [ -f build/compile_commands.json ]; then
  find src tests -name '*.c' | while read -r f; do
    clang-tidy "$f" -p build 2>&1 | grep -E '(warning|error)' && fail=1 || true
  done
fi

echo "=== BUILD ==="
meson compile -C build

echo "=== TEST ==="
meson test -C build --print-errorlogs

echo "=== CHECK PASSED ==="
