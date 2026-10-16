# Vendored libraries

This is third-party code carried in the tree rather than fetched at configure time.
[0006](../docs/decisions/0006-dependencies.md) says what qualifies and why the two kinds of
dependency are handled differently. This file is the inventory and the procedure.

## The rules

- One directory per library, holding upstream's files **unmodified** and a `CMakeLists.txt` of
  ours. A change upstream needs goes upstream. A change we need is a reason to reconsider the
  library, not to fork it silently.
- Upstream's files are not formatted, not tidied, and built without the project's warning set.
  `scripts/format.sh`, `scripts/tidy.sh` and `.clang-tidy` all stop at `src/` and `tests/`, and
  the include directory is `SYSTEM` on the consumer's side. A library directory therefore has no
  `src/` subdirectory, because the tidy header filter would match it.
- The table below is updated **in the same commit** as the files it describes.

## Inventory

| Library | Upstream | Commit | Licence | Local changes | Used by |
|---|---|---|---|---|---|
| `chips` | https://github.com/floooh/chips | `9e88298ce56319953ac7a43213a1120359f7a3a6` (2026-09-26) | zlib | none | `pet_core`: the 6502, see [0003](../docs/decisions/0003-cpu-core.md) |

Of chips, only `chips/m6502.h`, `util/m6502dasm.h` and `LICENSE` are carried. The other chips, the
systems and the UI are not. The commit is the one PGMEmu carries its `z80.h` from.

## Updating one

1. Clone upstream at the new commit into a scratch directory, outside this tree.
2. Replace the library's files with upstream's, file by file, keeping the subset listed above.
3. Diff upstream's own build of the files against the flags in ours and carry over what changed.
4. Update the commit column above, then build and run the suite, the `[cpu-suite]` tests
   included, in the same commit.
