# Credits and third-party licences

PetMcp is released under the GNU General Public License, version 2 only ([LICENSE](LICENSE)).
This file names the work it is derived from and the code it is built with, and carries the terms
that work asks to be carried. The binary packages hold this file and the licence of every library
linked into them, in `licenses/`.

## The PET: VICE

The emulated PET follows **VICE**, the Versatile Commodore Emulator, https://vice-emu.sourceforge.io,
by the VICE Team, released under the GNU General Public License, version 2 or later. Code ported
from it names the VICE source file it ports in its header.

## The SD2PET: NODISKEMU

The emulated SD2PET's disk operating system is ported from **NODISKEMU**,
https://github.com/nils-eilers/NODISKEMU, by Nils Eilers, a fork of **sd2iec** by Ingo Korb,
released under the GNU General Public License, version 2. Code ported from it names the source
file and the commit it was read at in its header.

The SD card's layer over a host directory (`src/pet_core/src/sd2pet/HostFs.cpp`) ports parts of
**FatFs** R0.06 as NODISKEMU carries it, by ChaN, with long names by Jim Brain. Its terms:

> Copyright (C) 2008, ChaN, all right reserved.
>
> * The FatFs module is a free software and there is no warranty.
> * You can use, modify and/or redistribute it for personal, non-profit or commercial use without
>   restriction under your responsibility.
> * Redistributions of source code must retain the above copyright notice.

## The expansion board and TangPet

The expansion board's design and TangPet, the FPGA PET the emulator is compared with, are the
owner's.

## Libraries

Carried in `libextern/`, unmodified, with their licences beside them
([libextern/README.md](libextern/README.md)):

| Library | Copyright | Licence |
|---|---|---|
| [chips](https://github.com/floooh/chips)' `m6502.h` and `m6502dasm.h`, the 6502 | Andre Weissflog | zlib |

Fetched when the build is configured ([cmake/Dependencies.cmake](cmake/Dependencies.cmake)), and
linked into the programs:

| Library | Copyright | Licence |
|---|---|---|
| [SDL 3](https://github.com/libsdl-org/SDL) | Sam Lantinga | zlib |
| [Dear ImGui](https://github.com/ocornut/imgui) | Omar Cornut | MIT |
| [spdlog](https://github.com/gabime/spdlog), with its bundled [{fmt}](https://github.com/fmtlib/fmt) | Gabi Melman and contributors; Victor Zverovich | MIT |
| [nlohmann/json](https://github.com/nlohmann/json) | Niels Lohmann | MIT |
| [miniz](https://github.com/richgel999/miniz) | RAD Game Tools and Valve Software, Rich Geldreich | MIT |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | yhirose | MIT |
| [CLI11](https://github.com/CLIUtils/CLI11) | University of Cincinnati | BSD-3-Clause |

Used by the test suite alone, and not distributed: [Catch2](https://github.com/catchorg/Catch2)
(Boost Software License 1.0), and the [SingleStepTests](https://github.com/SingleStepTests) 65x02
suite, fetched by `scripts/fetch-6502-tests.sh`.

## What is not here

No ROM of any kind is part of PetMcp. The PET's ROMs are Commodore's; the emulator reads them
from a directory the user names, such as VICE's `data/PET`. Commodore and PET are their owners'
trademarks; PetMcp is not affiliated with them.
