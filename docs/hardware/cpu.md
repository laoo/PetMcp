# The 6502

Where the emulated 6502 knowingly departs from `m6502.h`, or `m6502.h` from the reference
([0003](../decisions/0003-cpu-core.md)), and the evidence. The SingleStepTests 65x02 suite's NMOS
6502 tests (`tests/cpu/Cpu6502SingleStepTest.cpp`, suite commit `2f6980a`) are the measure: every
case of every opcode, bus cycle by bus cycle.

## Corrected in the adapter

**SHA, SHX, SHY and TAS where the index carries into the high byte** (`$93`, `$9B`, `$9C`, `$9E`,
`$9F`). The NMOS 6502 stores the register ANDed with the base address's high byte plus one, and
the value it stores also replaces the high byte of the address it stores to. VICE models this
(`6510core.c`, `STORE_ABS_SH_*`), and the suite records it. `m6502.h` stores to the carried
address, and ANDs with that address's high byte plus one; about half of each opcode's cases
failed. `Cpu6502` redirects the instruction's one write cycle through its pins, and all cases
pass. Without a carry the two agree.

VICE also drops the AND when the CPU is halted by RDY in that instruction. Nothing on the PET pulls
RDY, so that case does not arise.

## Left as they are

**The bus cycles of JAM** (`$02`, `$12`, `$22`, `$32`, `$42`, `$52`, `$62`, `$72`, `$92`, `$B2`,
`$D2`, `$F2`). Both halt the CPU until a reset. The suite has it read `$FFFF`, `$FFFE`, `$FFFE`
and then `$FFFF` for ever; `m6502.h` reads `$FFFF` from the start. On the PET both addresses are
the kernal ROM, which a read does not disturb, so nothing outside the CPU can tell the
difference. Every case of the twelve files differs, and the test holds that count.
