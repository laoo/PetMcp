# 0006: Code that decides emulated behaviour is vendored, tooling is fetched

**Status:** accepted

## Context

PGMEmu draws the line between its two kinds of dependency this way, after NGA: code that decides
what the emulated machine does is carried in the tree, and everything else is fetched.

## Decision

- **Vendored** under `libextern/`, unmodified, at a named upstream commit, with a
  `CMakeLists.txt` of ours and an entry in `libextern/README.md`: `m6502.h` and `m6502dasm.h`
  from floooh/chips ([0003](0003-cpu-core.md)). It is added with the 6502, in M1.
- **Fetched** with `FetchContent`, each pinned to an exact tag, at PGMEmu's versions: SDL3, Dear
  ImGui (docking branch), CLI11, spdlog, nlohmann/json, miniz, cpp-httplib
  ([0011](0011-network-transports.md)), and Catch2 for the tests.
- Code ported from VICE and NODISKEMU is not vendored: it is rewritten as this project's C++,
  under its style, with its source named ([0004](0004-licence-gpl-2.md)).
- Vendored code is not formatted, not tidied, and is built without the project's warning set.
  Its include directories are `SYSTEM` for consumers.

## Consequences

- Updating the CPU core is a commit that shows upstream's diff.
- A clean configure needs the network once per build directory.
