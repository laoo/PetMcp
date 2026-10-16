# Credits and third-party licences

PetMcp is Copyright (C) 2026 Waldemar Pawlaszek, and is released under the GNU General Public
License, version 2 only ([LICENSE](LICENSE)). This file names the work it is derived from and the
code it is built with, and carries the terms that work asks to be carried. The binary packages
hold this file and the licence of every library linked into them, in `licenses/`.

## The PET: VICE

The emulated PET follows **VICE**, the Versatile Commodore Emulator, https://vice-emu.sourceforge.io,
by the VICE Team, released under the GNU General Public License, version 2 or later. Code ported
from it names the VICE source file it ports, and that file's authors, in its header: among them
Andre Fachat, Andreas Boose, Olaf Seibert, Jouko Valta, Ettore Perazzoli, Teemu Rantanen and
Marco van den Heuvel; VICE's README names the whole team. The YM2149F board's emulation,
`src/pet/petay.c`, is the owner's, in the owner's VICE tree.

## The SD2PET: NODISKEMU

The emulated SD2PET's disk operating system is ported from **NODISKEMU**,
https://github.com/nils-eilers/NODISKEMU, by Nils Eilers, a fork of **sd2iec** by Ingo Korb
(Copyright (C) 2007-2018 Ingo Korb; its ASCII/PET conversion Copyright (C) 2008 Jim Brain),
released under the GNU General Public License, version 2. Code ported from it names the source
file, its authors and the commit it was read at, `e51a6df`, in its header.

NODISKEMU's IEEE-488 bus handling, `src/ieee.c` and `src/avr/atn-ack-petsd+.S`, which
`src/pet_core/src/sd2pet/Sd2Pet.cpp` ports, is under this licence instead:

> Copyright (c) 2015, 2017 Nils Eilers. All rights reserved.
>
> Redistribution and use in source and binary forms, with or without modification, are permitted
> provided that the following conditions are met:
>
> 1. Redistributions of source code must retain the above copyright notice, this list of
>    conditions and the following disclaimer.
>
> 2. Redistributions in binary form must reproduce the above copyright notice, this list of
>    conditions and the following disclaimer in the documentation and/or other materials provided
>    with the distribution.
>
> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR
> IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
> FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
> CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
> DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
> DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
> WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
> WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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
