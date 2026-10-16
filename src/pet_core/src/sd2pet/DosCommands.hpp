// Copyright (C) 2007-2018 Ingo Korb <ingo@akana.de>, as NODISKEMU (a fork of sd2iec)
// carries it. GPL-2.0. Ported to C++ for PetMcp, 2026 (THIRD_PARTY.md).

#pragma once

// doscmd.c (NODISKEMU at commit e51a6df): the commands on channel 15. The
// left-out parts are listed in DosCommands.cpp's header comment.

#include "sd2pet/DosTypes.hpp"
#include "sd2pet/HostFs.hpp"

#include <array>
#include <cstdint>

namespace pet::sd2pet
{

/// eeprom-conf.h's ROM_NAME_LENGTH: the longest name XR takes.
inline constexpr std::uint8_t ROM_NAME_LENGTH = 16;

struct DosCommandsState
{
  /// read_configuration's default is post-* matching on; the EEPROM that
  /// would override it is left out.
  std::uint8_t globalflags{ POSTMATCH };
  std::array<std::uint8_t, COMMAND_BUFFER_SIZE + 2> commandBuffer{};
  std::uint8_t commandLength{};
  std::uint8_t originalLength{};
  Date dateMatchStart{};
  Date dateMatchEnd{};
  /// The CRC of the data M-W wrote, reported by M-E with error 98.
  std::uint16_t datacrc{ 0xffff };
  /// doscmd.c's romfile: the file M-R reads the drive's ROM from.
  FatFile romfile{ .handle = -1, .flag = 0, .fptr = 0, .fsize = 0 };
  /// eeprom-conf.c's rom_filename, set by XR; empty when there is none.
  std::array<std::uint8_t, ROM_NAME_LENGTH + 1> romFilename{};
  /// menu.c's menu_system_enabled, set by XM. The menu itself is left out, so
  /// nothing reads it.
  bool menuSystemEnabled{ true };
};

void parseDoscommand( Dos& dos );
void doChdir( Dos& dos, std::uint8_t* parsestr );
std::uint8_t dayOfWeek( std::uint16_t y, std::uint8_t m, std::uint8_t d );

} // namespace pet::sd2pet
