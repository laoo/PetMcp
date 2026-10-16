# PetMcp

PetMcp is an emulator of the Commodore PET 3032, the 40-column PET with BASIC 2, cycle by cycle.

It follows **VICE**, the Versatile Commodore Emulator, by the VICE Team: its PET, its 6522 and
6520, and its timing. The SD2PET Future disk drive on the IEEE-488 bus runs the DOS of
**NODISKEMU**, by Nils Eilers and Ingo Korb. An LLM ported both to C++, held to VICE's trace and
to measurements made on a real PET. All credit for understanding the machine and the drive goes to
their authors.

- The 6502, the PIAs, the VIA and the video run a cycle at a time. The first two million cycles
  after power-up match VICE's trace instruction by instruction, cycle by cycle, and the 6502
  passes the SingleStepTests suite, undocumented opcodes included.
- The SD2PET runs NODISKEMU's DOS: D64, D71, D81, D80, D82 and DNP images, `CD` into them, the
  error channel, the commands on channel 15 and its listings. A folder stands for its SD card. Its
  timing on the bus is set from a real SD2PET's, measured with the kernal's receiver and two
  faster ones.
- The owner's expansion board is in the ROM sockets: two YM2149F sound chips, character RAM and
  1 MB of banked RAM ([docs/hardware/expansion-board.md](docs/hardware/expansion-board.md)).
- The window has shaders (sharp, scanlines, CRT) and the monitor's phosphor, types from the host
  keyboard by symbol, and has a debugger: breakpoints, watchpoints, the history of execution, the
  I/O chips, memory and the IEEE-488 bus.
- Other programs and AI agents can control it, with or without the window.

## The ROMs

**PetMcp does not include the PET's ROMs.** It reads them as VICE names them, from VICE's
`data/PET` folder or a copy of it: `basic-2.901465-01-02.bin`, `edit-2-n.901447-24.bin`,
`kernal-2.901465-03.bin` and `characters-2.901447-10.bin`.

Commodore and PET are trademarks of their owners. PetMcp is not affiliated with them, nor with
VICE, nor with the makers of the SD2PET.

## Building

You need CMake 3.25 or later, Ninja and a C++23 compiler: Apple clang, GCC 14 or MSVC.

```sh
cmake --preset release && cmake --build --preset release && ctest --preset release
```

CMake downloads every dependency, SDL3 included, at a pinned version when it first configures. So
the first configure needs the network.

On Linux, SDL3 needs the development packages of the display and sound systems. On Ubuntu:

```sh
sudo apt install ninja-build libasound2-dev libpulse-dev libx11-dev libxext-dev libxrandr-dev \
  libxcursor-dev libxi-dev libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev \
  libegl1-mesa-dev libdbus-1-dev libudev-dev
```

- To build without the window (and without SDL3 and ImGui), add `-DPET_BUILD_APP=OFF`.
- On Windows, the `windows` preset builds with Visual Studio.

Each tagged release on GitHub has packages for macOS (Apple silicon), Linux (x64) and Windows
(x64). The macOS application is not notarized. The first time, open it with right-click > Open,
or run `xattr -dr com.apple.quarantine PetMcp.app`.

## Running

There are two programs. Both print their options with `--help`.

### The window: `petmcp`

```sh
petmcp
petmcp --rom-dir path/to/vice/data/PET --card-dir path/to/card
petmcp --rom-dir path/to/vice/data/PET game.d64
```

- File > Choose ROM directory and File > Choose SD card directory set the folders, and the window
  remembers them.
- A `.prg` given after the options, opened with File > Open program or disk, or dropped on the
  window is loaded into memory and run once the PET is at READY.
- A disk image opened or dropped the same way is entered in the SD2PET, and its first program is
  loaded with `LOAD"*",8` and run.
- `--no-expansion` leaves the expansion board's sockets empty, as on a plain 3032.

The host keyboard types what its keys make: a key that makes `+` on the host types `+` on the
PET, whatever the layouts. A letter's SHIFT is the PET's SHIFT, so capitals make graphics in the
PET's graphics set. The keys that make no character are bound in View > Input:

