# 0009: The machine is stepped a cycle at a time

**Status:** accepted

## Context

Everything in the PET runs from one clock, and what programs measure depends on the order of
events within a cycle: which character the beam is drawing when video RAM is written, the cycle
a VIA timer underflows on, when the retrace edge reaches PIA1, how many cycles the SD2PET's
handshake takes. VICE keeps this order with alarms: devices compute their state lazily from the
clock, and the CPU dispatches the alarms due between instructions, with offsets that place each
access.

At 1 MHz the machine makes a million cycles a second. Stepping each device every cycle costs a
few function calls per cycle, well within budget on any host the frontend runs on.

## Decision

- The machine advances one CPU cycle at a time. In each cycle the 6502 makes its one bus access
  ([0003](0003-cpu-core.md)), then the devices step: video, the PIAs and the VIA, the YM2149Fs,
  the IEEE-488 bus and the SD2PET.
- Ports from VICE's alarm-driven devices keep VICE's behaviour, cycle for cycle, but compute it
  in the cycle it happens. A device's tests compare it with what VICE computes for the same
  sequence of accesses.
- Within a cycle, the order is fixed and documented in the machine's code: what a device sees
  of another's lines in the same cycle is the one choice that decides who wins a race.

## Consequences

- There is one clock to reason about and no catch-up, so a timing difference has one place to
  be found.
- The emulator runs slower than an alarm-driven one would, which leaves fast-forward less
  headroom; it is not expected to matter at 1 MHz.
