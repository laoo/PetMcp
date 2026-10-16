# 0004: Licence is GPL-2.0

**Status:** accepted

## Context

The owner chose to port code from VICE (GPL-2.0-or-later) where that is the shortest way to its
behaviour, and from NODISKEMU (GPL-2.0, as sd2iec it forks) for the SD2PET's DOS. A port is a
derivative work.

## Decision

PetMcp is released under GPL-2.0.

- Code ported from VICE or NODISKEMU names the file and the commit it was read at in its header.
- TangPet is the owner's. Its DOS firmware (`fw/dos`) and its `ieee_dev.v` and `ay8910.v` were
  written for it, with VICE as a reference for formats; Thomas Skibo's 6502, PIA and VIA cores
  in it are BSD. Any of them may be drawn on; the BSD ones keep their notices.
- Third-party libraries are MIT, zlib or BSD, and compatible ([0006](0006-dependencies.md)).

## Consequences

- `THIRD_PARTY.md` credits VICE and NODISKEMU and carries what their licences ask.
- Moving to GPL-3 would need NODISKEMU's code to be "or later", which sd2iec's is not.
