# 0003: The 6502 is floooh's m6502.h

**Status:** accepted

## Context

The PET's CPU is an NMOS 6502 at 1 MHz. The emulator must reproduce it as VICE does
([0002](0002-references.md)): every bus cycle in its place, dummy reads and the double write of
read-modify-write instructions included, all undocumented opcodes, and the interrupt timing:
IRQ seen at the end of an instruction's next-to-last cycle, one instruction's delay after CLI,
SEI and PLP, and the taken branch that does not poll. Programs the owner measured with, such as
`irqcost`, count those cycles.

Two ways were weighed:

- **A port of VICE's `6510core.c`.** It is the reference, but it is a block of macros included
  into each machine's CPU file, built around VICE's alarm and interrupt structures. It steps an
  instruction at a time and places each access by adjusting the clock.
- **[floooh/chips](https://github.com/floooh/chips) `m6502.h`.** The same library as the Z80 core
  in PGMEmu: cycle-stepped and pin-based, one bus access per tick, undocumented opcodes included,
  validated by its author against the SingleStepTests 65x02 suite. zlib.

## Decision

- The 6502 is `m6502.h`, with `util/m6502dasm.h` for disassembly, vendored unmodified under
  `libextern/chips` ([0006](0006-dependencies.md)).
- It is held to VICE by tests, not trusted: the SingleStepTests 65x02 NMOS suite, a trace of the
  first second after power-up against one VICE wrote (`../PETTest/trace_1s.txt`), and the
  owner's `irqcost` measurement. An interrupt timing it gets wrong is corrected in the adapter
  around it, through its pins, and recorded in `docs/hardware/`.

## Consequences

- A tick is a cycle, which is what [0009](0009-stepped-a-cycle-at-a-time.md) steps the machine
  by; there is no clock to adjust after the fact.
- If the adapter cannot bring an interrupt quirk into line, a port of VICE's core replaces it,
  under a new record.
