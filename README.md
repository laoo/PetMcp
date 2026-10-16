# PetMcp

An emulator of the Commodore PET 3032, cycle-exact, with an SD2PET disk drive on its IEEE-488
bus and the owner's expansion board (two YM2149Fs, character RAM, 1 MB of banked RAM). It is
driven by people through a desktop application, and by programs and AI agents through one
control protocol, over JSON lines or MCP.

PetMcp is under construction; [docs/plans/milestones.md](docs/plans/milestones.md) is what is
still to be built.

## Building

CMake 3.25 or later, Ninja, and a C++23 compiler (Apple clang, GCC 14 or MSVC). The libraries are
fetched when the build is configured.

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

## Running

The emulator needs the PET's ROMs, under the names VICE gives them: VICE's `data/PET` directory
serves.

```sh
build/release/src/pet_app/petmcp --rom-dir /path/to/vice/data/PET --card-dir /path/to/card
```

`--card-dir` is the directory that stands for the SD2PET's SD card.

For a program or an agent, the same emulator runs headless:

```sh
build/release/src/pet_cli/petmcp-cli --server tcp:7701 --rom-dir /path/to/vice/data/PET   # JSON lines on TCP
build/release/src/pet_cli/petmcp-cli --mcp --rom-dir /path/to/vice/data/PET               # MCP on stdio
```

The desktop application serves the same protocol while its window is open, with
`--server tcp:PORT` and `--mcp-http PORT`. The protocol is
[docs/spec/control-protocol.md](docs/spec/control-protocol.md).

## Licence

GPL-2.0; see [LICENSE](LICENSE) and [THIRD_PARTY.md](THIRD_PARTY.md).
