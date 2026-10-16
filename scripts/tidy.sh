#!/usr/bin/env bash
# Runs clang-tidy over the project sources against the debug compile database.
#
# On macOS the Homebrew clang-tidy does not know where Apple's SDK keeps the
# standard library, so every header lookup fails unless we hand it the sysroot
# that xcrun reports.
set -euo pipefail
root="$( cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd )"
tidy="$( "$root/scripts/find-llvm-tool.sh" clang-tidy )"
db="${1:-$root/build/debug}"

extra=""
if [[ "$( uname -s )" == "Darwin" ]]; then
  extra="--extra-arg=-isysroot$( xcrun --show-sdk-path )"
fi

# One clang-tidy per file, as many at once as there are cores: clang-tidy
# takes a list of files but works through it one at a time, and a list of a
# hundred took minutes on a machine with ten cores idle. `xargs` exits 123
# where any run failed, which is what a finding under -warnings-as-errors
# would be; a plain warning leaves clang-tidy's exit at 0, as it always has.
jobs="$( getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4 )"
find "$root/src" "$root/tests" -type f -name '*.cpp' -print0 \
  | xargs -0 -P "$jobs" -n 1 "$tidy" -p "$db" ${extra:+"$extra"}
