# Architecture

This document covers what the components are, how emulated time and data flow through them,
and why the boundaries sit where they do. The reasoning behind individual choices is in
[the decision records](decisions/). Facts about the PET itself are in VICE's sources
(`../vice/src/pet`), and facts about the expansion board in
[hardware/expansion-board.md](hardware/expansion-board.md); neither is repeated here
([0002](decisions/0002-references.md)).

## The shape

```
                 ┌──────────────────────────── pet_core (library) ───────────────────────────┐
  ROMs, card ───▶│ Machine                                                                   │
  directory      │  ├─ 6502 (m6502.h)   ├─ PIA1: keyboard, retrace   ├─ Video: the beam      │
                 │  ├─ RAM, ROMs        ├─ PIA2: IEEE-488 data        ├─ Expansion board:     │
                 │  └─ Keyboard matrix  └─ VIA: IEEE-488, CB2 sound      2 × YM2149F, RAMs    │
                 │                          │                                                │
                 │                    IEEE-488 bus ── SD2PET (NODISKEMU's DOS, card directory)│
                 │                                                                           │
                 │         control::Dispatcher  (every capability, JSON in and out)          │
                 └───────────────▲──────────────────▲─────────────────▲──────────────────────┘
                                 │                  │                 │
                     pet_server transports      petmcp (GUI)     petmcp-cli (headless)
                  JSON-lines stdio/TCP, MCP    SDL3 + ImGui      --server, --mcp, --transcript
```

The core is a library with no SDL, no ImGui, no threads and no file dialogs, and it is
deterministic: the same ROMs, card directory and inputs always give the same frames, sound and
memory. Everything else is a client of it. The boundary is drawn for three reasons:

- **Testability.** The suite links the core and nothing else, so every device is tested headless.
- **Determinism.** It is what makes golden frames and the timing tests against the owner's
  measurements possible.
- **Agent use.** An agent session reaches the same machine the GUI shows, through the same
  dispatcher.

## Emulated time

The PET runs on one clock: the 6502's 1 MHz cycle, which the 8 MHz dot clock divides into 8 dots.
Time is a 64-bit count of CPU cycles since power-up. Every device is stepped once per cycle, in
the cycle's order ([0009](decisions/0009-stepped-a-cycle-at-a-time.md)):

| Device | Rate in cycles | Source |
|---|---|---|
| 6502 | one bus access a cycle | `m6502.h`, held to VICE's interrupt timing |
| Video | 8 dots a cycle, 64 cycles a line, 260 lines a frame: 16640 cycles, 60.1 Hz | VICE's fixed CRTC values for a PET without a CRTC (`pet.c`, `pet_crtc_set_screen`) |
| VIA, PIAs | a cycle | VICE's `viacore.c` and `piacore.c` |
| YM2149F | 2 or 1 clocks a cycle | [hardware/expansion-board.md](hardware/expansion-board.md) |
| IEEE-488 bus, SD2PET | a cycle | [0010](decisions/0010-sd2pet.md) |

A frame is the span between two starts of the vertical retrace. The retrace drives PIA1 CB1 (the
60 Hz interrupt) and VIA PB5, at the moments VICE and TangPet agree on: on after the 40th
character of line 199, off at the same point of line 259.

## The machine

- **Memory.** 32 KB of RAM, 1 KB of video RAM at `$8000` mirrored at `$8400`, and BASIC 2, the
  screen editor and the kernal at `$C000`, `$E000` and `$F000`. The ROMs are read from the ROM
  directory under the names VICE gives them (`basic-2.901465-01-02.bin`,
  `edit-2-n.901447-24.bin`, `kernal-2.901465-03.bin`, `characters-2.901447-10.bin`), so that
  VICE's `data/PET` serves as one. The I/O page decodes as VICE's `petmem.c` does: a write
  reaches every chip its address selects, and a read of several is their AND. An address nothing
  answers gives the last byte the bus carried, except where the expansion board says otherwise.
- **The expansion board** occupies `$9000`–`$BFFF` as
  [hardware/expansion-board.md](hardware/expansion-board.md) specifies.
