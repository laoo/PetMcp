# The SD2PET

How the emulated SD2PET Future is built, and where it knowingly departs from the hardware or from
NODISKEMU ([0010](../decisions/0010-sd2pet.md)). What the owner measured on the real one is
`../PETTest/README.md`, §§5, 6 and 8; this document does not repeat it.

## What is ported

The DOS is NODISKEMU at commit `e51a6df` (2026-09-18), built as the petSD+ is
(`configs/config-petSD+`), the board the SD2PET descends from. It reports itself as the build of
that commit would: `73,NODISKEMU V1.0.0ALPHA0-20260918-E51A6DF,00,00` after power-up, and
`-M1284P-PETSD+` in message 9. Which build the owner's SD2PET runs is an
[open question](../open-questions.md).

The bus handling is NODISKEMU's `ieee.c` and the petSD+'s ATN interrupt, as a state machine
stepped with the PET's cycles. Every loop in which the firmware polls a line is a state; the
device acts on a line a fixed number of cycles after it changes, as the firmware's poll would.
The DOS runs whole when the firmware calls it, and the device then stays busy for a time given
per kind of call (a command, an OPEN, a sector, a CLOSE), as the ATmega would be; its ATN
interrupt still answers meanwhile.

## Timing

Each phase's time, in cycles, as [0018](../decisions/0018-the-sd2pets-timing-is-set-from-the-bench.md)
set them from the owner's `bench`; `disk.timing` reads and changes them.

| Phase | Cycles | Set by |
|---|---|---|
| ATN to the interrupt's answer | 2 | the firmware's interrupt |
| A polled line to the next action | 2 | the bench's inline receiver, 58 a byte |
| A command byte under ATN | 5 | CHKIN, 435 |
| A data byte received, until ready for the next | 320 | the `U1` command, 4852 to 4872 |
| The next byte to send | 3 | the bench's inline receiver |
| A command on channel 15 | 400 | CHKIN, which a longer command would delay |
| Opening a file, a directory or a buffer | 2000 | not measured |
| A file's next sector | 4000 | the sequential file, 74 to 167 a byte |
| A `#` buffer starting over | 2 | the sector's second half, as fast as its first |
| Closing a channel | 500 | not measured |

## The wiring

The PET's end follows VICE (`petpia1.c`, `petpia2.c`, `petvia.c`): PIA2's port A reads the data
lines and port B drives them, CA2 drives NDAC and CB2 DAV; the VIA's PB1 drives NRFD and PB2 ATN,
and PB0, PB6 and PB7 read NDAC, NRFD and DAV; PIA1's PA6 reads EOI and CA2 drives it. Two things
VICE leaves out are wired as on the PET's board:

- **ATN reaches PIA2's CA1.** The kernal does not enable its interrupt, so nothing changes for
  it; a program that does sees ATN's edges.
- **The PET's reset reaches the drive as IFC**, which the petSD+ watches: on a reset the drive
  closes every channel and its error channel says 73 again, as `ieee488_ProcessIFC` does. The
  directory, and the image entered, stay as they were.

## The card

A host directory stands for the SD card. NODISKEMU's FatFs is replaced by a layer with FatFs's
calls over the host's file system, so that the DOS above it is unchanged:

- **A directory is known by a number** where FatFs has its start cluster: 0 is the card's root.
  An image mounted from outside the card (`disk.mount`) makes its directory current; that
  directory's parent is the card's root.
- **Every file has an 8.3 name**, as on FAT: its own when it is one, else one made up as FAT
  makes them (`NAME~1.EXT`). A listing shows the long name when it fits, as NODISKEMU does, and
  names are looked up case-insensitively by either.
- **Entries are listed sorted by their host names.** FAT lists them in the order they were
  written to the card, which a host directory does not keep.
- **A host name beginning with `.` is hidden**, as macOS marks such files on a FAT card.

## Departures

**A save state leaves the drive out**
([0017](../decisions/0017-save-states-leave-the-drive-out.md)).

**Where the firmware would loop for ever, the drive stops.** A full D82 sends NODISKEMU's search
for a free track round for ever (its counter is 8 bits wide). The emulated drive stops there as
the ATmega would: only its ATN interrupt answers, until the PET is powered up again. The
emulator itself goes on. A read or write past an array, which the ATmega does without a word,
stops it the same way.

**No real-time clock.** The petSD+ has one; the emulated drive finds none, so the `T` commands
answer 30, and a directory entry written in an image is dated 1982-08-31 00:00, the date
NODISKEMU's clock drivers fall back to.

**What the ATmega's memory would hold.** Where NODISKEMU reads memory it never set (the link
byte of `$=P`'s lines, `M-R` reading past its 100-byte buffer), the emulated drive gives zeros.

**Left out:** fast loaders and their detection, the IEC bus, the LCD, the buttons, the EEPROM
(its file system and the configuration `XW` would save), disk swap lists, and the `D` commands
that read the SD card's own sectors, which answer 74.
