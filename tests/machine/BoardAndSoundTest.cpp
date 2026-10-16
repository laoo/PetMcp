#include <catch2/catch_test_macros.hpp>

#include "pet/machine/Machine.hpp"
#include "pet/machine/Programs.hpp"

#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <utility>
#include <vector>

// The expansion board and the sound, on the PET with VICE's ROMs: the board's
// decoding, banking and character source as docs/hardware/expansion-board.md
// specifies them, and the tones its YM2149Fs and CB2 make. Skip without the
// ROMs.

using pet::machine::AudioFrame;
using pet::machine::Machine;

namespace
{

std::unique_ptr<Machine> booted( bool board = true )
{
  auto roms = pet::machine::Roms::load( PET_TEST_ROM_DIR );
  if ( !roms )
  {
    return nullptr;
  }
  auto machine = std::make_unique<Machine>( std::move( *roms ), board );
  machine->runFrames( 100 );
  return machine;
}

/// The board's state, which must be there.
pet::machine::BoardState boardOf( Machine const& machine )
{
  auto const board = machine.board();
  REQUIRE( board );
  return board.value_or( pet::machine::BoardState{} );
}

/// Has the 6502 make the writes, in order, and come back to READY: a routine
/// at $1000 of LDA #value / STA address, run by SYS.
void store( Machine& machine, std::initializer_list<std::pair<std::uint16_t, std::uint8_t>> writes )
{
  std::uint16_t at = 0x1000;
  auto const put = [&]( std::uint8_t byte ) { machine.poke( at++, byte ); };
  for ( auto const& [address, value] : writes )
  {
    put( 0xa9 );
    put( value );
    put( 0x8d );
    put( static_cast<std::uint8_t>( address & 0xffU ) );
    put( static_cast<std::uint8_t>( address >> 8U ) );
  }
  put( 0x60 );
  pet::machine::queueKeys( machine, "SYS4096\r" );
  machine.runFrames( 10 );
}

/// The frequency of a channel's square wave, by its rising zero crossings,
/// and its peak.
struct Tone
{
  double hertz{};
  int peak{};
};

Tone toneOf( std::vector<AudioFrame> const& frames, bool right )
{
  // The first tenth of a second lets the DC blocker settle.
  std::size_t const from = Machine::AUDIO_RATE / 10;
  int crossings = 0;
  int peak = 0;
  std::size_t first = 0;
  std::size_t last = 0;
  for ( std::size_t i = from + 1; i < frames.size(); ++i )
  {
    int const previous = right ? frames[i - 1].right : frames[i - 1].left;
    int const sample = right ? frames[i].right : frames[i].left;
    peak = std::max( peak, std::abs( sample ) );
    if ( previous < 0 && sample >= 0 )
    {
      if ( crossings == 0 )
      {
        first = i;
      }
      last = i;
      ++crossings;
    }
  }
  if ( crossings < 2 )
  {
    return Tone{ .hertz = 0.0, .peak = peak };
  }
  return Tone{ .hertz =
                   static_cast<double>( crossings - 1 ) * Machine::AUDIO_RATE / static_cast<double>( last - first ),
               .peak = peak };
}

/// One second of sound.
std::vector<AudioFrame> listen( Machine& machine )
{
  machine.startCapture();
  machine.runFrames( 60 );
  return machine.stopCapture();
}

} // namespace

TEST_CASE( "after power-up the board is a plain 3032's sockets but for its registers", "[roms][board]" )
{
  auto machine = booted();
  if ( !machine )
  {
    SKIP( "no ROMs in " << PET_TEST_ROM_DIR );
  }
  auto const board = boardOf( *machine );
  // Both ports inputs, pulled up: bank $7F, the ROM's characters, 2 MHz.
  CHECK( board.banks == std::array<unsigned, 2>{ 0x7f, 0x7f } );
  CHECK( board.romCharacters );
  CHECK( board.fullClock == std::array<bool, 2>{ true, true } );
  // A latch's address drives nothing; the data reads the selected register.
  CHECK( machine->peek( 0x9000 ) == 0xff );
  CHECK( machine->peek( 0x9402 ) == 0xff );
  CHECK( machine->peek( 0x9001 ) == 0x00 );

  auto const without = booted( false );
  CHECK_FALSE( without->board() );
}

TEST_CASE( "AY1's port A banks RAM0 into $A000, AY2's RAM1 into $B000", "[roms][board]" )
{
  auto machine = booted();
  if ( !machine )
  {
    SKIP( "no ROMs in " << PET_TEST_ROM_DIR );
  }
  store( *machine,
         { { 0x9000, 7 },
           { 0x9001, 0x40 },
           { 0x9000, 14 },
           { 0x9001, 3 },
           { 0xa000, 0x5a },
           { 0x9001, 4 },
           { 0xa000, 0xa5 },
           { 0x9002, 7 },
           { 0x9003, 0x40 },
           { 0x9002, 14 },
           { 0x9003, 0x7e },
           { 0xbfff, 0x77 } } );
  auto const banked = boardOf( *machine );
  CHECK( banked.banks == std::array<unsigned, 2>{ 4, 0x7e } );
  CHECK( machine->boardMemory( 0 )[std::size_t{ 3 } * 4096] == 0x5a );
  CHECK( machine->boardMemory( 0 )[std::size_t{ 4 } * 4096] == 0xa5 );
  CHECK( machine->boardMemory( 1 )[( std::size_t{ 0x7e } * 4096 ) + 0xfff] == 0x77 );
  CHECK( machine->peek( 0xa000 ) == 0xa5 );
  CHECK( machine->peek( 0xbfff ) == 0x77 );
  // The data register reads back in full, as a YM2149's does.
  store( *machine, { { 0x9000, 1 }, { 0x9001, 0xff } } );
  CHECK( machine->peek( 0x9001 ) == 0xff );
}

