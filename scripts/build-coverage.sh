#!/bin/sh
set -e
BUILD_DIR="${1:-build-coverage}"

meson setup "$BUILD_DIR" -Db_coverage=true "$@"
meson compile -C "$BUILD_DIR"
meson test -C "$BUILD_DIR" --verbose
ninja -C "$BUILD_DIR" coverage-html
echo "Coverage report: $BUILD_DIR/meson-logs/coveragereport/index.html"
