#!/usr/bin/env bash
set -euo pipefail
ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
DATA=${DATA_DIR:-"${ROOT}/data"}
mkdir -p "${DATA}/sift" "${DATA}/gist"

fetch_extract() {
  local url=$1 archive=$2 destination=$3
  [[ -s "${archive}" ]] || curl --fail --location --retry 3 "${url}" -o "${archive}"
  tar -C "${destination}" -xzf "${archive}"
}

# Public TexMex corpus files only.  This script never generates vectors.
fetch_extract "ftp://ftp.irisa.fr/local/texmex/corpus/sift.tar.gz" \
  "${DATA}/sift/sift.tar.gz" "${DATA}/sift"
fetch_extract "ftp://ftp.irisa.fr/local/texmex/corpus/gist.tar.gz" \
  "${DATA}/gist/gist.tar.gz" "${DATA}/gist"

find "${DATA}" -type f \( -name '*.fvecs' -o -name '*.ivecs' \) -printf '%p %s bytes\n'
