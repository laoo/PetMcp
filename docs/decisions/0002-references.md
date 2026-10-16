# 0002: What decides how the machine behaves

**Status:** accepted

## Context

The emulator must be cycle-exact. Four sources describe how the PET 3032 and what is attached to
it behave, and they do not always agree:

- **VICE** (`../vice`, 3.10 with the owner's commits), whose PET emulation is the most complete
  in software, and whose 6502 is a real NMOS 6502 down to its interrupt timing.
- **TangPet** (`../TangPet`), the owner's FPGA PET. Its CPU core departs from a real 6502: it
  polls IRQ a cycle late, lets CLI and SEI act at once, has no undocumented opcodes and resets in
  two cycles. Its video fetch and its expansion board are the owner's design.
- **The owner's measurements on a real 3032 with an SD2PET Future** (`../PETTest/README.md`).
- **NODISKEMU** (https://github.com/nils-eilers/NODISKEMU), the firmware the SD2PET runs.

## Decision

- **A measurement on the real machine decides**, where there is one.
- **Otherwise VICE decides for the PET**: the 6502's behaviour, the memory map and its I/O
  decoding, the PIAs and the VIA, the video timing of a PET without a CRTC, the keyboard matrix
  and the IEEE-488 wiring. TangPet's CPU departures are not followed; TangPet is to be corrected
  towards the real 6502, not the emulator towards TangPet.
- **The expansion board** is [hardware/expansion-board.md](../hardware/expansion-board.md). Where
  it is silent, TangPet's `pet3032_core.v` decides.
- **The SD2PET's DOS** is NODISKEMU's ([0010](0010-sd2pet.md)). Its timing is the owner's
  measurements.
- Where the emulator knowingly departs from the source that decides, `docs/hardware/` says so,
  with the evidence.
- These checkouts are read-only references, never edited from here.

## Consequences

- A difference from VICE is a bug in the emulator unless a measurement or a record says
  otherwise.
- The owner's measurements become tests. Where VICE disagrees with a measurement, the emulator
  follows the measurement and `docs/hardware/` records the difference.
