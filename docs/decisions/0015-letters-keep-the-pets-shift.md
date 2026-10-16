# 0015: A letter typed on the host keeps the PET's SHIFT, and the host's layout names the character

**Status:** accepted; amends [0014](0014-keyboard-by-symbol.md)

## Context

[0014](0014-keyboard-by-symbol.md) maps the host keyboard by symbol, the character a key makes on
the host made on the PET whatever the host's SHIFT. Letters do not fit that rule. In the graphics
set, which the 3032 starts in, an unshifted letter key makes a capital and a shifted one a
graphic; in the text set they make a lower-case letter and a capital. The host makes two
characters of a letter key, and the PET two, but the pairs do not correspond in the graphics set:
a host capital made on the PET as a capital would leave the graphics no host key could reach.

[0014](0014-keyboard-by-symbol.md) also took the character from SDL's text input. The application's
ImGui backend stops SDL's text input whenever none of ImGui's own fields wants text, so none
arrives while the PET's screen has the keyboard.

## Decision

- **On the host keyboard** a lower-case letter presses the PET's letter key, and a capital
  presses it with SHIFT. A person types as at a PET: capitals in the graphics set without SHIFT,
  graphics with it; in the text set what is typed is what appears. Every other character follows
  [0014](0014-keyboard-by-symbol.md).
- The character a key makes is read from the host's layout as SDL maps it, with SHIFT, Caps Lock,
  AltGr and Alt/Option held (`SDL_GetKeyFromScancode`), when the key goes down. The host's layout
  is honoured as before, and the key that made a character is the key whose release lets it go.
- **In `input.type`** a letter of either case presses the unshifted key, so that an agent writing
  `PRINT` or `print` gets the command it means in the graphics set; a shifted key is written
  `{SHIFT-A}`.

## Consequences

- With Caps Lock on, the host types graphics.
- A text written for `input.type` reads as the program it types, whatever its case.
