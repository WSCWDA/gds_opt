#!/usr/bin/env bash
set -euo pipefail

# Create benchmark test files with configurable sizes.
# Default output directory: /mnt/gds
#
# Examples:
#   scripts/gen_test_file.sh --size 1G
#   scripts/gen_test_file.sh --size 4G --name test_4g.dat
#   scripts/gen_test_file.sh --size 16G --dir /mnt/gds2/cwd_test --method dd --pattern zero
#   scripts/gen_test_file.sh --size 8G --method fallocate

OUT_DIR="/mnt/gds"
FILE_NAME="test.dat"
SIZE="1G"
METHOD="dd"         # dd | fallocate
PATTERN="zero"      # zero | random (only for dd)
FORCE=0

usage() {
  cat <<USAGE
Usage: $0 [options]

Options:
  --size <SIZE>        File size (e.g. 4K, 16M, 1G, 32G). Default: 1G
  --dir <DIR>          Output directory. Default: /mnt/gds
  --name <NAME>        Output filename. Default: test.dat
  --method <METHOD>    dd | fallocate. Default: dd
  --pattern <PATTERN>  zero | random (dd only). Default: zero
  --force              Overwrite if file exists
  -h, --help           Show this help
USAGE
}

parse_size_to_bytes() {
  local v="$1"
  if [[ "$v" =~ ^[0-9]+$ ]]; then
    echo "$v"
    return 0
  fi

  if [[ "$v" =~ ^([0-9]+)([KMGTP]?)$ ]]; then
    local num="${BASH_REMATCH[1]}"
    local unit="${BASH_REMATCH[2]}"
    case "$unit" in
      "") echo "$num" ;;
      K) echo $((num * 1024)) ;;
      M) echo $((num * 1024 * 1024)) ;;
      G) echo $((num * 1024 * 1024 * 1024)) ;;
      T) echo $((num * 1024 * 1024 * 1024 * 1024)) ;;
      P) echo $((num * 1024 * 1024 * 1024 * 1024 * 1024)) ;;
      *) return 1 ;;
    esac
    return 0
  fi

  return 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --size)
      SIZE="${2:-}"; shift 2 ;;
    --dir)
      OUT_DIR="${2:-}"; shift 2 ;;
    --name)
      FILE_NAME="${2:-}"; shift 2 ;;
    --method)
      METHOD="${2:-}"; shift 2 ;;
    --pattern)
      PATTERN="${2:-}"; shift 2 ;;
    --force)
      FORCE=1; shift ;;
    -h|--help)
      usage; exit 0 ;;
    *)
      echo "Unknown option: $1" >&2
      usage
      exit 1 ;;
  esac
done

if [[ -z "$SIZE" ]]; then
  echo "--size cannot be empty" >&2
  exit 1
fi

BYTES="$(parse_size_to_bytes "$SIZE")" || {
  echo "Invalid --size value: $SIZE" >&2
  exit 1
}

if [[ "$BYTES" -le 0 ]]; then
  echo "Size must be > 0" >&2
  exit 1
fi

if [[ "$METHOD" != "dd" && "$METHOD" != "fallocate" ]]; then
  echo "--method must be dd or fallocate" >&2
  exit 1
fi

if [[ "$PATTERN" != "zero" && "$PATTERN" != "random" ]]; then
  echo "--pattern must be zero or random" >&2
  exit 1
fi

if [[ "$METHOD" == "fallocate" && "$PATTERN" == "random" ]]; then
  echo "--pattern=random is only supported with --method=dd" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"
OUT_PATH="$OUT_DIR/$FILE_NAME"

if [[ -e "$OUT_PATH" && "$FORCE" -ne 1 ]]; then
  echo "File exists: $OUT_PATH (use --force to overwrite)" >&2
  exit 1
fi

if [[ -e "$OUT_PATH" && "$FORCE" -eq 1 ]]; then
  rm -f "$OUT_PATH"
fi

echo "Generating file..."
echo "  path   : $OUT_PATH"
echo "  size   : $SIZE ($BYTES bytes)"
echo "  method : $METHOD"
if [[ "$METHOD" == "dd" ]]; then
  echo "  pattern: $PATTERN"
fi

if [[ "$METHOD" == "fallocate" ]]; then
  fallocate -l "$BYTES" "$OUT_PATH"
else
  # Use 1M block for practical throughput, and add a tail write for non-MiB sizes.
  BS=$((1024 * 1024))
  COUNT=$((BYTES / BS))
  TAIL=$((BYTES % BS))

  if [[ "$PATTERN" == "zero" ]]; then
    SRC="/dev/zero"
  else
    SRC="/dev/urandom"
  fi

  if [[ "$COUNT" -gt 0 ]]; then
    dd if="$SRC" of="$OUT_PATH" bs="$BS" count="$COUNT" status=progress conv=fsync
  else
    : > "$OUT_PATH"
  fi

  if [[ "$TAIL" -gt 0 ]]; then
    dd if="$SRC" of="$OUT_PATH" bs=1 count="$TAIL" seek=$((COUNT * BS)) \
      conv=notrunc,fsync status=none
  fi
fi

# Force metadata update and show final size.
sync
ACTUAL_SIZE=$(stat -c%s "$OUT_PATH")
echo "Done."
echo "  actual bytes: $ACTUAL_SIZE"
ls -lh "$OUT_PATH"
