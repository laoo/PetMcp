#include <catch2/catch_test_macros.hpp>

#include "pet/Emulator.hpp"
#include "pet/control/Dispatcher.hpp"

#include <array>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

// The SD2PET held to the owner's measurements of the real one: the bench
// program of ../PETTest, built for each of its three receivers, run from its
// own D64, and its counts compared with ../PETTest/README.md §8. Needs VICE's
// ROMs and ../PETTest; skips without them.

using pet::control::Json;

namespace
{

/// A receiver's counts on the hardware (../PETTest/README.md §8).
struct Measured
{
  std::string_view disk;
  int u1;
  int talk;
  int untalk;
  int perByte;
  int sector;
  int sequential;
};

constexpr std::array<Measured, 3> HARDWARE{ {
    { .disk = "bench-chrin.d64",
      .u1 = 4872,
      .talk = 435,
      .untalk = 1230,
      .perByte = 152,
      .sector = 45544,
      .sequential = 167 },
    { .disk = "bench-fast.d64",
      .u1 = 4867,
      .talk = 431,
      .untalk = 2250,
      .perByte = 111,
      .sector = 36024,
      .sequential = 127 },
    { .disk = "bench-inline.d64",
      .u1 = 4852,
      .talk = 439,
      .untalk = 2250,
      .perByte = 58,
      .sector = 22431,
      .sequential = 74 },
} };

/// What UNTALK really takes. The bench reads timer 2's low byte, then its
/// high byte; when the low byte passes 0 between the two reads, t_start
/// reads 256 too few, and an interval as short as UNTALK comes out negative,
/// 65536 more. Each such sample adds 1016 to the average of 64: the
/// hardware's 1230 and 2250 are this with one and with two of them.
constexpr int UNTALK = 214;
constexpr int WRAP = 1016;

/// UNTALK without the samples timer 2 wrapped, and how many wrapped.
std::pair<int, int> unwrapped( int untalk )
{
  int const wraps = ( untalk - UNTALK + ( WRAP / 2 ) ) / WRAP;
  return { untalk - ( wraps * WRAP ), wraps };
}

Json call( pet::control::Dispatcher const& dispatcher, std::string const& method, Json params = Json::object() )
{
  Json const response =
      dispatcher.handle( Json{ { "id", 1 }, { "method", method }, { "params", std::move( params ) } } );
  INFO( method << ": " << response.dump() );
  REQUIRE( response.at( "ok" ) == true );
  return response.at( "result" );
}

/// The bench's report, "NAME: value", from the PET's screen.
std::map<std::string, int> report( pet::control::Dispatcher const& dispatcher )
{
  std::map<std::string, int> values;
  for ( auto const& line : call( dispatcher, "video.screen_text" ).at( "lines" ) )
  {
    std::string const text = line.get<std::string>();
    auto const colon = text.find( ':' );
    if ( colon != std::string::npos && colon < 5 )
    {
      std::string_view number{ text };
      number.remove_prefix( colon + 1 );
      while ( !number.empty() && number.front() == ' ' )
      {
        number.remove_prefix( 1 );
      }
      int value = 0;
      std::from_chars( number.data(), number.data() + number.size(), value );
      values[text.substr( 0, colon )] = value;
    }
  }
  return values;
}

} // namespace

