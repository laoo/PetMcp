# Batch scripts

A batch script is a run of the emulator written down: requests of the control protocol
([control-protocol.md](control-protocol.md)), and checkpoints at which the machine is summed.
`petmcp-cli --batch FILE` runs one headless and unthrottled. The regression suite is such
scripts, each holding the sums it was recorded with: `tests/regression/*.json`.

## The file

A JSON object:

| Field | Meaning |
|---|---|
| `description` | What the script runs, for people. Optional. |
| `steps` | The steps, in order. |
| `golden` | The sums each checkpoint was recorded with, by checkpoint name. Written by `--record`. |

A step is one of:

- a request: `{"method": "emu.run_frames", "params": {"count": 600}}`, as the protocol has it,
  without an `id`;
- a checkpoint: `{"checkpoint": "ready"}`.

Paths in requests (`disk.card`, `disk.run`, `program.load` ...) are relative to the working
directory.

```json
{
  "description": "Power-up to READY.",
  "steps": [
    { "method": "emu.run_frames", "params": { "count": 120 } },
    { "checkpoint": "ready" }
  ],
  "golden": {
    "ready": { "frame": 120, "picture": "01443f06", "ram": "07861d2a", "video_ram": "3f834983",
               "expansion_ram0": "75660aac", "expansion_ram1": "75660aac", "character_ram": "c71c0011",
               "sound": "900c106d", "drive": "73,NODISKEMU V1.0.0ALPHA0-20260918-E51A6DF,00,00" }
  }
}
```

## Checkpoints

At a checkpoint the machine is summed:

| Sum | Of |
|---|---|
| `frame` | Frame boundaries passed since power-up. |
| `picture` | The last complete picture, 320 by 200 RGBA. |
| `ram`, `video_ram` | The 32 KB of RAM and the 1 KB of video RAM. |
| `expansion_ram0`, `expansion_ram1`, `character_ram` | The expansion board's memories, the regions `memory.read` names so; absent without the board. |
| `sound` | Every stereo frame made since the checkpoint before (or since power-up): left, then right, 16-bit little-endian. |
| `drive` | The SD2PET's error channel, as `disk.status` gives it: text, not a sum. |

Each sum is a CRC-32 as eight lowercase hex digits. The emulator is deterministic, so a script's
sums change only when what it emulates does; the host files a script's drive reads are part of
what it emulates. A power-up (`emu.reset` with `power`) starts the sound afresh.

## Running

```sh
petmcp-cli --batch FILE --rom-dir ../vice/data/PET            # check against golden
petmcp-cli --batch FILE --rom-dir ../vice/data/PET --record   # write golden into FILE
```

It prints a line per checkpoint that differs from its golden sums, or one that says they all
match, and exits:

| Status | When |
|---|---|
| 0 | Every checkpoint matches. |
| 1 | A checkpoint differs from its golden sums, has none, or a golden one was not reached. |
| 2 | A step failed, or the file is not a script. |
| 77 | The PET's ROMs, or a file a step names, are not there: CTest's code for a skipped test. |

`--record` overwrites the file's `golden` with the sums of this run. Golden sums are this
emulator's own, so they catch a change, not an error: before recording new ones, see that the run
does what its description says.

## The regression suite

`ctest -L regression` runs every script in `tests/regression/` from the build's tests directory,
where `pet_test_disk` has written the test disk into `disk` and `disk-rw`, with VICE's ROMs from
`../vice/data/PET`. `pettest.json` runs the owner's programs from `../PETTest`, and is skipped
without it.
