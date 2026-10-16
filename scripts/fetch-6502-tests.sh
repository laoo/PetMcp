#!/usr/bin/env bash
# Downloads the SingleStepTests 65x02 suite's NMOS 6502 tests into
# build/tools/6502/, which the tests tagged [cpu-suite] read and skip without.
#
#   scripts/fetch-6502-tests.sh
#
# About 1.1 GB of JSON, a file per opcode, from
# https://github.com/SingleStepTests/65x02 at a fixed commit, so that a result
# can be reproduced. Each file is gzipped as it arrives, which leaves about
# 150 MB. Files already present are not fetched again. Kept free of bash-4
# features: macOS ships bash 3.2.
set -euo pipefail
root="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
commit="${SST_6502_COMMIT:-2f6980a2d95757486c7bee24355c360e40e2a224}"
target="$root/build/tools/6502"
base="https://raw.githubusercontent.com/SingleStepTests/65x02/$commit/6502/v1"

mkdir -p "$target"
for byte in $( seq 0 255 )
do
  name="$( printf '%02x.json' "$byte" )"
  [[ -s "$target/$name.gz" ]] && continue
  curl -sSL --fail -o "$target/$name.part" "$base/$name"
  gzip -9 -c "$target/$name.part" > "$target/$name.gz.part"
  mv "$target/$name.gz.part" "$target/$name.gz"
  rm "$target/$name.part"
done
echo "$( ls "$target"/*.json.gz | wc -l | tr -d ' ' ) files in $target"
