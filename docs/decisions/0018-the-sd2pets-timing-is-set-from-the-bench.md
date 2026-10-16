# 0018: The SD2PET's timing is set from the bench

**Status:** accepted

## Context

[0010](0010-sd2pet.md) gives the emulated SD2PET a parameter per phase of its firmware, each to be
set from the owner's measurements (`../PETTest/README.md`, §8): the `bench` program, built with
the kernal's CHRIN, its own `acptr_fast` and an inline receiver, each averaging 64 sectors and a
16 KB file. The measurements are of whole phases as the PET sees them (the `U1` command, CHKIN,
CLRCHN, the bytes, the file), not of the firmware's steps, so a parameter can only be set where it
moves a measured phase.

Two of the numbers did not fit any parameter at first:

- **CLRCHN, 1230 cycles with CHRIN and 2250 with the other two receivers.** The bench reads timer
  2's low byte, then its high byte. When the low byte passes 0 between the two reads, `t_start`
  reads 256 too few, and an interval shorter than that, as UNTALK is, comes out negative: 65536
  more. One such sample of the 64 adds 1016 to the average, two add 2032. UNTALK itself takes
  214 cycles; 1230 and 2250 are that with one and two wrapped samples. The emulated PET's VIA
  wraps the same way: which samples wrap depends on the timer's phase, so the count differs
  between receivers and between runs, on the hardware and in the emulator alike.
- **The `U1` command, 375 cycles a character.** The PET sends a character in some 130 cycles when
  the drive keeps up, so the drive takes the rest. NODISKEMU's code shows no step that slow; with
  `CONFIG_UART_DEBUG` off its `printf` calls still reach avr-libc, which is one candidate.

## Decision

- The parameters are those [hardware/sd2pet.md](../hardware/sd2pet.md) lists, set so that the
  three receivers' counts come out as the hardware's: a data byte taken costs 320 cycles until
  the drive is ready for the next, a command on channel 15 runs in 400 (the most that still
  leaves CHKIN at 435), a file's next sector in 4000, and a `#` buffer starting over in 2.
- The data byte's cost stands for whatever makes the real drive slow to listen; it is not traced
  to a step of NODISKEMU's code.
- UNTALK is held to 214 cycles. Tests compare CLRCHN and the whole sector with the timer's
  wrapped samples taken out of both, the hardware's and the emulator's.
- Opening a file and closing a channel keep provisional times until `openbench` is measured.

## Consequences

- `tests/machine/BenchTest.cpp` runs the bench from `../PETTest` for each receiver and holds the
  counts to the hardware's: the command within 60 cycles, CHKIN within 15, the bytes and the
  file within 1 a byte, UNTALK within 12 and the sector within 1 %.
- A disagreement found later moves a parameter, or splits one, not the state machine.
- A receiver or a command the bench does not exercise (a long command, a SAVE) is timed by the
  same per-byte cost, which no measurement checks yet.
