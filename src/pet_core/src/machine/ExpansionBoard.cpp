#include "ExpansionBoard.hpp"

namespace pet::machine
{

namespace
{

constexpr std::size_t BUFFER = 2048;
constexpr std::size_t WINDOW = 4096;

} // namespace

ExpansionBoard::ExpansionBoard() : mCharacterRam( CHARACTER_RAM_SIZE, 0 ), mRam0( RAM_SIZE, 0 ), mRam1( RAM_SIZE, 0 ) {}

bool ExpansionBoard::decodes( std::uint16_t address )
{
  return address >= 0x9000 && address < 0xc000;
}

unsigned ExpansionBoard::shownBuffer() const
{
  return mChips[0].portB() & 0x01U;
}

bool ExpansionBoard::romShown() const
{
  return ( mChips[0].portB() & 0x02U ) != 0;
}

unsigned ExpansionBoard::bank( unsigned window ) const
{
  return mChips.at( window ).portA() & 0x7fU;
}

std::uint8_t ExpansionBoard::read( std::uint16_t address ) const
{
  if ( address < 0xa000 )
  {
    if ( ( address & 0x0800U ) != 0 )
    {
      // The buffer the video does not show.
      return mCharacterRam.at( ( ( shownBuffer() ^ 1U ) * BUFFER ) + ( address & 0x07ffU ) );
    }
    switch ( address & 3U )
    {
    case 1:
      return mChips[0].readData();
    case 3:
      return mChips[1].readData();
    default:
      // The address latches select no chip, and the board's author has the
      // pull-ups give $FF (docs/open-questions.md).
      return 0xff;
    }
  }
  if ( address < 0xb000 )
  {
    return mRam0.at( ( bank( 0 ) * WINDOW ) + ( address & 0x0fffU ) );
  }
  return mRam1.at( ( bank( 1 ) * WINDOW ) + ( address & 0x0fffU ) );
}

void ExpansionBoard::write( std::uint16_t address, std::uint8_t value )
{
  if ( address < 0xa000 )
  {
    if ( ( address & 0x0800U ) != 0 )
    {
      mCharacterRam.at( ( ( shownBuffer() ^ 1U ) * BUFFER ) + ( address & 0x07ffU ) ) = value;
      return;
    }
    Ym2149& chip = mChips.at( ( address >> 1U ) & 1U );
    if ( ( address & 1U ) == 0 )
    {
      chip.latchAddress( value );
    }
    else
    {
      chip.writeData( value );
    }
    return;
  }
  if ( address < 0xb000 )
  {
    mRam0.at( ( bank( 0 ) * WINDOW ) + ( address & 0x0fffU ) ) = value;
    return;
  }
  mRam1.at( ( bank( 1 ) * WINDOW ) + ( address & 0x0fffU ) ) = value;
}

void ExpansionBoard::cycle()
{
  // AY1's port B bits 2 and 3: 0 puts a chip's divider on, 1 MHz; 1 leaves it
  // at its oscillator's 2 MHz.
  std::uint8_t const control = mChips[0].portB();
  mChips[0].clock( ( control & 0x04U ) != 0 ? 2 : 1 );
  mChips[1].clock( ( control & 0x08U ) != 0 ? 2 : 1 );
}

std::span<std::uint8_t const> ExpansionBoard::glyphs() const
{
  if ( romShown() )
  {
    return {};
  }
  return std::span<std::uint8_t const>{ mCharacterRam }.subspan( shownBuffer() * BUFFER, BUFFER );
}

Ym2149& ExpansionBoard::chip( unsigned index )
{
  return mChips.at( index );
}

Ym2149 const& ExpansionBoard::chip( unsigned index ) const
{
  return mChips.at( index );
}

std::span<std::uint8_t const> ExpansionBoard::characterRam() const
{
  return mCharacterRam;
}

std::span<std::uint8_t const> ExpansionBoard::ram( unsigned index ) const
{
  return index == 0 ? mRam0 : mRam1;
}

} // namespace pet::machine
