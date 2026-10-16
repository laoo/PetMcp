#pragma once

// The PET's one clock (docs/architecture.md, "Emulated time"): a cycle of the
// 6502's 1 MHz, counted from power-up, and the video's fixed timing within it.

#include <cstdint>

namespace pet::machine
{

/// Cycles since power-up. A device's event "at cycle N" happens after the
/// 6502's bus access of cycle N, as VICE's alarms for clock N run after the
/// CPU's access for N.
using Clock = std::uint64_t;

/// A line is 64 cycles, 8 dots each; a frame is 260 lines. These are the
/// fixed CRTC values VICE gives a PET without a CRTC (pet.c,
/// pet_crtc_set_screen), which TangPet's video counters also make.
inline constexpr Clock CYCLES_PER_LINE = 64;
inline constexpr Clock LINES_PER_FRAME = 260;
inline constexpr Clock CYCLES_PER_FRAME = CYCLES_PER_LINE * LINES_PER_FRAME;
inline constexpr unsigned TEXT_COLUMNS = 40;
inline constexpr unsigned TEXT_ROWS = 25;
inline constexpr unsigned TEXT_LINES = TEXT_ROWS * 8;

/// Where a cycle falls in the video's frame. Line 0 of the first frame
/// begins at cycle 1, as VICE's crtc_reset has the first line start a cycle
/// after the clock it resets at.
constexpr Clock inFrame( Clock clock )
{
  return ( clock + CYCLES_PER_FRAME - 1 ) % CYCLES_PER_FRAME;
}

/// Where in each frame the vertical blank begins and ends: at the end of the
/// 40th cycle of line 199, and of line 259 (crtc.c, the adjusted retrace
/// alarm). TangPet's video_on changes at the same point, the end of the cycle
/// that fetches a line's last character. VICE's trace of a power-up fixes it:
/// its IRQs, the branch that delays one among them, are entered where an
/// edge at these cycles has them.
inline constexpr Clock VBLANK_START = ( ( TEXT_LINES - 1 ) * CYCLES_PER_LINE ) + TEXT_COLUMNS - 1;
inline constexpr Clock VBLANK_END = ( ( LINES_PER_FRAME - 1 ) * CYCLES_PER_LINE ) + TEXT_COLUMNS - 1;

/// An interrupt output as VICE's interrupt status keeps it: a level, and the
/// cycle from whose end it holds. The 6502 sees the level a source holds as of
/// cycle N in the cycles after N.
class InterruptLine
{
public:
  /// The line holds `level` from cycle `from` on. A change made for a cycle
  /// still to come replaces one made earlier for a later cycle.
  void set( bool level, Clock from )
  {
    mBefore = from == 0 ? mBefore : at( from - 1 );
    mLevel = level;
    mFrom = from;
  }

  [[nodiscard]] bool at( Clock clock ) const
  {
    return clock >= mFrom ? mLevel : mBefore;
  }

  template <class Archive>
  void serialize( Archive& archive )
  {
    archive( mLevel );
    archive( mBefore );
    archive( mFrom );
  }

private:
  bool mLevel{};
  bool mBefore{};
  Clock mFrom{};
};

} // namespace pet::machine
