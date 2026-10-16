# The machine

Where the emulated PET knowingly departs from VICE, from TangPet or from the hardware, and the
evidence ([0002](../decisions/0002-references.md)). VICE's trace of a power-up
(`../PETTest/trace_1s.txt`, two million cycles) is the measure: `tests/machine/TraceTest.cpp`
holds the emulator to every instruction of it, its cycle and its registers, and it holds.

## Following VICE where the hardware would differ

**The first frame after power-up has no vertical blank.** VICE's CRTC emulation begins with its
display enable off (`crtc.c`, `venable`), which only the end of the first frame turns on. In that
frame the line PIA1 CB1 and VIA PB5 see never goes low, and the first retrace interrupt comes in
the second frame. A real PET's counters come up in some state and blank in the first frame. The
emulator follows VICE: the kernal's RAM test runs with interrupts on by then, and only VICE's
timing lets the whole trace be held to.

**The phase of the video against the 6502.** On a real PET it depends on how the counters come up.
The emulator has line 0 of the first frame begin at cycle 1, the 6502's first fetch at cycle 6,
and the blank begin at the end of the 40th cycle of line 199. The trace's interrupt entries fix
these: an edge a cycle either way would have the 6502 enter one of them a cycle off, the one
after a taken branch included.

**The PIAs' interrupts change a cycle later on a write than on a read.** VICE's PIA takes the
clock of a store as its CPU core holds it, a cycle past the access, and does not correct it as
its VIA does (`piacore.c`, `my_set_int`). The emulator keeps the difference.

**The stack pointer after power-up.** The 6502's reset takes 3 from S; VICE's leaves it at 0.
The kernal's second instruction sets it, so nothing sees the difference.

## Following TangPet where VICE has no answer

**A character set changed in the middle of a line.** VICE draws a line at its end, with the set
VIA CA2 chooses then. TangPet reads each glyph in its character's cycle, with the set chosen
before that cycle's access, and so does the emulator: a write to the PCR in a line's cycle `c`
changes the characters from `c + 1` on.

**A write to video RAM ahead of the beam.** The beam reads character `k` of a line in the line's
cycle `k`, after the 6502's access of that cycle, so a write in cycle `k` or before shows in
character `k`. TangPet has it so, and VICE's prefetch, which takes a write while the beam has not
reached it, agrees.

## Departing from all of them

**Video RAM at power-up.** VICE fills it at random. The emulator must be deterministic, so it
fills it with a fixed pseudo-random sequence: the garbage the screen shows before the editor
clears it is always the same. RAM holds VICE's default pattern without its random bits.
