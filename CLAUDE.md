# Working in this repository

## Language

Discussion with the user is in Polish. **All code, comments, documentation,
commit messages and identifiers are in English.**

## Build and test

```sh
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
cmake --install build/release --component petmcp --prefix build/stage   # what a release package holds; PetMcp.app on macOS
```

Presets: `debug`, `release`, `asan`. Never create build directories by hand;
the presets own `build/<preset>`. The test presets run tests in parallel on
every core. `-DPET_BUILD_APP=OFF` at configure leaves out the desktop
application and with it SDL3 and ImGui.

```sh
build/debug/src/pet_app/petmcp --rom-dir ../vice/data/PET --card-dir ~/PetCard              # desktop
build/debug/src/pet_app/petmcp --rom-dir ../vice/data/PET --server tcp:7701 --mcp-http 7702  # with agents attached
build/debug/src/pet_cli/petmcp-cli --server --rom-dir ../vice/data/PET                       # JSON lines on stdio
build/debug/src/pet_cli/petmcp-cli --mcp --rom-dir ../vice/data/PET --card-dir ~/PetCard     # MCP for an agent
ctest --preset debug -L control                     # the protocol's recorded transcripts, tests/control/*.jsonl
ctest --preset debug -L regression                  # the batch scripts, tests/regression/*.json, against their golden sums
(cd build/debug/tests && ../src/pet_cli/petmcp-cli --batch ../../../tests/regression/boot.json --rom-dir ../../../../vice/data/PET --record)   # re-record one (docs/spec/batch.md)
(cd tests && ../build/debug/src/pet_cli/petmcp-cli --transcript control/lines.jsonl > control/lines.expect)   # re-record one, after a deliberate change of answers
(cd build/debug/tests && ./pet_test_disk disk && ../src/pet_cli/petmcp-cli --rom-dir ../../../../vice/data/PET --transcript ../../../tests/control/disk.jsonl > ../../../tests/control/disk.expect)   # the SD2PET's, which runs beside the disk it builds
./scripts/fetch-6502-tests.sh                       # the SingleStepTests 65x02 suite's NMOS 6502, 1.1 GB to fetch
build/release/tests/pet_tests "[cpu-suite]"         # the 6502 against it, about 15 s
./scripts/compile-shaders.sh                        # the screen's shaders, after changing their GLSL (brew install glslang spirv-cross)
```

```sh
./scripts/format.sh          # format in place
./scripts/format.sh --check  # fail on deviation
./scripts/tidy.sh            # clang-tidy against build/debug
```

Homebrew keeps LLVM keg-only, so `clang-tidy` is not on `PATH` on macOS, and it
does not know where Apple's SDK keeps the standard library. `scripts/tidy.sh`
handles both; do not invoke `clang-tidy` directly.

An agent drives the emulator with the project skill, `.claude/skills/petmcp/SKILL.md`: a headless
server, `scripts/petmcp.py`, and the methods to reach for.

What to build next is [docs/plans/milestones.md](docs/plans/milestones.md); a
session takes one milestone by name.

## Layout

| Path | Contents |
|---|---|
| `src/pet_core/` | The emulated machine and the control dispatcher. All logic; no SDL, ImGui, threads or file dialogs; deterministic. |
| `src/pet_server/` | Transports onto the dispatcher: JSON-lines (stdio, TCP) and MCP. |
| `src/pet_cli/` | `petmcp-cli`: headless runs, `--server`, `--mcp`, `--transcript`. Argument parsing and I/O only. |
| `src/pet_app/` | `petmcp`: the SDL3 + Dear ImGui desktop frontend. |
| `tests/` | Catch2 v3 tests, linked against `pet_core`; `control/` also holds the protocol's recorded transcripts. |
| `.claude/skills/petmcp/` | The project skill an agent drives the emulator with. |
| `libextern/` | Third-party code carried in the tree, unmodified. Not formatted, not tidied. `libextern/README.md` is the inventory. |
| `cmake/` | `Warnings.cmake`, `Dependencies.cmake`, `Version.cmake`, `Install.cmake`. |
| `docs/` | See `docs/README.md`; it is the index and the rulebook. |

