#!/usr/bin/env bash
set -euo pipefail

TRACEFS="${TRACEFS:-/sys/kernel/tracing}"
OUT_DIR="${1:-results/trace}"
RAW_TRACE="${OUT_DIR}/trace_raw.txt"

if [[ ! -d "${TRACEFS}" ]]; then
  echo "error: tracefs not found at ${TRACEFS}" >&2
  exit 1
fi

if [[ ! -w "${TRACEFS}/tracing_on" ]]; then
  echo "error: insufficient permission to control tracefs (try sudo)" >&2
  exit 1
fi

mkdir -p "${OUT_DIR}"

echo 0 > "${TRACEFS}/tracing_on"
cat "${TRACEFS}/trace" > "${RAW_TRACE}"

echo 0 > "${TRACEFS}/events/enable"

echo "stop_time_utc=$(date -u +"%Y-%m-%dT%H:%M:%SZ")" >> "${OUT_DIR}/trace_meta.txt"

echo "Trace captured: ${RAW_TRACE}"
echo "Next: python3 scripts/parse_trace.py --input ${RAW_TRACE} --output ${OUT_DIR}/trace_summary.csv"
