#include <catch2/catch_test_macros.hpp>

#include "pet/Emulator.hpp"
#include "pet/control/Dispatcher.hpp"

#include <filesystem>
#include <string>

// The protocol drives the PET as a person at its keyboard would: typed
// commands, programs loaded and run. Needs VICE's ROMs; skips without them.

using pet::control::Json;

namespace
{

Json call( pet::control::Dispatcher const& dispatcher, std::string const& method, Json params = Json::object() )
{
  Json const response =
      dispatcher.handle( Json{ { "id", 1 }, { "method", method }, { "params", std::move( params ) } } );
  INFO( method << ": " << response.dump() );
  REQUIRE( response.at( "ok" ) == true );
  return response.at( "result" );
}

pet::Settings withRoms()
{
  pet::Settings settings;
  settings.romDirectory = PET_TEST_ROM_DIR;
  return settings;
}

std::string screen( pet::control::Dispatcher const& dispatcher )
{
  std::string text;
  for ( auto const& line : call( dispatcher, "video.screen_text" ).at( "lines" ) )
  {
    text += line.get<std::string>() + "\n";
  }
  return text;
}

} // namespace

TEST_CASE( "a typed command runs, and its answer is on the screen", "[roms][control]" )
{
  pet::Emulator emulator{ withRoms() };
  if ( emulator.machine() == nullptr )
  {
    SKIP( "no ROMs in " << PET_TEST_ROM_DIR );
  }
  pet::control::Dispatcher const dispatcher{ emulator };
  REQUIRE( call( dispatcher, "emu.run_until", { { "screen_contains", "READY." } } ).at( "reason" ) == "condition_met" );

  call( dispatcher, "input.type", { { "text", "print 2+2\n" } } );
  call( dispatcher, "emu.run_frames", { { "count", 5 } } );

  INFO( screen( dispatcher ) );
  // Each line is 40 characters, padded with spaces.
  CHECK( screen( dispatcher ).contains( "\nPRINT 2+2      " ) );
  CHECK( screen( dispatcher ).contains( "\n 4      " ) );
}

TEST_CASE( "the history is counted back from now, and searched back on the emulator's side", "[roms][control]" )
{
  pet::Emulator emulator{ withRoms() };
  if ( emulator.machine() == nullptr )
  {
    SKIP( "no ROMs in " << PET_TEST_ROM_DIR );
  }
  pet::control::Dispatcher const dispatcher{ emulator };
  call( dispatcher, "emu.run_frames", { { "count", 200 } } );

  // Index 0 is the instruction the PET stands at.
  auto const newest = call( dispatcher, "debug.trace", { { "count", 2 } } ).at( "instructions" );
  CHECK( newest.at( 0 ).at( "index" ) == 0 );
  CHECK( newest.at( 0 ).at( "pc" ) == call( dispatcher, "cpu.get_state" ).at( "pc" ) );
  CHECK( newest.at( 1 ).at( "cycle" ).get<std::uint64_t>() < newest.at( 0 ).at( "cycle" ).get<std::uint64_t>() );

  // The kernal's interrupt entry, once a frame, newest first: a frame apart,
  // give or take the cycles of the instruction the interrupt waits for.
  auto const aFrameApart = []( pet::control::Json const& later, pet::control::Json const& earlier )
  {
    std::uint64_t const apart = later.at( "cycle" ).get<std::uint64_t>() - earlier.at( "cycle" ).get<std::uint64_t>();
    return apart + 7 >= 16640 && apart <= 16640 + 7;
  };
  auto const entries = call( dispatcher, "debug.trace.find", { { "pc", "$E61B" }, { "limit", 3 } } );
  auto const& matches = entries.at( "matches" );
  REQUIRE( matches.size() == 3 );
  for ( std::size_t i = 0; i + 1 < matches.size(); ++i )
  {
    CHECK( aFrameApart( matches.at( i ), matches.at( i + 1 ) ) );
    CHECK( matches.at( i ).at( "index" ).get<std::uint64_t>() <
           matches.at( i + 1 ).at( "index" ).get<std::uint64_t>() );
  }
  // Going on from where it stopped finds the next one back.
  auto const more = call(
      dispatcher, "debug.trace.find", { { "pc", "$E61B" }, { "limit", 1 }, { "from", entries.at( "next_from" ) } } );
  CHECK( aFrameApart( matches.at( 2 ), more.at( "matches" ).at( 0 ) ) );

  // By the disassembly as it ran: the keyboard's columns read.
  auto const reads = call( dispatcher, "debug.trace.find", { { "disasm_contains", "lda $e812" }, { "limit", 1 } } );
  REQUIRE( reads.at( "matches" ).size() == 1 );
  CHECK( reads.at( "matches" ).at( 0 ).at( "bytes_hex" ) == "ad12e8" );
}

TEST_CASE( "a program loaded with run runs, and sees the keys input.set holds", "[roms][control]" )
{
  pet::Emulator emulator{ withRoms() };
  std::filesystem::path const program = std::filesystem::path{ PET_TEST_PETTEST_DIR } / "kbscan.prg";
  if ( emulator.machine() == nullptr || !std::filesystem::exists( program ) )
  {
    SKIP( "needs the ROMs and " << program );
  }
  pet::control::Dispatcher const dispatcher{ emulator };
  call( dispatcher, "emu.run_until", { { "screen_contains", "READY." } } );

  auto const loaded = call( dispatcher, "program.load", { { "path", program.string() }, { "run", true } } );
  CHECK( loaded.at( "start" ) == 0x0401 );
  CHECK( loaded.at( "typed" ) == "RUN" );
  call( dispatcher, "input.set", { { "key", "a" }, { "down", true } } );
  call( dispatcher, "emu.run_until", { { "screen_contains", "FRAMES:" } } );

  // kbscan prints the matrix as it read it: A, row 4, bit 0, down.
  INFO( screen( dispatcher ) );
  CHECK( screen( dispatcher ).contains( "ROWS: 255 255 255 255 254 255 255 255 25\n5 255" ) );
  CHECK( call( dispatcher, "input.get_state" ).at( "held" ) == Json::array( { "A" } ) );
}
