#!/usr/bin/env bash
# Pre-edit hook: verify GTK4/libadwaita API calls in the proposed edit
# Usage: pre-edit.sh <file-to-edit>
# Reads a proposed change from stdin and checks GTK API calls against docs.

set -euo pipefail

FILE="${1:-}"
GTK_DOCS="/usr/share/doc/gtk4"
ADW_DOCS="/usr/share/doc/libadwaita-1"

if [ -z "$FILE" ]; then
  echo "Usage: pre-edit.sh <file>"
  exit 1
fi

# Don't check non-C files
case "$FILE" in
  *.c|*.h) ;;
  *) exit 0 ;;
esac

# Check if new/changed lines reference GTK functions we haven't verified
# Reads stdin (the diff or proposed new content)
# This is a lightweight check: warn on gtk_/adw_ calls not found in docs

check_api() {
  local doc_dir="$1"
  local prefix="$2"
  local func="$3"

  if grep -l "${func}" "${doc_dir}"/*.html 2>/dev/null | head -1 >/dev/null 2>&1; then
    return 0
  fi
  return 1
}

# Read the diff from stdin
DIFF=$(cat)

# Extract gtk_ function calls
while IFS= read -r line; do
  # Skip comments and strings
  case "$line" in
    *//*|*\**|*printf*) continue ;;
  esac

  # Find gtk_ function calls (not declarations or comments)
  for func in $(echo "$line" | grep -oP 'gtk_\w+(?=\s*\()' | sort -u); do
    if ! check_api "$GTK_DOCS" "gtk_" "$func"; then
      echo "WARNING: $func not found in $GTK_DOCS"
      echo "  Location: $FILE"
      echo "  Action: verify with 'mcp-server-context7' or ask user"
    fi
  done

  for func in $(echo "$line" | grep -oP 'adw_\w+(?=\s*\()' | sort -u); do
    if ! check_api "$ADW_DOCS" "adw_" "$func"; then
      echo "WARNING: $func not found in $ADW_DOCS"
      echo "  Location: $FILE"
      echo "  Action: verify with 'mcp-server-context7' or ask user"
    fi
  done
done <<< "$DIFF"
