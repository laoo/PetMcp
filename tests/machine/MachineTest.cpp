#include <catch2/catch_test_macros.hpp>

#include "pet/machine/Machine.hpp"
#include "pet/machine/Programs.hpp"
#include "pet/machine/Screen.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// The PET with its ROMs from VICE's data/PET: it boots, and the owner's
// measuring programs from ../PETTest count on it what they counted in VICE.
// Skip without the ROMs or the programs.

using pet::machine::Machine;

namespace
{

std::optional<Machine> powerUp()
{
  auto roms = pet::machine::Roms::load( PET_TEST_ROM_DIR );
  if ( !roms )
  {
    return std::nullopt;
  }
  return std::optional<Machine>{ std::in_place, std::move( *roms ) };
}

std::string screenOf( Machine const& machine )
{
  std::string text;
  for ( std::string const& line : pet::machine::screenText( machine ) )
  {
    text += line;
    text += '\n';
  }
  return text;
}

bool screenHas( Machine const& machine, std::string const& text )
{
  return screenOf( machine ).contains( text );
}

std::vector<std::uint8_t> readFile( std::filesystem::path const& path )
{
  std::ifstream stream{ path, std::ios::binary };
  return { std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
}

/// Boots, loads the program, types RUN and runs until `last` is on the screen;
/// answers the numbers after each `NAME:` the screen shows.
std::map<std::string, long> runMeasurement( Machine& machine, std::string const& program, std::string const& last )
{
  machine.runFrames( 120 );
  auto const loaded =
      pet::machine::loadProgram( machine, readFile( std::filesystem::path{ PET_TEST_PETTEST_DIR } / program ) );
  REQUIRE( loaded );
  REQUIRE( pet::machine::queueKeys( machine, "RUN\r" ) == 4 );
  auto const run =
      machine.runUntil( [&last]( Machine const& pet ) { return screenHas( pet, last ); }, 60ULL * 16640 * 60 );
  INFO( screenOf( machine ) );
  REQUIRE( run.reason == pet::machine::StopReason::CONDITION );
  // The last line is printed a character at a time: give it a frame.
  machine.runFrames( 2 );
  std::map<std::string, long> values;
  for ( std::string const& line : pet::machine::screenText( machine ) )
  {
    auto const colon = line.find( ':' );
    if ( colon != std::string::npos && colon > 0 )
    {
      std::istringstream number{ line.substr( colon + 1 ) };
      long value = 0;
      if ( number >> value )
      {
        values[line.substr( 0, colon )] = value;
      }
    }
  }
  return values;
}

} // namespace

TEST_CASE( "the PET boots to READY with BASIC 2's banner", "[roms]" )
{
  auto machine = powerUp();
  if ( !machine )
  {
    SKIP( "no ROMs in " << PET_TEST_ROM_DIR );
  }
  machine->runFrames( 120 );
  INFO( screenOf( *machine ) );
  REQUIRE( screenHas( *machine, "COMMODORE BASIC" ) );
  REQUIRE( screenHas( *machine, "31743 BYTES FREE" ) );
  REQUIRE( screenHas( *machine, "READY." ) );
}

TEST_CASE( "irqcost counts the interrupts' cost as the owner's VICE build did", "[roms][pettest]" )
{
  auto machine = powerUp();
  if ( !machine || !std::filesystem::exists( std::filesystem::path{ PET_TEST_PETTEST_DIR } / "irqcost.prg" ) )
  {
    SKIP( "needs the ROMs and ../PETTest/irqcost.prg" );
  }
  auto const values = runMeasurement( *machine, "irqcost.prg", "N3:" );
  std::ostringstream all;
  for ( auto const& [name, value] : values )
  {
    all << name << '=' << value << ' ';
  }
  INFO( all.str() );
  CHECK( values.at( "SEI" ) == 57911 );
  CHECK( values.at( "ROM" ) == 65520 );
  CHECK( values.at( "N2" ) == 5 );
  CHECK( values.at( "MIN" ) == 58128 );
  CHECK( values.at( "N3" ) == 3 );
}

TEST_CASE( "vsync times the retrace by the PIA flag and by VIA PB5", "[roms][pettest]" )
{
  auto machine = powerUp();
  if ( !machine || !std::filesystem::exists( std::filesystem::path{ PET_TEST_PETTEST_DIR } / "vsync.prg" ) )
  {
    SKIP( "needs the ROMs and ../PETTest/vsync.prg" );
  }
  auto const values = runMeasurement( *machine, "vsync.prg", "HI:" );
  std::ostringstream all;
  for ( auto const& [name, value] : values )
  {
    all << name << '=' << value << ' ';
  }
  INFO( all.str() );
  // Three frames by the PIA's flag, less the measurement's own cycles, as
  // README section 11 has it.
  CHECK( values.at( "PIA" ) == 49841 );
  // The two-phase poll of PB5 is not short: its first wait ends at the first
  // blank after the timer starts, 1400 cycles on here, and only the other two
  // are whole frames.
  CHECK( values.at( "VIA" ) == ( 2 * 16640 ) + 1400 );
  CHECK( values.at( "LO" ) == 3742 );
  CHECK( values.at( "HI" ) == 12729 );
}
