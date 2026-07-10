#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/src/build}"
TEST_BIN="${TEST_BIN:-$BUILD_DIR/alligator_tests}"
PROFILE_RAW="${PROFILE_RAW:-$ROOT_DIR/src/default.profraw}"
PROFILE_DATA="${PROFILE_DATA:-$ROOT_DIR/src/default.profdata}"
REPORT_OUT="${REPORT_OUT:-$ROOT_DIR/src/tests/coverage/coverage_report.txt}"
TOP_OUT="${TOP_OUT:-$ROOT_DIR/src/tests/coverage/coverage_top15.txt}"
MIN_LINE_COVERAGE="${MIN_LINE_COVERAGE:-0}"
INCLUDE_REGEX='^.*/src/.*\.c$'
EXCLUDE_REGEX='^.*/src/tests/.*|^.*/src/external/.*|^.*/src/build/.*'

if command -v llvm-profdata >/dev/null 2>&1; then
  LLVM_PROFDATA_CMD=(llvm-profdata)
else
  LLVM_PROFDATA_CMD=(xcrun llvm-profdata)
fi
if command -v llvm-cov >/dev/null 2>&1; then
  LLVM_COV_CMD=(llvm-cov)
else
  LLVM_COV_CMD=(xcrun llvm-cov)
fi

if [[ ! -x "$TEST_BIN" ]]; then
  echo "error: test binary not found or not executable: $TEST_BIN" >&2
  exit 1
fi

cd "$ROOT_DIR/src"
rm -f "$PROFILE_RAW" "$PROFILE_DATA" "$REPORT_OUT" "$TOP_OUT"

LLVM_PROFILE_FILE="$PROFILE_RAW" "$TEST_BIN" pass
"${LLVM_PROFDATA_CMD[@]}" merge -sparse "$PROFILE_RAW" -o "$PROFILE_DATA"

"${LLVM_COV_CMD[@]}" report "$TEST_BIN" \
  -instr-profile="$PROFILE_DATA" \
  -ignore-filename-regex="$EXCLUDE_REGEX" \
  > "$REPORT_OUT"

"${LLVM_COV_CMD[@]}" report "$TEST_BIN" \
  -instr-profile="$PROFILE_DATA" \
  -ignore-filename-regex="$EXCLUDE_REGEX" \
  | awk '
    BEGIN { started = 0; }
    /^TOTAL/ { started = 0; next; }
    /^-+/ { if (started == 0) { started = 1; next; } }
    {
      if (started && $1 ~ /\.c$/) print;
    }
  ' | sort -k10,10n | head -n 15 > "$TOP_OUT"

python3 "$ROOT_DIR/src/tests/coverage/parse_coverage_summary.py" "$REPORT_OUT" "$MIN_LINE_COVERAGE"
