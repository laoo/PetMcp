# Milestones

This file sets the order in which the emulator described in [architecture.md](../architecture.md) is
built. Each entry is one milestone, made of session-sized tasks.

**This file only shrinks.** A milestone is removed in the commit that completes it. A task that
needs a decision the records do not hold stops, and puts the question in
[open-questions.md](../open-questions.md) instead of deciding it.

A session takes work by saying *build M<n> of docs/plans/milestones.md*. Every task ends with:

- the suite green;
- `./scripts/format.sh --check` and `./scripts/tidy.sh` clean;
- documentation updated in the same commit;
- no commit until the owner has reviewed.

Every milestone ends with an **exit** condition that can be checked without a person looking at
a screen.

## M1: The 6502

- Vendor `m6502.h` and `m6502dasm.h` from floooh/chips under `libextern/chips`, with their
  licence and an entry in `libextern/README.md` ([0003](../decisions/0003-cpu-core.md)).
- An adapter around it: registers, a tick, the pins, and disassembly.
- `scripts/fetch-6502-tests.sh` fetches the SingleStepTests 65x02 NMOS 6502 suite into
  `build/tools/6502`; a `[cpu-suite]` test runs every opcode's cases, bus cycle by bus cycle,
  undocumented ones included, and skips without the data.

**Exit:** every case of the suite passes, or each one that does not is listed in
`docs/hardware/` with the reason it does not matter or the record that decides it.

## M2: The PET

- The machine: RAM, video RAM and its mirror, the ROMs read from the ROM directory, the I/O page
  decoded as VICE's `petmem.c` does, the floating bus.
- PIA1, PIA2 and the VIA, ported from VICE's `piacore.c` and `viacore.c` and stepped a cycle at
  a time ([0009](../decisions/0009-stepped-a-cycle-at-a-time.md)); unit tests of their timers
  and edges against what VICE computes.
- The video: the beam, the retrace on PIA1 CB1 and VIA PB5, the character ROM and VIA CA2's set,
  drawn into a 384 by 240 picture.
- The 60 Hz interrupt, and the 6502's interrupt timing held to VICE's.
- Methods: `emu.reset`, `emu.run_frames`, `emu.run_cycles`, `emu.run_until`, `memory.read`,
  `memory.write`, `cpu.get_state`, `cpu.disassemble`, `video.screenshot`, `video.screen_text`
  (the 40 by 25 screen codes as text), and the fields of `emu.status` that describe the machine.
- A test runs the first second after power-up against `../PETTest/trace_1s.txt`, instruction by
  instruction, and skips without it.
- A `[roms]` test boots to `READY.` with the ROMs of `../vice/data/PET`, and skips without them.

**Exit:** the trace matches; `video.screen_text` shows the boot banner and `READY.`; the
`irqcost` and `vsync` programs of `../PETTest`, run from a test, count what README §11–12
measured on the real PET.

## M3: Keyboard, programs and the desktop

- The keyboard matrix; `input.press`, `input.set`, `input.clear`, `input.get_state` by PET key
  name, and `input.type` for text ([0014](../decisions/0014-keyboard-by-symbol.md)).
- `program.load`: a `.prg` put into memory at its load address, BASIC's pointers set as LOAD
  sets them, and run when asked (`RUN` for BASIC, a `SYS` otherwise).
- The desktop: frames run at 60.1 Hz, the host keyboard mapped by symbol, the Input window and
  its bindings, the monitor's phosphor colour and pixel shape in the Display menu, File > Open
  and drag-and-drop for a `.prg`.
- The `kbscan` program of `../PETTest` as a test.

**Exit:** a transcript types `PRINT 2+2` and a return, runs a few frames, and reads ` 4` on the
screen; another loads a `.prg` and runs it.

## M4: The debugger and save states

- `debug.breakpoint.*`, `debug.watchpoint.*`, `debug.trace`; a run stops at them, and the
  window pauses.
- Windows: CPU (registers and disassembly), memory, VIA and PIAs.
- `state.save`, `state.load`, `state.list`: tagged chunks per device; rewind in the desktop.

**Exit:** a state saved mid-program and loaded runs on to the same frame sums as the run that
saved it.

## M5: Sound and the expansion board

- VIA CB2 sound, as VICE's `petsound.c` makes it.
- Two YM2149Fs, with register read-back, 32-step envelopes and the YM's levels, at 2 or 1 MHz.
- The expansion board as [hardware/expansion-board.md](../hardware/expansion-board.md)
  specifies: decoding, the character RAM shown by the video, the 512 KB RAMs through their
  windows.
- Audio output in the desktop; `audio.capture_start`, `audio.capture_stop`; a sound window with
  the YM2149Fs' registers.

**Exit:** tests of the board's decoding, banking and character source after a reset; a WAV of a
known tone sequence on each chip matches the expected frequencies and levels.

## M6: IEEE-488 and the SD2PET

- The bus: data and handshake lines as the AND of both ends, wired to PIA2 and the VIA as VICE
  wires them.
- The SD2PET as a device on it ([0010](../decisions/0010-sd2pet.md)): listener and talker, ATN,
  EOI, with provisional timing.
- NODISKEMU's DOS, ported: the error channel, commands, listings, images (D64, D80, D82, D81,
  D71, DNP), files on the card directory.
- Methods: `disk.mount`, `disk.unmount`, `disk.status`, `disk.run` (mount, then `LOAD` of the
  first or a named file and `RUN`), `disk.command` (a command on channel 15, and the status it
  leaves); `ieee.trace` (the bus's line changes, cycle-stamped). The same in the desktop: File >
  Open of an image, and drag-and-drop.

**Exit:** a transcript mounts a D64, lists `$` on the PET's screen, and runs a program from it
with `disk.run`.

## M7: The SD2PET's timing

- Each phase's parameter set from `../PETTest/README.md` §8.
- The `bench` program, built for each of its three receivers, runs from a test against its own
  D64; the per-byte, per-sector, `U1`, CHKIN and CLRCHN counts are held to the hardware's.
- `openbench` and `switch` run as tests too, against expectations to be filled in when their
  hardware numbers are measured ([open questions](../open-questions.md)).

**Exit:** every count measured on the hardware is reproduced within the spread the hardware's
own runs show.

## M8: Regression and release

- Batch scripts with checkpoints summing the picture, memory and sound, and a regression suite
  of them (`ctest -L regression`).
- The project skill, `.claude/skills/petmcp/SKILL.md`, for driving the emulator from an agent.
- CI green on three platforms; a tagged release.

**Exit:** CI publishes a release from a tag.
