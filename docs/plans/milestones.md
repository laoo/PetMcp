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