| PET key | Host key |
|---|---|
| RETURN | Return |
| INST/DEL | Backspace |
| CLR/HOME | Home |
| The cursor keys | The arrows |
| RUN/STOP | Esc |
| RVS/OFF | Tab |
| ← | ` |

- Display chooses the shader, the phosphor (green as the 3032's, white or amber) and the shape of
  the dots.
- Emulation pauses the PET, resets it, or powers it off and on.
- View opens the debugger's windows: CPU (registers, code, breakpoints, stepping), History (every
  instruction of the last seconds, searched by address or disassembly), Memory, I/O, Sound (the
  YM2149Fs), and Drive (the SD2PET's card, image and status, a command to send it, and the
  IEEE-488 bus's last changes).

### The SD2PET

The drive is device 8. It works as the real one does:

```basic
LOAD"$",8                       : REM the card's folder, as a BASIC program
LIST
OPEN1,8,15,"CD:GAME.D64":CLOSE1 : REM enter an image
LOAD"*",8                       : REM its first program
OPEN1,8,15,"CD←":CLOSE1         : REM leave it
```

Its status after power-up is `73,NODISKEMU V1.0.0ALPHA0-20260918-E51A6DF,00,00`. Files on the card
are host files, with the 8.3 names a FAT card would give them.
[docs/hardware/sd2pet.md](docs/hardware/sd2pet.md) says how the drive is built, and where it
departs from the hardware.

### The command line: `petmcp-cli`

`petmcp-cli` has no window. It runs only on request.

```sh
petmcp-cli --server --rom-dir vice/data/PET                              # JSON lines on stdio
petmcp-cli --batch tests/regression/basic.json --rom-dir vice/data/PET  # check a batch script
```

Useful options:

- `--card-dir DIR` is the SD2PET's card; `--no-expansion` takes the expansion board out.
- `--state-dir DIR` is where `state.save` and `state.load` keep save states.
- `--batch FILE` runs a batch script and checks it against its recorded sums. `--record` writes
  the sums into the script. The format is in [docs/spec/batch.md](docs/spec/batch.md).
- `--transcript FILE` answers the requests in FILE and prints the answers. `--expect FILE`
  compares them with recorded answers instead.

## Remote control

Other programs can control a running emulator, with or without the window. They can run it by
frames, by cycles or until a condition, type on its keyboard, load programs, mount and run disks,
read and write memory, read registers, disassemble code, set breakpoints and watchpoints, search
the history of execution, watch the IEEE-488 bus, save and load states, take screenshots and record
the sound.

There is one set of methods. It is available in two ways:

- **JSON lines**, on stdio or TCP. Each request is one line of JSON. Each answer is one line.
- **MCP** (Model Context Protocol), on stdio or HTTP. Each method is an MCP tool, so an AI agent
  can use the emulator directly. Screenshots come back as images.

The protocol is specified in [docs/spec/control-protocol.md](docs/spec/control-protocol.md).

### JSON lines

```sh
petmcp-cli --server tcp:7701 --rom-dir vice/data/PET --card-dir card
```

Then send requests from another terminal:

```sh
printf '%s\n' '{"id":1,"method":"emu.run_until","params":{"screen_contains":"READY."}}' | nc -w 5 127.0.0.1 7701
```

```json
{"id":1,"ok":true,"result":{"reason":"condition_met","cycles_executed":1460451,"frames_executed":88}}
```

`scripts/petmcp.py` is a small client. It sends one request and prints the result:

```sh
scripts/petmcp.py input.type text='print 2+2\n'
scripts/petmcp.py emu.run_frames count=10
scripts/petmcp.py video.screen_text
scripts/petmcp.py disk.run path=game.d64
```

Without `tcp:PORT`, `--server` reads requests on stdin and writes answers on stdout.

### MCP

```sh
petmcp-cli --mcp --rom-dir vice/data/PET     # MCP on stdio
petmcp --mcp-http 7702                       # the window, MCP on http://127.0.0.1:7702/mcp
```

To add it to Claude Code:

```sh
claude mcp add petmcp -- /path/to/petmcp-cli --mcp --rom-dir /path/to/vice/data/PET --card-dir /path/to/card
claude mcp add --transport http petmcp http://127.0.0.1:7702/mcp    # the window, when it is open
```

Other MCP clients take a configuration like this one:

```json
{"mcpServers":{"petmcp":{"command":"/path/to/petmcp-cli","args":["--mcp","--rom-dir","/path/to/vice/data/PET"]}}}
```

In this repository, the project skill `.claude/skills/petmcp/SKILL.md` tells an agent how to drive
the emulator.

### The window

The window serves the same protocol while you watch it:

```sh
petmcp --server tcp:7701 --mcp-http 7702 --rom-dir vice/data/PET --card-dir card
```

All clients control the same emulator: the PET on the screen. A client can pause the window's run
(`emu.pause`), and a breakpoint that the window's run reaches pauses it too.

The servers listen on 127.0.0.1 only. They have no authentication. A client can make the emulator
read and write files, by the paths it gives to methods such as `video.screenshot`, `state.save`
and `disk.card`, and through the SD2PET's card. Do not expose the servers to a network.

## Tests

`ctest` runs the unit tests, the 6502 suite, recorded sessions of the control protocol, the
SD2PET against the measurements of a real one, and a regression suite of batch scripts. The tests
that need the ROMs, or the measuring programs, skip when they are not there.
[CLAUDE.md](CLAUDE.md) says where they look for them.

The design is in [docs/architecture.md](docs/architecture.md). The reasons for it are in
[docs/decisions/](docs/decisions/). [docs/README.md](docs/README.md) is the index.

## Licence

Copyright (C) 2026 Waldemar Pawlaszek.

PetMcp is released under the GNU General Public License, version 2 only ([LICENSE](LICENSE)).
NODISKEMU's DOS is under that licence;
[docs/decisions/0004-licence-gpl-2.md](docs/decisions/0004-licence-gpl-2.md) says why.
[THIRD_PARTY.md](THIRD_PARTY.md) lists the work it is derived from and the libraries it is built
with, and carries their terms.