TEST_CASE( "the bench counts what the owner's SD2PET counts, for each receiver", "[roms][pettest][drive]" )
{
  std::filesystem::path const pettest{ PET_TEST_PETTEST_DIR };
  pet::Settings settings;
  settings.romDirectory = PET_TEST_ROM_DIR;
  for ( Measured const& hardware : HARDWARE )
  {
    std::filesystem::path const disk = pettest / hardware.disk;
    if ( !std::filesystem::exists( disk ) )
    {
      SKIP( "no " << disk.string() );
    }
    pet::Emulator emulator{ settings };
    if ( emulator.machine() == nullptr )
    {
      SKIP( "no ROMs in " << PET_TEST_ROM_DIR );
    }
    pet::control::Dispatcher const dispatcher{ emulator };
    call( dispatcher, "disk.run", { { "path", disk.string() } } );
    REQUIRE( call( dispatcher, "emu.run_until", { { "screen_contains", "SMAX" }, { "timeout_cycles", 100'000'000 } } )
                 .at( "reason" ) == "condition_met" );
    call( dispatcher, "emu.run_frames", { { "count", 10 } } );
    auto counts = report( dispatcher );
    std::string shown{ hardware.disk };
    for ( auto const& [name, value] : counts )
    {
      shown += " " + name + "=" + std::to_string( value );
    }
    INFO( shown );

    // The command, TALK and the bytes, within the spread of the hardware's
    // three receivers.
    CHECK( std::abs( counts["U1"] - hardware.u1 ) <= 60 );
    CHECK( std::abs( counts["TALK"] - hardware.talk ) <= 15 );
    CHECK( std::abs( counts["RAW1"] - hardware.perByte ) <= 1 );
    CHECK( std::abs( counts["RAW2"] - hardware.perByte ) <= 1 );
    CHECK( std::abs( counts["SEQ"] - hardware.sequential ) <= 1 );

    // UNTALK, and the whole sector it is part of, once the samples timer 2
    // wrapped are taken out of both: how many wrap depends on the phase of
    // the timer, which no two runs share.
    auto const [untalk, wraps] = unwrapped( counts["UNT"] );
    auto const [hardwareUntalk, hardwareWraps] = unwrapped( hardware.untalk );
    CHECK( std::abs( untalk - hardwareUntalk ) <= 12 );
    int const sector = counts["SECT"] - ( wraps * WRAP );
    int const hardwareSector = hardware.sector - ( hardwareWraps * WRAP );
    CHECK( std::abs( sector - hardwareSector ) <= hardwareSector / 100 );
  }
}

TEST_CASE( "openbench and switch run to their reports", "[roms][pettest][drive]" )
{
  // Their numbers on the hardware are not measured yet
  // (docs/open-questions.md), so only what is already known is held: both
  // end, and the file read beside a # buffer is intact. switch's ERRS is
  // not: its read_block_at1 points at >buf+1, which DASM takes as
  // (>buf)+1, a page past its buffer, so it checks bytes it never stored.
  std::filesystem::path const pettest{ PET_TEST_PETTEST_DIR };
  pet::Settings settings;
  settings.romDirectory = PET_TEST_ROM_DIR;
  for ( auto const& [disk, last] : { std::pair{ "openbench.d64", "CLSD" },
                                     std::pair{ "switch-chrin.d64", "S2FL" },
                                     std::pair{ "switch-inline.d64", "S2FL" } } )
  {
    if ( !std::filesystem::exists( pettest / disk ) )
    {
      SKIP( "no " << ( pettest / disk ).string() );
    }
    pet::Emulator emulator{ settings };
    if ( emulator.machine() == nullptr )
    {
      SKIP( "no ROMs in " << PET_TEST_ROM_DIR );
    }
    pet::control::Dispatcher const dispatcher{ emulator };
    call( dispatcher, "disk.run", { { "path", ( pettest / disk ).string() } } );
    INFO( disk );
    REQUIRE( call( dispatcher, "emu.run_until", { { "screen_contains", last }, { "timeout_cycles", 100'000'000 } } )
                 .at( "reason" ) == "condition_met" );
    call( dispatcher, "emu.run_frames", { { "count", 10 } } );
    if ( std::string_view{ disk }.starts_with( "switch" ) )
    {
      auto counts = report( dispatcher );
      CHECK( counts["ERRF"] == 0 );
      CHECK( counts["RESU"] == 1 );
      CHECK( counts["DOS"] == 0 );
    }
  }
}
