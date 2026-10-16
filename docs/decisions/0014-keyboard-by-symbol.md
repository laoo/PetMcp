# 0014: The host keyboard is mapped by symbol

**Status:** accepted; its rule for letters, and where the character is read, amended by [0015](0015-letters-keep-the-pets-shift.md)

## Context

The PET 3032 has the graphics keyboard: its keys stand where no modern keyboard has them, and
many symbols that need SHIFT on a host keyboard have keys of their own (`"`, `@`, `[`, `(`). A
key can be mapped by position, the host key in the same place pressing the PET's, or by symbol,
the character the host key makes pressing what makes it on the PET.

## Decision

- The host keyboard is mapped by symbol: the character a key makes on the host is made on the
  PET, by pressing its key, with or without SHIFT as the PET needs, whatever the host's SHIFT
  was. The symbol is taken from SDL's text input, so the host's layout is honoured.
- Keys that make no host character (RUN/STOP, CLR/HOME, RVS, the cursor keys, INST/DEL) and the
  PET's own symbols (`←`, `↑`, `π`) have bindings of their own, shown and changed in the Input
  window.
- `input.type` takes text and types it the same way, a key at a time, held long enough for the
  editor's scan; `input.press` and `input.set` press PET keys by name, for what a symbol does
  not say, such as SHIFT held over a graphic.

## Consequences

- What a person types is what appears, on any host layout.
- A program that reads the matrix directly, such as a game holding two keys, needs the PET keys
  by name; the Input window can bind host keys to them by position for that.