TEST_CASE( "AY1's port B puts the character RAM's buffer on the screen", "[roms][board]" )
{
  auto machine = booted();
  if ( !machine )
  {
    SKIP( "no ROMs in " << PET_TEST_ROM_DIR );
  }
  // Buffer 1 is shown while the port is an input, so $9800 is buffer 0:
  // screen code 0 of the graphics set, all its dots lit. Then buffer 0 shown,
  // from RAM, the chips at 2 MHz.
  machine->poke( 0x8000, 0x00 );
  store( *machine,
         { { 0x9800, 0xff },
           { 0x9801, 0xff },
           { 0x9802, 0xff },
           { 0x9803, 0xff },
           { 0x9804, 0xff },
           { 0x9805, 0xff },
           { 0x9806, 0xff },
           { 0x9807, 0xff },
           { 0x9000, 7 },
           { 0x9001, 0x80 },
           { 0x9000, 15 },
           { 0x9001, 0x0c } } );
  machine->poke( 0x8000, 0x00 );
  machine->runFrames( 2 );
  auto const shown = boardOf( *machine );
  CHECK_FALSE( shown.romCharacters );
  CHECK( shown.shownBuffer == 0 );
  CHECK( machine->boardMemory( 2 )[0] == 0xff );
  auto const picture = machine->picture();
  bool allLit = true;
  for ( std::size_t row = 0; row < 8; ++row )
  {
    for ( std::size_t dot = 0; dot < 8; ++dot )
    {
      allLit = allLit && picture[( ( row * 320 ) + dot ) * 4] == 0xff;
    }
  }
  CHECK( allLit );
}

TEST_CASE( "a tone on each YM2149F has the frequency and the level its registers give", "[roms][board][sound]" )
{
  auto machine = booted();
  if ( !machine )
  {
    SKIP( "no ROMs in " << PET_TEST_ROM_DIR );
  }
  // AY1, channel A: period 284, full volume, tone alone. At 2 MHz that is
  // 2,000,000 / 16 / 284 = 440.1 Hz on the left.
  store( *machine,
         { { 0x9000, 0 },
           { 0x9001, 284 & 0xff },
           { 0x9000, 1 },
           { 0x9001, 284 >> 8 },
           { 0x9000, 7 },
           { 0x9001, 0x3e },
           { 0x9000, 8 },
           { 0x9001, 0x0f } } );
  auto const loud = listen( *machine );
  Tone const left = toneOf( loud, false );
  CHECK( std::abs( left.hertz - 440.1 ) < 1.0 );
  // A square between 0 and the top level over three channels, DC removed:
  // a sixth of the chip's swing, 16,000.
  CHECK( std::abs( left.peak - 2667 ) < 150 );
  CHECK( toneOf( loud, true ).peak < 50 );

  // Volume 8 is level 17 of the 32: 0.111 of the top.
  store( *machine, { { 0x9000, 8 }, { 0x9001, 0x08 } } );
  CHECK( std::abs( toneOf( listen( *machine ), false ).peak - ( 2667 * 0.111 ) ) < 40 );

  // AY2, with its divider on through AY1's port B: 1 MHz, 220 Hz on the right.
  store( *machine,
         { { 0x9000, 8 },
           { 0x9001, 0x00 },
           { 0x9002, 0 },
           { 0x9003, 284 & 0xff },
           { 0x9002, 1 },
           { 0x9003, 284 >> 8 },
           { 0x9002, 7 },
           { 0x9003, 0x3e },
           { 0x9002, 8 },
           { 0x9003, 0x0f },
           { 0x9000, 7 },
           { 0x9001, 0x80 },
           { 0x9000, 15 },
           { 0x9001, 0x06 } } );
  auto const divided = boardOf( *machine );
  CHECK_FALSE( divided.fullClock[1] );
  auto const slow = listen( *machine );
  CHECK( std::abs( toneOf( slow, true ).hertz - 220.0 ) < 1.0 );
  CHECK( toneOf( slow, false ).peak < 50 );
}

TEST_CASE( "CB2 sounds on both channels while the shift register runs free, and stops with it", "[roms][sound]" )
{
  auto machine = booted( false );
  if ( !machine )
  {
    SKIP( "no ROMs in " << PET_TEST_ROM_DIR );
  }
  // The PET's own beeper: ACR $10, the shift register free-running from T2,
  // a pattern of 00001111, T2's latch 100.
  store( *machine, { { 0xe84b, 0x10 }, { 0xe84a, 0x0f }, { 0xe848, 100 } } );
  auto const sounding = listen( *machine );
  Tone const left = toneOf( sounding, false );
  Tone const right = toneOf( sounding, true );
  CHECK( left.peak > 1000 );
  CHECK( left.hertz == right.hertz );
  // A bit shifts every second T2 underflow, a T2 period being its latch plus 2.
  CHECK( std::abs( left.hertz - ( 1'000'000.0 / ( 8 * 2 * 102 ) ) ) < 2.0 );

  store( *machine, { { 0xe84b, 0x00 } } );
  auto const quiet = listen( *machine );
  CHECK( toneOf( quiet, false ).peak < 50 );
}