## The workspace around it

The repository builds and tests on its own. These checkouts beside it are
optional; the tests that need one say so, and skip without it. They are
read-only references, never edited from here
([0002](docs/decisions/0002-references.md) says which decides what):

- `../vice` — VICE 3.10 with the owner's commits (IEEE-488 retimed to an
  SD2PET, the dual YM2149F board, raw memory maps). Its `src/pet`, `src/core`
  and `src/piacore.c` are the reference for the PET; port from them. Its
  `data/PET` holds the ROMs. `../PETTest/xpet-sd2pet.sh` starts its xpet as the
  owner's 3032.
- `../TangPet` — the owner's FPGA PET 3032. `eda_proj/src/pet/pet3032_core.v`
  is the reference for the video fetch and for the expansion board where
  [docs/hardware/expansion-board.md](docs/hardware/expansion-board.md) is
  silent. Its CPU is **not** a reference: it departs from the 6502.
- `../PETTest` — the owner's measurements on a real 3032 with an SD2PET Future
  (`README.md`), the programs that made them, and a VICE trace of the first
  second after power-up (`trace_1s.txt`).
- NODISKEMU (https://github.com/nils-eilers/NODISKEMU) is the source of the
  SD2PET's DOS ([0010](docs/decisions/0010-sd2pet.md)); clone it outside the
  tree to port from it.
- `../PGMWorkspace/PGMEmu` — the owner's PGM emulator, whose architecture,
  protocol, frontend and style this one takes.

Tests tagged `[cpu-suite]` read `build/tools/6502/` and skip without it.

## Code style

Enforced by `.clang-format` and `.clang-tidy`; the summary below is for
orientation, and the config files are the authority.

- Allman braces, 2-space indent, no tabs, 120-column limit.
- Spaces inside parentheses, but not empty ones: `foo( a, b )`, `if ( x )`, `bar()`.
- East const, pointer on the left: `int* p`, `int const* p`.
- Types are `CamelCase`; functions and variables are `camelBack`; namespaces are
  `lower_case` (`pet`, `pet::control`, ...).
- Class members are `mMember` (private or protected); struct members are `member` (public).
- Constants are `UPPER_CASE` at namespace and class scope, and `camelBack` inside functions.
- Enum names follow types; enum constants follow constants.
- File names are `CamelCase.hpp` / `CamelCase.cpp`.
- Emulated registers, buses and bit fields are held in unsigned fixed-width
  types (`std::uint8_t` ... `std::uint64_t`).
- A name ported from VICE or NODISKEMU keeps its words: `set_error_ts` becomes
  `setErrorTs`.
- Every ported module's header comment names the file(s) it ports and the
  commit they were read at.

C++23, but only the subset Apple clang, GCC and MSVC all implement.
`src/pet_core/src/PortabilityChecks.cpp` pins that subset down; extend it
rather than discovering a gap in CI. Prefer `fmt` (via spdlog) over
`std::format`, and avoid `std::print`, `<stacktrace>` and `std::flat_map`.

## Documentation discipline

The rules are in `docs/README.md` and they are not optional. In short:

- Documentation carries **why** and **contracts**; code carries **how**.
- One fact, one place; everywhere else links to it. Facts about the PET stay
  in VICE, TangPet and PETTest and are linked, not copied.
- No status, progress or session-note files.
- Documentation is updated **in the same commit** as the change that invalidated it.
- A new document requires an entry in `docs/README.md`, or it does not get written.

Non-obvious reasoning belongs in code comments; do not comment the obvious.

## Never

- Commit ROMs, disk images or programs that are not built by the tests.
- Commit before the owner has reviewed.
