---
name: petmcp
description: Boot, run, type on, inspect and debug the Commodore PET 3032 in PetMcp, the emulator in this repository, with its SD2PET drive and the owner's expansion board. Use it to run BASIC or machine code headless, load and run programs or disk images, read the screen, memory and registers, stop at breakpoints or watchpoints, search the history of execution, watch the IEEE-488 bus, sound the YM2149Fs, save and load states, or run and record batch scripts.
---

# Driving PetMcp

PetMcp answers a JSON control protocol (`docs/spec/control-protocol.md`, every method with its
parameters). Run it headless as a server that keeps its state, and send it one request per
command with `scripts/petmcp.py`. Work from the repository root.

## Start it

Build once, then start the server in the background (Bash with `run_in_background`):

```sh
cmake --build --preset release
build/release/src/pet_cli/petmcp-cli --server tcp:7701 --rom-dir ../vice/data/PET --card-dir ~/PetCard --state-dir build
```

The ROMs are VICE's (`../vice/data/PET`). `--card-dir` is the directory that stands for the
SD2PET's SD card; leave it out for no card. `--no-expansion` takes the expansion board out. When
done, stop the one you started, by its port: `pkill -f "petmcp-cli --server tcp:7701"`.

If 7701 is taken, pick another port and pass it to every request as `--port PORT`, written out on
each call (zsh does not split a command kept in a variable).

A person may have the desktop application open instead, started with `--server tcp:7701`: then
the same requests drive the PET on their screen, and you must not start a second server. The
window runs the PET by itself between your requests: read what you need right after the run that
stops where you want it, or `emu.pause` it first (`emu.resume` when done). A breakpoint or a
watchpoint its own run reaches pauses it, and `emu.status` then says where it stopped. Do not
change what the person is running without being asked: try things on a headless server of your
own.

To have the emulator as MCP tools in your own sessions instead, register it for yourself (it is
not committed, as each session would then run the binary of its start):
`claude mcp add --scope local petmcp -- "$PWD/build/release/src/pet_cli/petmcp-cli" --mcp --rom-dir "$PWD/../vice/data/PET"`.
Reconnect it (`/mcp`) after a rebuild.

## Send requests

```sh
scripts/petmcp.py emu.run_until screen_contains=READY.
scripts/petmcp.py input.type text='print 2+2\n'
scripts/petmcp.py emu.run_frames count=10
scripts/petmcp.py video.screen_text
scripts/petmcp.py video.screenshot path=build/shot.png     # then Read build/shot.png to see it
```

Parameters are `key=value` (JSON values; `0x...` is hex) or one JSON object. In raw JSON an address
may be a string `"$E840"` or `"0xE840"`. The answer is the result as JSON; an error goes to stderr
with exit status 1. Numbers in answers are decimal.

## What to ask for

| To | Methods |
|---|---|
| See where it is | `emu.status`, `video.screen_text` (25 lines of 40), `video.screenshot {path}` |
| Run | `emu.run_frames {count}` (60 is a second), `emu.run_cycles {count}` (1 MHz cycles), `emu.run_until {pc, screen_contains, timeout_cycles}`, `emu.step {count}` (instructions) |
| Reset | `emu.reset` (the reset line; RAM kept), `emu.reset {power: true}` (off and on), `emu.set_rom_directory {path}` |
| Type | `input.type {text}`: letters of either case are the unshifted keys, `\n` is RETURN, `{CLR}`, `{HOME}`, `{STOP}`, `{SHIFT-A}` and the like in braces; `input.press {key, shift}`, `input.set {key, down}`, `input.clear` |
| Programs | `program.load {path or data_hex, run}`: a `.prg` into RAM as LOAD does, RUN or SYS typed with `run` |
| Disks | `disk.card {path}`, `disk.mount {path}` (D64, D71, D81, D80, D82, DNP from anywhere), `disk.run {path, file}` (mount, LOAD, RUN), `disk.command {command}` (channel 15: `cd:name`, `s:name`, `u1 2 0 18 0`), `disk.status`, `disk.unmount` |
| The bus | `ieee.trace {count, from}`: the IEEE-488 lines' changes, newest first; `disk.timing` |
| Stop at code | `debug.breakpoint.add/remove/list {address}`: a run stops before that instruction |
| Stop at data | `debug.watchpoint.add {address, size, access: read/write/access}` (`access` for both): a run stops after the instruction, and its answer says what was accessed and from where |
| See what ran | `debug.trace {count, from}`: index 0 is the newest instruction; `debug.trace.find {pc, pc_start, pc_end, disasm_contains, a, x, y, s, from, limit}`; `debug.trace.configure {seconds}` |
| CPU | `cpu.get_state`, `cpu.disassemble {address, count}` |
| Memory | `memory.list_regions`, `memory.read {address, size, region}`, `memory.write {address, data_hex, region}` (RAM, VIDEO_RAM, the expansion board's RAMs) |
| I/O chips | `io.get_state`: the PIAs' and the VIA's registers, the VIA's timers |
| Expansion board | `expansion.get_state`: both YM2149Fs' registers, the banks at `$A000` and `$B000`, the character source |
| Sound | `audio.capture_start`, `audio.capture_stop {path}` (a WAV) |
| State | `state.save {filename}`, `state.load {filename}`, `state.list` |
| The window | `emu.pause`, `emu.resume`: the PET on screen stops running by itself |

A run answers `reason`: `completed`, `breakpoint`, `watchpoint`, `condition_met` or `timeout`,
with the cycles and frames it took. After a breakpoint, the next run executes that instruction
before stopping again; a breakpoint in code that runs every frame (the retrace interrupt, entered at
`$E61B`) stops every run within a frame, so remove it to run on.

## The PET's habits

- After power-up it takes some 90 frames to READY.: `emu.run_until {screen_contains: "READY."}`.
- BASIC's input line is 80 characters, two screen lines: type longer commands as several lines.
- The keyboard buffer holds 10 keys; `input.type` types a key at a time, so it has no limit.
- The SD2PET is device 8 and answers 73 after power-up and after the PET's reset. BASIC 2's
  LOAD of a file the drive cannot open hangs at LOADING, as on the hardware; `disk.status` then
  says why (`62,FILE NOT FOUND`), and `disk.run` stops it with STOP.
- The expansion board's YM2149Fs are at `$9000`/`$9001` (AY1, latch then data) and
  `$9002`/`$9003` (AY2); `docs/hardware/expansion-board.md` has the rest. Programs written for the
  older board, with the chips at `$A000`, are silent here.

## Batch scripts

A run written down, with checkpoints summed against golden sums
(`docs/spec/batch.md`):

```sh
(cd build/release/tests && ../src/pet_cli/petmcp-cli --batch ../../../tests/regression/basic.json --rom-dir ../../../../vice/data/PET)
ctest --preset release -L regression
```
