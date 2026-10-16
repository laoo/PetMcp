# 0007: Code style and toolchain are PGMEmu's

**Status:** accepted

## Context

The owner holds code quality and consistent formatting to a strict standard, and already enforces
it in PGMEmu, which took it from NGA.

## Decision

- `.clang-format`, `.clang-tidy`, `scripts/format.sh`, `scripts/tidy.sh` and
  `scripts/find-llvm-tool.sh` are PGMEmu's.
- The build follows PGMEmu's: CMake 3.25+, Ninja presets `debug`, `release` and `asan`, one
  interface target carrying the warning set (`cmake/Warnings.cmake`, with an option to turn
  warnings into errors), and Catch2 v3 for tests.
- The language is C++23, restricted to the subset Apple clang, GCC and MSVC all implement.
  `src/pet_core/src/PortabilityChecks.cpp` pins that subset down.

## Consequences

- One LLVM major formats and tidies the owner's projects alike.
- A name ported from VICE or NODISKEMU keeps its words in this style: `viacore_t1` becomes
  `t1`, `set_error_ts` becomes `setErrorTs`, so that a grep across the trees still meets.
