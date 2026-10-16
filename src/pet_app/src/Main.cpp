// The desktop entry point: everything it does is Application's.

#include "Application.hpp"

#include "pet/Version.hpp"

#include <CLI/CLI.hpp>
#include <SDL3/SDL_main.h>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>

namespace
{

int run( int argc, char** argv )
{
  CLI::App app{ "Commodore PET 3032 emulator", "petmcp" };
  app.set_version_flag( "--version", std::string{ pet::versionString() } );
  pet::Settings settings;
  app.add_option( "--rom-dir", settings.romDirectory, "Where the PET's ROMs are, under the names VICE gives them" )
      ->check( CLI::ExistingDirectory );
  app.add_option( "--card-dir", settings.cardDirectory, "The directory that stands for the SD2PET's SD card" )
      ->check( CLI::ExistingDirectory );
  bool noExpansion = false;
  app.add_flag( "--no-expansion", noExpansion, "Leave the $9000-$BFFF sockets empty, without the expansion board" );
  app.add_option( "--state-dir", settings.stateDirectory, "Where save states given by name are kept" )
      ->check( CLI::ExistingDirectory );
  std::string server;
  app.add_option( "--server", server, "Serve JSON-lines to other programs: tcp:PORT, on 127.0.0.1" )
      ->check( []( std::string const& value )
               { return value.starts_with( "tcp:" ) ? std::string{} : std::string{ "expected tcp:PORT" }; } );
  std::uint16_t mcpPort = 0;
  app.add_option( "--mcp-http", mcpPort, "Serve MCP over HTTP on 127.0.0.1:PORT, at /mcp" );
  std::filesystem::path program;
  app.add_option( "program", program, "A .prg to load and run once the PET is ready" )->check( CLI::ExistingFile );
  CLI11_PARSE( app, argc, argv );
  settings.expansionBoard = !noExpansion;
  std::uint16_t const linePort =
      server.empty() ? std::uint16_t{ 0 } : static_cast<std::uint16_t>( std::stoul( server.substr( 4 ) ) );

  auto application = pet::app::Application::create( std::move( settings ) );
  if ( !application )
  {
    spdlog::critical( "cannot start: {}", application.error() );
    return 1;
  }
  ( *application )->serve( linePort, mcpPort );
  if ( !program.empty() )
  {
    ( *application )->runProgram( program );
  }
  ( *application )->run();
  return 0;
}

} // namespace

// Nothing may escape main, for the reasons petmcp-cli's main gives.
int main( int argc, char** argv )
{
  try
  {
    return run( argc, argv );
  }
  catch ( std::exception const& error )
  {
    std::fprintf( stderr, "petmcp: %s\n", error.what() );
    return 2;
  }
  catch ( ... )
  {
    std::fputs( "petmcp: internal error: unknown exception\n", stderr );
    return 2;
  }
}