- **Video.** The beam reads a character of video RAM and a row of its glyph in each cycle of a
  line's 40 visible ones, as the PET's logic and TangPet do, so that a write to video RAM, a
  change of VIA CA2's character set or of the expansion board's character source shows from the
  next character drawn.
- **Keyboard.** The graphics keyboard's 10 × 8 matrix, scanned through PIA1. A client presses
  PET keys by name, or types text that becomes the presses that make it
  ([0014](decisions/0014-keyboard-by-symbol.md)).
- **Sound.** VIA CB2, and the expansion board's two YM2149Fs, mixed to stereo at the machine's
  own rate. Resampling to the host rate is the frontend's job.
- **IEEE-488 and the SD2PET.** The bus is its eight lines of data and its handshake lines, each
  the AND of what both ends drive. The SD2PET is a device on it at address 8, with the timing the
  owner measured on the real one and the DOS of the firmware it runs
  ([0010](decisions/0010-sd2pet.md)). A directory on the host stands for its SD card.

There is no tape and no user port.

## The control API

Every capability is a method of `control::Dispatcher`
([0005](decisions/0005-one-control-api.md)), in groups:

- `emu.*`: status, reset, run frames or cycles, run until a condition, pause and resume the
  application's own run
- `memory.*`: read and write the address space and the RAMs
- `cpu.*`, `debug.*`: registers, disassembly, breakpoints, watchpoints, trace
- `video.*`: screenshot, and the screen as text
- `input.*`: PET keys pressed and held, and text typed
- `program.*`, `disk.*`: a program put into memory, a disk image mounted on the SD2PET, either
  run
- `audio.*`, `state.*`

The transports are thin, and none adds a capability of its own:

- JSON-lines on stdio (`petmcp-cli --server`) and TCP (`petmcp --server tcp:PORT`)
- MCP on stdio (`petmcp-cli --mcp`) and HTTP (`petmcp --mcp-http PORT`)

A request from a GUI-attached transport is executed on the emulation thread between two frames,
so the person at the GUI watches what the agent does. `petmcp-cli --transcript` answers recorded
requests and compares the answers with recorded ones; the protocol's own tests are such
transcripts ([spec/control-protocol.md](spec/control-protocol.md#transcripts)).

## The desktop frontend

`petmcp` is SDL3 with Dear ImGui (docking), the same frontend as PGMEmu's.

**Threads:**

- The core runs on its own emulation thread (`EmulationThread`). It runs frames at the PET's
  pace, and between two frames answers the requests queued for it. It hands each finished
  picture over under a lock, and its sound straight to the audio stream.
- The GUI thread never touches the core. It takes pictures and sends dispatcher requests through
  the queue, the same path the network transports use.
- Headless mode has one thread.

**Pacing:** the host's monotonic clock paces the emulation at the PET's 60.1 frames a second,
whatever the display's refresh rate. The sound goes to an SDL audio stream at the machine's rate,
with the dynamic rate control PGMEmu has.

**Debugger windows:** the 6502's registers and disassembly, memory, the VIA's and PIAs'
registers, the YM2149Fs, and the IEEE-488 bus.

## Save states

Each device writes a tagged chunk (`FOURCC`, version, size). A state is a header, the chunks, and
an optional thumbnail. Tagged chunks let a device's layout change without invalidating other
devices' data. The SD2PET's state holds its channels and buffers, and names the images it has
mounted; the card directory's files are not part of a state.

## Testing

1. **The CPU single-step suite:** SingleStepTests 65x02's NMOS 6502, undocumented opcodes
   included. It holds the core to the chip, bus cycle by bus cycle.
2. **Unit tests:** address decoding, the VIA's and PIAs' timers and edges against VICE's, the
   YM2149F, the bus handshake, and NODISKEMU's DOS against the images it reads.
3. **Traces against VICE:** the first second after power-up, instruction by instruction, against
   a trace VICE's xpet wrote of the same ROMs (`../PETTest/trace_1s.txt`).
4. **The owner's measurements on a real 3032** (`../PETTest/README.md`): the programs that made
   them run in the emulator, and the cycles they count are held to the hardware's: IRQ cost,
   retrace timing, and the SD2PET's transfer rates with each of the three receivers.
5. **Golden frames and transcripts:** scripted runs whose pictures, memory and sound are summed
   at checkpoints, to catch regressions.
