# Documentation map

This file is the index of every document in the project. **A document that is
not listed here must not exist.** If a new piece of writing cannot be classified
into one of the buckets below, that is the signal it should not be written. The
regime is PGMEmu's, adopted whole: [0001](decisions/0001-documentation-regime.md).

## The rule

> **Documentation carries *why* and *contracts*. Code carries *how*.**

If a sentence can be read off a function signature, it does not belong in a
document. Non-obvious *reasoning* inside the code belongs in code comments, not
here.

Two supporting rules:

1. **One fact, one place.** Every other place links to it instead of repeating it.
2. **No status files.** No `PROGRESS.md`, `TODO.md`, `CHANGES.md`, session notes
   or similar. Git history covers that ground.

Documentation is updated in the **same commit** as the change that invalidated
it, never "at the end of the session".

## Buckets

| Location | Answers | Must not contain |
|---|---|---|
| `/README.md` | What PetMcp is, how to build it, how to run it | architecture, internal APIs |
| `/THIRD_PARTY.md` | What PetMcp is derived from and built with, and the terms that work asks to be carried | anything of ours beyond the credits |
| `/CLAUDE.md` | How to work in this repository: commands, conventions, where things live | domain knowledge; rules and pointers only |
| `docs/architecture.md` | What the components are, how time and data flow between them, **why** the boundaries sit where they do | class or function listings, signatures |
| `docs/decisions/NNNN-*.md` | Why a decision was made, and what it costs. Immutable once merged; a reversal is a new record that supersedes the old one | present-tense state of the system |
| `docs/spec/*.md` | External contracts: the control protocol, save-state layout | implementation detail |
| `docs/hardware/*.md` | Facts about the hardware that no reference outside this repository holds: the expansion board, and where the emulator knowingly departs from its references, with the evidence | anything VICE, TangPet or the owner's measurements already say; link to them |
| `docs/plans/*.md` | The order decided-but-unbuilt work is built in, one entry per session-sized task, each removed in the commit that lands it. A queue that only shrinks | status, progress, what was done, any decision the records do not hold |
| `docs/open-questions.md` | Questions deferred on purpose, each removed when a record answers it | anything decided |
| `libextern/README.md` | Third-party code carried in the tree: what, from which upstream commit, under which licence, and how it is updated | anything of ours; a local patch to upstream code |

## Index

### Overview
- [Architecture](architecture.md): the core, the control API, the frontends, and how emulated time is kept

### Specifications
- [The control protocol](spec/control-protocol.md): transports, requests, responses, error codes, transcripts, and every method

### Decision records
- [0001: Documentation regime](decisions/0001-documentation-regime.md)
- [0002: What decides how the machine behaves](decisions/0002-references.md)
- [0003: The 6502 is floooh's m6502.h](decisions/0003-cpu-core.md)
- [0004: Licence is GPL-2.0](decisions/0004-licence-gpl-2.md)
- [0005: One control API](decisions/0005-one-control-api.md)
- [0006: Code that decides emulated behaviour is vendored, tooling is fetched](decisions/0006-dependencies.md)
- [0007: Code style and toolchain are PGMEmu's](decisions/0007-code-style-is-pgmemus.md)
- [0008: The renderer is SDL_GPU](decisions/0008-the-renderer-is-sdl-gpu.md)
- [0009: The machine is stepped a cycle at a time](decisions/0009-stepped-a-cycle-at-a-time.md)
- [0010: The SD2PET is a behavioural device that runs NODISKEMU's DOS](decisions/0010-sd2pet.md)
- [0011: MCP over HTTP is served by cpp-httplib, JSON-lines over TCP by sockets of our own](decisions/0011-network-transports.md)
- [0012: The screen's shaders are GLSL, compiled offline to SPIR-V and MSL](decisions/0012-shaders-are-compiled-offline.md)
- [0013: CI builds three platforms, and a tag makes a release](decisions/0013-ci-and-releases.md)
- [0014: The host keyboard is mapped by symbol](decisions/0014-keyboard-by-symbol.md)

### Hardware
- [The 6502](hardware/cpu.md): where the emulated 6502 departs from m6502.h or m6502.h from the NMOS 6502, and the evidence
- [The machine](hardware/machine.md): where the emulated PET follows VICE, TangPet or neither, and the evidence
- [The expansion board](hardware/expansion-board.md): two YM2149Fs, character RAM and 1 MB of banked RAM in the PET's sockets

### Plans
- [Milestones](plans/milestones.md): what is still to be built, in order

### Open questions
- [Open questions](open-questions.md)
