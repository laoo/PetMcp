# 0017: A save state leaves the SD2PET out

**Status:** accepted

## Context

[architecture.md](../architecture.md) planned that a save state would hold the SD2PET's channels
and buffers and name the images mounted. The DOS ported from NODISKEMU
([0010](0010-sd2pet.md)) holds more than that: files open on the host, positions in them, a BAM
held in a buffer and not yet written back, pointers between its buffers, and the state machine of
the firmware's bus loop. The files are outside the PET: a state loaded later, or on another
machine, would find them changed or gone, and an image written since would disagree with the BAM a
state held. Restoring all of it faithfully is a second save-state format, for the DOS alone, that
can still not promise what it restores.

## Decision

- A state holds both ends of the IEEE-488 bus, and nothing of the drive.
- Loading a state leaves the drive's DOS as it was before the load: its channels, the directory
  current on the card and the image entered there. The drive lets go of the bus and waits for the
  next ATN.

## Consequences

- A state saved and loaded while the PET is at READY, or between two disk operations, runs on as
  it would have: the drive answers the next one as before.
- A state saved in the middle of a transfer does not finish it: the PET waits for a byte that does
  not come, until its kernal's timeout or until a reset, which reaches the drive as IFC.
- The rule that loading a state, then running, gives exactly the frames that followed holds only
  for runs that do not use the drive.
