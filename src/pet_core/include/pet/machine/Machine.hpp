#pragma once

#include "pet/machine/Keyboard.hpp"
#include "pet/machine/Roms.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>

namespace pet::machine
{

/// The 6502's programmer's registers, at an instruction boundary. `p` holds
/// the status as PHP pushes it.
struct CpuState
{
  std::uint16_t pc{};
  std::uint8_t a{};
  std::uint8_t x{};
  std::uint8_t y{};
  std::uint8_t s{};
  std::uint8_t p{};
};

enum class StopReason : std::uint8_t
{
  COMPLETED,
  CONDITION,
  TIMEOUT
};

struct RunResult
{
  StopReason reason{};
  std::uint64_t cycles{};
  std::uint64_t frames{};
};

/// The PET 3032: the 6502, 32 KB of RAM, the video, the PIAs and the VIA,
/// stepped a cycle at a time (docs/decisions/0009-stepped-a-cycle-at-a-time.md).
/// Every run ends at an instruction boundary, where the registers are whole.
class Machine
{
public:
  /// A machine powered up with `roms`: RAM in VICE's pattern, every chip as
  /// after power-up, the 6502 about to run its reset sequence.
  explicit Machine( Roms roms );
  ~Machine();

  Machine( Machine const& ) = delete;
  Machine& operator=( Machine const& ) = delete;
  Machine( Machine&& ) = delete;
  Machine& operator=( Machine&& ) = delete;

  /// Pulls the reset line, which reaches the 6502, the PIAs and the VIA; RAM
  /// and the video's counters are left alone. The 6502 resets at its next
  /// instruction boundary.
  void reset();

  /// Runs at least `count` cycles, to the next instruction boundary.
  RunResult runCycles( std::uint64_t count );
  /// Runs through `count` frame boundaries, where the vertical blank begins,
  /// to the next instruction boundary.
  RunResult runFrames( std::uint64_t count );
  /// Runs until `condition` holds at an instruction boundary, or for
  /// `timeoutCycles`.
  RunResult runUntil( std::function<bool( Machine const& )> const& condition, std::uint64_t timeoutCycles );

  /// Called at every instruction boundary from now on, before the instruction
  /// runs; empty to stop. For traces; it slows the run.
  void setInstructionHook( std::function<void( Machine const& )> hook );

  /// Cycles since power-up, and frame boundaries passed.
  [[nodiscard]] std::uint64_t now() const;
  [[nodiscard]] std::uint64_t frame() const;

  [[nodiscard]] CpuState cpu() const;

  /// A byte as the 6502 would read it at `address`, without the read's side
  /// effects; an address nothing answers gives what the bus last carried.
  [[nodiscard]] std::uint8_t peek( std::uint16_t address ) const;
  /// Writes RAM or video RAM as a debugger does: no bus cycle is made. False
  /// where `address` is neither.
  bool poke( std::uint16_t address, std::uint8_t value );

  /// Holds `key` down, or lets it go: the keys the control protocol holds.
  void setKey( PetKey key, bool down );
  /// Lets go every key setKey() holds.
  void releaseKeys();
  /// The keys setKey() holds.
  [[nodiscard]] KeyMatrix heldKeys() const;
  /// The keys the host's keyboard holds, which the PET sees together with
  /// those setKey() holds.
  void setHostKeys( KeyMatrix const& keys );

  [[nodiscard]] std::span<std::uint8_t const> ram() const;
  [[nodiscard]] std::span<std::uint8_t const> videoRam() const;
  [[nodiscard]] Roms const& roms() const;

  /// The last complete picture, SCREEN_WIDTH by SCREEN_HEIGHT RGBA, and how
  /// many have been completed since power-up.
  [[nodiscard]] std::span<std::uint8_t const> picture() const;
  [[nodiscard]] std::int64_t picturesDrawn() const;
  /// Whether the video shows the text set, VIA CA2 high, rather than the
  /// graphics set.
  [[nodiscard]] bool textSet() const;

private:
  struct Board;
  std::unique_ptr<Board> mBoard;
};

} // namespace pet::machine
