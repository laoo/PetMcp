# The expansion board

The owner's expansion board for the PET 3032 sits in the `$9000`–`$BFFF` ROM sockets. It carries
two YM2149F sound chips, 4 KB of character RAM the video can show instead of the character ROM,
and two 512 KB RAMs seen through 4 KB windows. This document is the board's specification as the
emulator implements it. It is newer than the board TangPet implements
(`../TangPet/PLAN.md`), which put the same functions at other addresses; where the two differ,
this document holds.

## Address decoding

The board decodes the PET's `/SEL9`, `/SELA` and `/SELB` (each a 4 KB block) and the low
address lines. Nothing else of the address is decoded, so every register repeats through its
block.

| Address | A11 | What answers |
|---|---|---|
| `$9000`–`$97FF` | 0 | The YM2149Fs: A1 picks the chip, A0 the latch or the data (below) |
| `$9800`–`$9FFF` | 1 | Character RAM: the 2 KB buffer the video does not show |
| `$A000`–`$AFFF` | | RAM0: the 4 KB bank AY1's port A picks |
| `$B000`–`$BFFF` | | RAM1: the 4 KB bank AY2's port A picks |

The YM2149F registers, repeating every 4 bytes through `$9000`–`$97FF`:

| A1 A0 | Address | Read | Write |
|---|---|---|---|
| 0 0 | `$9000` | nothing drives the bus | AY1's register latch |
| 0 1 | `$9001` | AY1's register selected | AY1's register selected |
| 1 0 | `$9002` | nothing drives the bus | AY2's register latch |
| 1 1 | `$9003` | AY2's register selected | AY2's register selected |

- Reading a latch address selects no chip. The board's author expects the PET's pull-ups to
  give `$FF`, and the emulator answers `$FF` ([open question](../open-questions.md)).
- Registers read back in full, as a YM2149F gives them: all 8 bits of every register, unused
  bits included. An AY-3-8910 masks them; this board has none.

## The YM2149Fs

- Each chip has its own 2 MHz oscillator. A divider, chosen per chip, halves it to 1 MHz (AY1's
  port B, below).
- AY1 is the left channel and AY2 the right. The PET's own sound, VIA CB2, goes to both.

## The ports

The chips' I/O ports drive the board. A port set as an input (register 7, bits 6 and 7, as they
are after a reset) is pulled up inside the chip, so the board sees `$FF` on it.

**AY1 port A, bits 0–6:** the 4 KB bank of RAM0 at `$A000`. **AY2 port A, bits 0–6:** the bank of
RAM1 at `$B000`. Bit 7 of each is unused. 128 banks of 4 KB make each RAM's 512 KB.

**AY1 port B:**

| Bit | 0 | 1 |
|---|---|---|
| 0 | the video shows character buffer 0; the CPU sees buffer 1 at `$9800` | the video shows buffer 1; the CPU sees buffer 0 |
| 1 | characters are drawn from the shown buffer | characters are drawn from the ROM, the PET's own set |
| 2 | AY1 runs at 1 MHz | AY1 runs at 2 MHz |
| 3 | AY2 runs at 1 MHz | AY2 runs at 2 MHz |

AY2's port B is unused.

After a reset both ports are inputs, so the board starts as a plain 3032: characters from the
ROM, bank `$7F` in both windows, both chips at 2 MHz.

## The character RAM

Two buffers of 2 KB, each laid out as the character ROM is: the graphics set in the first 1 KB
and the text set in the second, which VIA CA2 chooses between as it does on the ROM. Each
character is 8 bytes, a row each, its leftmost dot in bit 7; bit 7 of the screen code inverts it,
as for the ROM. The video reads the RAM at the same moment of each character as it reads the ROM
(TangPet's `pet3032_core.v`), so a change of AY1's port B shows from the next character drawn.

The character RAM and both 512 KB RAMs hold zeros at power-up, as TangPet's do; the character
RAM is not copied from the ROM.

## Reset

The emulator resets the YM2149Fs at power-up only: the PET's reset line leaves their registers,
and so the banks, the character source and the sound, as they were. The board this one replaces
had its chips' /RESET on an RC network (the owner's VICE `petay.c`); whether this one does is an
[open question](../open-questions.md).
