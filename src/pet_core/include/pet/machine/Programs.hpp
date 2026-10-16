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

/// Types `keys`, PETSCII, through the keyboard buffer, 10 at a time: queues
/// what fits and runs the PET until the editor has taken it, then the next.
/// Gives up after `timeoutFrames` frames. Answers the runs' cycles and
/// frames together, the reason the last one gave.
RunResult typeKeys( Machine& machine, std::string_view keys, std::uint64_t timeoutFrames );

/// ASCII as it would be typed on the PET with its graphics set: letters of
/// either case the unshifted keys, `←`, `↑` and `£` (UTF-8) their own; the
/// rest is kept.
std::string petsciiTyped( std::string_view text );

} // namespace pet::machine
