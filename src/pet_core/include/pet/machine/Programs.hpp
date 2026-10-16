#pragma once

#include "pet/machine/Machine.hpp"

#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

namespace pet::machine
{

/// Where a program was put.
struct LoadedProgram
{
  std::uint16_t start{};
  /// One past its last byte.
  std::uint32_t end{};
};

/// Puts a `.prg` (a load address, little-endian, then the bytes) into RAM as
/// BASIC 2's LOAD does, and sets the start of BASIC's variables after it as
/// LOAD does, so that RUN runs it. Fails, saying why, when it is shorter than
/// its address or does not fit below the screen.
std::expected<LoadedProgram, std::string> loadProgram( Machine& machine, std::span<std::uint8_t const> prg );

/// Puts `keys`, PETSCII, into the kernal's keyboard buffer as if they had been
/// typed, after what is there; at most 10 fit. Returns how many were taken.
std::size_t queueKeys( Machine& machine, std::string_view keys );

} // namespace pet::machine
