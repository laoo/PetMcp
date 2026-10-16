#pragma once

// The owner's expansion board in the $9000-$BFFF sockets
// (docs/hardware/expansion-board.md): two YM2149Fs, 4 KB of character RAM the
// video shows instead of the ROM, and two 512 KB RAMs through 4 KB windows,
// all steered by the YM2149Fs' ports.

#include "Ym2149.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace pet::machine
{

class ExpansionBoard
{
public:
  static constexpr std::size_t RAM_SIZE = std::size_t{ 512 } * 1024;
  static constexpr std::size_t CHARACTER_RAM_SIZE = 4096;

  /// Powered up: the RAMs zero, both chips reset, so the board starts as a
  /// plain 3032's sockets would show the PET's own characters.
  ExpansionBoard();

  /// Whether the board answers `address`: $9000-$BFFF.
  [[nodiscard]] static bool decodes( std::uint16_t address );

  /// The 6502's access; peek() reads without touching anything, which a read
  /// of the board never does anyway.
  [[nodiscard]] std::uint8_t read( std::uint16_t address ) const;
  void write( std::uint16_t address, std::uint8_t value );

  /// A cycle of the PET's 1 MHz: each chip runs 2 clocks, or 1 where its
  /// divider is on.
  void cycle();

  /// The glyphs the video draws: the shown character buffer, 2 KB laid out as
  /// the ROM is, or empty when the ROM's are shown.
  [[nodiscard]] std::span<std::uint8_t const> glyphs() const;

  [[nodiscard]] Ym2149& chip( unsigned index );
  [[nodiscard]] Ym2149 const& chip( unsigned index ) const;
  /// The 4 KB banks the windows at $A000 and $B000 show.
  [[nodiscard]] unsigned bank( unsigned window ) const;
  [[nodiscard]] std::span<std::uint8_t const> characterRam() const;
  [[nodiscard]] std::span<std::uint8_t const> ram( unsigned index ) const;

  template <class Archive>
  void serialize( Archive& archive )
  {
    mChips.at( 0 ).serialize( archive );
    mChips.at( 1 ).serialize( archive );
    archive( mCharacterRam );
    archive( mRam0 );
    archive( mRam1 );
  }

private:
  /// AY1's port B: which character buffer is shown, and whether the ROM is.
  [[nodiscard]] unsigned shownBuffer() const;
  [[nodiscard]] bool romShown() const;

  std::array<Ym2149, 2> mChips;
  std::vector<std::uint8_t> mCharacterRam;
  std::vector<std::uint8_t> mRam0;
  std::vector<std::uint8_t> mRam1;
};

} // namespace pet::machine
