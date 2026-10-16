# 0005: One control API

**Status:** accepted

## Context

The emulator will be driven by AI agent sessions as much as by a person: running programs,
typing, mounting disks, reading memory and the screen, and measuring cycles. PGMEmu answers this
with one dispatcher behind several transports, and the owner asked for the same protocols here.

- MCP gives an agent tool discovery, typed parameters and images it can see.
- A line protocol is what a Python harness, CI and a timing test want: cheap per call, trivially
  scriptable, and no session handshake.

## Decision

Every capability is a method of one dispatcher in the core library. Each method takes JSON
`params` and returns a JSON `result`.

- The wire format, the error conventions and the method groups are PGMEmu's, so that the owner's
  tools and agent skills carry over.
- Several transports reach the one dispatcher: JSON-lines on stdio (headless) and on TCP
  (attached to the GUI), and MCP on stdio (headless) and over HTTP (attached to the GUI). The MCP
  tool list is generated from the dispatcher's method table.
- The GUI's windows read state through the same methods.
- PGMEmu's `sim.*` aliases are not carried: there is no simulator here to share scripts with.

The protocol is specified in [spec/control-protocol.md](../spec/control-protocol.md).

## Consequences

- Supporting both MCP and the line protocol costs two thin adapters, not two APIs, and neither
  can drift from the other.
- Anything the GUI can do, an agent can do, such as mounting and running a disk image.
