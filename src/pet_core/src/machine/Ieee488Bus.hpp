#pragma once

// The PET's IEEE-488 port and what hangs on it. Every line is open collector:
// each end only pulls it low, and the line is low when either end does
// (VICE's parallel.c keeps a mask of who pulls each). The PET drives its end
// from PIA2, PIA1 and the VIA (petpia1.c, petpia2.c, petvia.c); the SD2PET
// drives the other. Each change of either end is noted, with its cycle, for
// ieee.trace.

#include "Clock.hpp"
#include "pet/machine/Machine.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pet::machine
{

/// One end's pull on the lines.
struct IeeeEnd
{
  /// The data lines' levels as this end lets them be: a 0 bit pulls its line
  /// low, which on IEEE-488 is a 1.
  std::uint8_t data{ 0xff };
  /// The control lines it pulls low, as IEEE_* bits.
  std::uint8_t lines{};

  bool operator==( IeeeEnd const& ) const = default;
};

class Ieee488Bus
{
public:
  /// The data lines' levels.
  [[nodiscard]] std::uint8_t data() const
  {
    return static_cast<std::uint8_t>( mPet.data & mDrive.data );
  }

  /// Whether a control line, an IEEE_* bit, is low: pulled by either end.
  [[nodiscard]] bool low( std::uint8_t line ) const
  {
    return ( ( mPet.lines | mDrive.lines ) & line ) != 0;
  }

  [[nodiscard]] IeeeEnd const& pet() const
  {
    return mPet;
  }

  [[nodiscard]] IeeeEnd const& drive() const
  {
    return mDrive;
  }

  void setPetData( std::uint8_t data, Clock now )
  {
    IeeeEnd end = mPet;
    end.data = data;
    set( mPet, end, now );
  }

  /// Pulls `line` low from the PET's end, or lets go of it.
  void setPetLine( std::uint8_t line, bool pulled, Clock now )
  {
    IeeeEnd end = mPet;
    end.lines = static_cast<std::uint8_t>( pulled ? ( end.lines | line ) : ( end.lines & ~line ) );
    set( mPet, end, now );
  }

  void setDrive( IeeeEnd const& end, Clock now )
  {
    set( mDrive, end, now );
  }

  /// How many changes are kept.
  [[nodiscard]] std::size_t traceLength() const
  {
    return mTraceSize;
  }

  /// The `index`th change kept, 0 being the newest.
  [[nodiscard]] IeeeEvent traced( std::size_t index ) const
  {
    return mTrace.at( ( mTraceNext + TRACE_SIZE - 1 - index ) % TRACE_SIZE );
  }

  /// Both ends, for a save state. The changes noted are not part of it.
  template <class Archive>
  void serialize( Archive& archive )
  {
    archive( mPet );
    archive( mDrive );
  }

  /// How many changes the bus keeps: a sector read is about 2000.
  static constexpr std::size_t TRACE_SIZE = 65536;

private:
  void set( IeeeEnd& end, IeeeEnd const& now, Clock clock )
  {
    if ( end == now )
    {
      return;
    }
    end = now;
    if ( mTrace.empty() )
    {
      mTrace.resize( TRACE_SIZE );
    }
    mTrace[mTraceNext] = IeeeEvent{
      .cycle = clock, .petData = mPet.data, .driveData = mDrive.data, .petLines = mPet.lines, .driveLines = mDrive.lines
    };
    mTraceNext = ( mTraceNext + 1 ) % TRACE_SIZE;
    mTraceSize = std::min( mTraceSize + 1, TRACE_SIZE );
  }

  IeeeEnd mPet;
  IeeeEnd mDrive;
  std::vector<IeeeEvent> mTrace;
  std::size_t mTraceNext{};
  std::size_t mTraceSize{};
};

} // namespace pet::machine
