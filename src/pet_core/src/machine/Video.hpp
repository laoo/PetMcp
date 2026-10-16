#pragma once

// The 3032's video: no CRTC, but fixed counters (docs/architecture.md,
// "Emulated time"). The beam reads one character of video RAM and one row of
// its glyph in each of a line's first 40 cycles, as TangPet's pet3032_core.v
// does, and raises the vertical blank VICE's crtc.c raises.

#include "Clock.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace pet::machine
{

class Video
{
public:
  /// What the beam did in a cycle that the rest of the machine hears.
  enum class Event : std::uint8_t
  {
    NONE,
    /// The vertical blank begins: a frame is complete.
    BLANK_BEGINS,
    BLANK_ENDS
  };

  /// Reads the glyphs from `characters`, 2 KB, and the screen from
  /// `videoRam`, 1 KB; both must outlive it.
  Video( std::span<std::uint8_t const> characters, std::span<std::uint8_t const> videoRam );

  /// The beam's work in cycle `now`, after the 6502's access of it.
  /// `textSet` is VIA CA2, high for the text set; the glyph read in a cycle
  /// uses the set chosen before that cycle's access, as TangPet's does.
  Event cycle( Clock now, bool textSet );

  /// Whether the beam is in the vertical blank (VIA PB5 low).
  [[nodiscard]] bool offScreen() const;

  /// The last complete picture: SCREEN_WIDTH by SCREEN_HEIGHT RGBA, lit dots
  /// white.
  [[nodiscard]] std::span<std::uint8_t const> picture() const;
  /// Pictures completed since power-up.
  [[nodiscard]] std::int64_t picturesDrawn() const;

  /// The picture being drawn and the last complete one are kept a bit a dot.
  template <class Archive>
  void serialize( Archive& archive )
  {
    std::vector<std::uint8_t> drawing;
    std::vector<std::uint8_t> complete;
    if constexpr ( !Archive::READING )
    {
      drawing = packDots();
      complete = packPicture();
    }
    archive( drawing );
    archive( complete );
    archive( mPicturesDrawn );
    archive( mDisplayEnabled );
    archive( mOffScreen );
    archive( mTextSet );
    if constexpr ( Archive::READING )
    {
      unpack( drawing, complete );
    }
  }

private:
  [[nodiscard]] std::vector<std::uint8_t> packDots() const;
  [[nodiscard]] std::vector<std::uint8_t> packPicture() const;
  /// Takes the picture being drawn and the last complete one back from their
  /// bits; bits of the wrong size leave them as they were.
  void unpack( std::vector<std::uint8_t> const& drawing, std::vector<std::uint8_t> const& complete );

  std::span<std::uint8_t const> mCharacters;
  std::span<std::uint8_t const> mVideoRam;
  /// The picture being drawn, a byte a dot.
  std::vector<std::uint8_t> mDots;
  std::vector<std::uint8_t> mPicture;
  std::int64_t mPicturesDrawn{};
  /// VICE's crtc.venable: the display, on from each frame's line 0 to its
  /// line 200. It is off at power-up, so the first frame shows nothing and its
  /// blank begins with PB5 high and a rising edge on CB1, which PIA1 does not
  /// take for the retrace (docs/hardware/machine.md).
  bool mDisplayEnabled{};
  bool mOffScreen{};
  bool mTextSet{};
};

} // namespace pet::machine
