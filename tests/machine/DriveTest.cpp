#include <catch2/catch_test_macros.hpp>

#include "pet/Emulator.hpp"
#include "pet/control/Dispatcher.hpp"
#include "support/DiskImage.hpp"
#include "support/Files.hpp"

#include <filesystem>
#include <fstream>
#include <string>

// The SD2PET on the PET's IEEE-488 port, driven as a person at the PET's
// keyboard would drive it, and through the protocol. Needs VICE's ROMs; skips
// without them. The disk, hello.d64, is built by support/DiskImage.cpp: HELLO
// and SECOND, the same BASIC program, 10 PRINT"HELLO FROM DISK".

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

Json fails( pet::control::Dispatcher const& dispatcher, std::string const& method, Json params )
{
  Json const response =
      dispatcher.handle( Json{ { "id", 1 }, { "method", method }, { "params", std::move( params ) } } );
  INFO( method << ": " << response.dump() );
  REQUIRE( response.at( "ok" ) == false );
  return response.at( "error" );
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

/// A directory holding hello.d64, for as long as it lives.
class HelloDisk
{
public:
  HelloDisk()
  {
    pet::test::writeFile( path(), pet::test::helloDisk() );
  }

  [[nodiscard]] std::filesystem::path path() const
  {
    return mDirectory.path() / "hello.d64";
  }

private:
  pet::test::TemporaryDirectory mDirectory;
};

pet::Settings withRoms()
{
  pet::Settings settings;
  settings.romDirectory = PET_TEST_ROM_DIR;
  return settings;
}

/// Types `text` and runs the PET until READY. follows it, or for 10 seconds.
void typeAndWait( pet::control::Dispatcher const& dispatcher, std::string const& text )
{
  call( dispatcher, "input.type", { { "text", text } } );
  call( dispatcher, "emu.run_frames", { { "count", 600 } } );
}

} // namespace

TEST_CASE( "the SD2PET says what it is after power-up, and again after the PET's reset", "[roms][drive]" )
{
  pet::Emulator emulator{ withRoms() };
  if ( emulator.machine() == nullptr )
  {
    SKIP( "no ROMs in " << PET_TEST_ROM_DIR );
  }
  pet::control::Dispatcher const dispatcher{ emulator };
  Json const drive = call( dispatcher, "disk.status" );
  CHECK( drive.at( "status" ) == "73,NODISKEMU V1.0.0ALPHA0-20260918-E51A6DF,00,00" );
  CHECK( drive.at( "device" ) == 8 );
  CHECK( drive.at( "card" ).get<std::string>().empty() );

  // Without a card, NODISKEMU's I answers as a drive with no disk would.
  CHECK( call( dispatcher, "disk.command", { { "command", "i" } } ).at( "status" ) == "21,READ ERROR,18,00" );
  call( dispatcher, "emu.reset" );
  CHECK( call( dispatcher, "disk.status" ).at( "status" ) == "73,NODISKEMU V1.0.0ALPHA0-20260918-E51A6DF,00,00" );
}

TEST_CASE( "a D64 is mounted, listed on the PET's screen, and a program in it loaded and run", "[roms][drive]" )
{
  pet::Emulator emulator{ withRoms() };
  if ( emulator.machine() == nullptr )
  {
    SKIP( "no ROMs in " << PET_TEST_ROM_DIR );
  }
  pet::control::Dispatcher const dispatcher{ emulator };
  REQUIRE( call( dispatcher, "emu.run_until", { { "screen_contains", "READY." } } ).at( "reason" ) == "condition_met" );

  HelloDisk const hello;
  Json const mounted = call( dispatcher, "disk.mount", { { "path", hello.path().string() } } );
  CHECK( mounted.at( "status" ) == "00, OK,00,00" );
  CHECK( std::filesystem::path{ mounted.at( "image" ).get<std::string>() }.filename() == "hello.d64" );

  typeAndWait( dispatcher, "load\"$\",8\n" );
  typeAndWait( dispatcher, "list\n" );
  std::string const listing = screen( dispatcher );
  INFO( listing );
  CHECK( listing.contains( "\"PETMCP TEST     \" PM 2A" ) );
  CHECK( listing.contains( "1    \"HELLO\"            PRG" ) );
  CHECK( listing.contains( "1    \"SECOND\"           PRG" ) );
  CHECK( listing.contains( "662 BLOCKS FREE." ) );

  typeAndWait( dispatcher, "load\"second\",8\n" );
  typeAndWait( dispatcher, "run\n" );
  std::string const ran = screen( dispatcher );
  INFO( ran );
  CHECK( ran.contains( "HELLO FROM DISK" ) );
  CHECK( call( dispatcher, "ieee.trace", { { "count", 1 } } ).at( "length" ).get<unsigned>() > 1000 );
}

TEST_CASE( "disk.run mounts, loads the first file and runs it", "[roms][drive]" )
{
  pet::Emulator emulator{ withRoms() };
  if ( emulator.machine() == nullptr )
  {
    SKIP( "no ROMs in " << PET_TEST_ROM_DIR );
  }
  pet::control::Dispatcher const dispatcher{ emulator };
  HelloDisk const hello;
  Json const ran = call( dispatcher, "disk.run", { { "path", hello.path().string() } } );
  CHECK( ran.at( "typed" ) == "LOAD\"*\",8 RUN" );
  call( dispatcher, "emu.run_frames", { { "count", 30 } } );
  std::string const text = screen( dispatcher );
  INFO( text );
  CHECK( text.contains( "HELLO FROM DISK" ) );

  Json const missing = fails( dispatcher, "disk.run", { { "file", "nothing" } } );
  CHECK( missing.at( "code" ) == "load_failed" );
  CHECK( missing.at( "message" ) == "62,FILE NOT FOUND,00,00" );
  call( dispatcher, "emu.run_frames", { { "count", 30 } } );
  CHECK( screen( dispatcher ).contains( "READY." ) );
}

TEST_CASE( "a program saved on the card is a host file, and loads back", "[roms][drive]" )
{
  pet::test::TemporaryDirectory const card;
  pet::Settings settings = withRoms();
  settings.cardDirectory = card.path();
  pet::Emulator emulator{ settings };
  if ( emulator.machine() == nullptr )
  {
    SKIP( "no ROMs in " << PET_TEST_ROM_DIR );
  }
  pet::control::Dispatcher const dispatcher{ emulator };
  CHECK( call( dispatcher, "disk.status" ).at( "directory" ) == card.path().string() );
  REQUIRE( call( dispatcher, "emu.run_until", { { "screen_contains", "READY." } } ).at( "reason" ) == "condition_met" );

  typeAndWait( dispatcher, "10 print\"saved\"\n" );
  typeAndWait( dispatcher, "save\"keep\",8\n" );
  INFO( screen( dispatcher ) );
  CHECK( call( dispatcher, "disk.status" ).at( "status" ) == "00, OK,00,00" );
  bool found = false;
  for ( auto const& entry : std::filesystem::directory_iterator{ card.path() } )
  {
    // FatFs writes a name that fits 8.3 as its 8.3 name, in capitals.
    found |= entry.path().filename() == "KEEP";
  }
  CHECK( found );

  typeAndWait( dispatcher, "new\n" );
  typeAndWait( dispatcher, "load\"keep\",8\n" );
  typeAndWait( dispatcher, "run\n" );
  CHECK( screen( dispatcher ).contains( "\nSAVED" ) );
}
