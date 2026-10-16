# 0016: The screen is green, its dots square, unless chosen otherwise

**Status:** accepted

## Context

The core draws the PET's dots lit or not ([spec/control-protocol.md](../spec/control-protocol.md),
`video.screenshot`): the colour and the shape the monitor gave them are the frontend's. The 3032's
monitor had a green phosphor, the 2001's a white one, and amber monitors were fitted too. On the
monitor the 40 by 25 characters filled a roughly 4:3 area, which makes its dots taller than wide;
emulators commonly show them square.

## Decision

- The Display menu offers the phosphor, green, white or amber, and the dots' shape, square (the
  picture 8:5) or as the monitor had them (the picture 4:3). Both are kept with the display
  settings.
- The defaults are green and square: the 3032's colour, and the shape that scales the 320 by 200
  dots to whole multiples without resampling.

## Consequences

- `video.screenshot` stays white on black whatever the window shows: what an agent compares does
  not depend on a person's taste.
