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

`--card-dir` is the directory that stands for the SD2PET's SD card, device 8; it can also be
chosen with File > Choose SD card directory, and is then kept. The owner's expansion board, two
YM2149Fs, character RAM and 1 MB of banked RAM, fills the `$9000`-`$BFFF` sockets unless
`--no-expansion` leaves them empty. A `.prg` named after the options, opened with File > Open
program or disk or dropped on the window is loaded and run once the PET is at READY. A disk image
(`.d64`, `.d71`, `.d81`, `.d80`, `.d82`, `.dnp`) opened or dropped the same way is entered in the
SD2PET, and its first program loaded with `LOAD"*",8` and run. The ROM directory can also be
chosen with File > Choose ROM directory, and is then kept.

View opens the debugger's windows: CPU (registers, code, breakpoints, stepping), History (every
instruction of the last seconds, as it ran, searched by PC or disassembly, the seconds set there),
Memory, I/O, and Drive (the SD2PET's card, image and status, a command to send it, and the
IEEE-488 bus's last changes).

The host keyboard types as a PET's would: what a key makes on the host appears on the PET, a
letter's SHIFT is the PET's SHIFT (so capitals make graphics in the graphics set), and the keys
that make no character, RETURN, INST/DEL, CLR/HOME, the cursor keys, RUN/STOP, RVS and the left
arrow, are bound in the Input window.

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
