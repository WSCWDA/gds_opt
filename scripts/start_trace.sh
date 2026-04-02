#!/usr/bin/env bash
set -euo pipefail

TRACEFS="${TRACEFS:-/sys/kernel/tracing}"
OUT_DIR="${1:-results/trace}"
EVENT_GROUPS_DEFAULT="nvfs,cufile"
EVENT_FILTER_DEFAULT="map,bounce,io,complete"

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
echo nop > "${TRACEFS}/current_tracer"
echo > "${TRACEFS}/trace"

# Disable all events first; then enable matched ones.
echo 0 > "${TRACEFS}/events/enable"

IFS=',' read -r -a EVENT_GROUPS <<< "${GDS_EVENT_GROUPS:-${EVENT_GROUPS_DEFAULT}}"
IFS=',' read -r -a EVENT_FILTERS <<< "${GDS_EVENT_FILTERS:-${EVENT_FILTER_DEFAULT}}"

enabled=0
for group in "${EVENT_GROUPS[@]}"; do
  [[ -d "${TRACEFS}/events/${group}" ]] || continue
  while IFS= read -r event_dir; do
    event_name="$(basename "${event_dir}")"
    lname="${event_name,,}"
    for needle in "${EVENT_FILTERS[@]}"; do
      n="${needle,,}"
      if [[ "${lname}" == *"${n}"* ]]; then
        if [[ -w "${event_dir}/enable" ]]; then
          echo 1 > "${event_dir}/enable"
          enabled=$((enabled + 1))
        fi
        break
      fi
    done
  done < <(find "${TRACEFS}/events/${group}" -mindepth 1 -maxdepth 1 -type d | sort)
done

if [[ "${enabled}" -eq 0 ]]; then
  echo "warning: no matching trace events were enabled." >&2
  echo "hint: inspect available events with: sudo ls ${TRACEFS}/events/<group>/" >&2
fi

echo 1 > "${TRACEFS}/tracing_on"

cat > "${OUT_DIR}/trace_meta.txt" <<META
tracefs=${TRACEFS}
start_time_utc=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
enabled_event_count=${enabled}
event_groups=${GDS_EVENT_GROUPS:-${EVENT_GROUPS_DEFAULT}}
event_filters=${GDS_EVENT_FILTERS:-${EVENT_FILTER_DEFAULT}}
META

echo "Tracing started."
echo "  output dir: ${OUT_DIR}"
echo "  enabled events: ${enabled}"
echo "Run your benchmark now, then execute: scripts/stop_trace.sh ${OUT_DIR}"
