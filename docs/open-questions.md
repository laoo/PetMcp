# Open questions

These questions are deferred on purpose. Each one is removed in the commit whose decision record
answers it. **This file only shrinks.**

## What a read of the expansion board's latch addresses gives

A read of `$9000` or `$9002` selects neither YM2149F
([hardware/expansion-board.md](hardware/expansion-board.md)). The board's author expects the
PET's pull-ups to give `$FF`; VICE and TangPet give the last byte the bus carried, which for
`LDA $9000` is `$90`. The emulator gives `$FF`. `PRINT PEEK(36864)` on the real PET answers it:
255 or 144.

## Which NODISKEMU the SD2PET runs

TFW8b does not say which build of NODISKEMU the SD2PET Future ships. The status it reports after
power-up names it: `OPEN1,8,15:INPUT#1,A,B$:PRINT B$` on the real PET. The DOS is ported from
that commit ([0010](decisions/0010-sd2pet.md)).

## The SD2PET's timing that was not measured

`openbench` (the OPEN of a named file, CHKIN, the first byte, CLOSE, and whether the drive
caches its directory) and `switch` (a `#` buffer and a file interleaved, and whether the
buffer's position survives) were built but not run on the hardware. Until they are, those
phases follow NODISKEMU's code.

## Why UNTALK costs 1230 cycles after CHRIN and 2250 after the other receivers

`../PETTest/README.md` §8 measures CLRCHN at 1230 cycles with the kernal's CHRIN and at 2250
with both receivers of its own. The device model must reproduce both; which state of the bus or
of the drive makes the difference is not known.

## The LOAD that hung after CD

On the hardware, `CD` sent through `PRINT#15` and followed by `LOAD"P",8` hung at LOADING;
opened with the command as its name, it worked (`../PETTest/README.md` §6). NODISKEMU's code
does not explain it. Is it the secondary address, the first access after `CD`, or how `CD` was
sent?

## The monitor's shape and colour

The 384 by 240 picture's pixels are not square on the PET's monitor, and its phosphor is green
or white depending on the year. Which shape and which colours does the Display menu offer, and
what is its default?
