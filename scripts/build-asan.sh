#!/bin/sh
set -e
BUILD_DIR="${1:-build-asan}"

meson setup "$BUILD_DIR" \
  -Db_sanitize=address \
  -Db_lundef=false \
  -Ddevelopment=true \
  "$@"
meson compile -C "$BUILD_DIR"
meson test -C "$BUILD_DIR" --verbose
echo "ASAN build ready. Run individual tests:"
echo "  $BUILD_DIR/tests/unit/test-<name>"
echo "  ASAN_OPTIONS=detect_leaks=1 $BUILD_DIR/tests/unit/test-<name>"
