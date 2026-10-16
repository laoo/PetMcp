// The headless command line is deliberately thin: it parses arguments, wires a
// transport to the dispatcher and hands over. Everything testable lives in
// pet_core and pet_server.

#include "pet/Emulator.hpp"
#include "pet/Version.hpp"
#include "pet/control/Dispatcher.hpp"
#include "pet/server/JsonLinesServer.hpp"
#include "pet/server/McpServer.hpp"
#include "pet/server/TcpLineServer.hpp"

#include <CLI/CLI.hpp>
#include <spdlog/fmt/fmt.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace
{

/// `answer` with the build's version written `devel`, as a transcript holds
/// it, so that any build checks a transcript alike.
std::string asRecorded( std::string answer )
{
  std::string const version{ pet::versionString() };
  for ( std::size_t at = answer.find( version ); at != std::string::npos; at = answer.find( version, at ) )
  {
    answer.replace( at, version.size(), "devel" );
    at += 5;
  }
  return answer;
}

/// Answers the requests of the transcript at `path`, one a line, as JSON
/// lines or as MCP messages, and prints the answers; or, given `expected`,
/// compares them with its lines (docs/spec/control-protocol.md §1).
int runTranscript( pet::control::Dispatcher const& dispatcher,
                   std::filesystem::path const& path,
                   bool mcp,
                   std::filesystem::path const& expected )
{
  std::ifstream requests{ path };
  if ( !requests )
  {
    std::cerr << "petmcp-cli: cannot read " << path.string() << '\n';
    return 2;
  }
  pet::server::JsonLinesServer const lines{ dispatcher };
  pet::server::McpServer mcpServer{ dispatcher };
  std::vector<std::string> answers;
  std::string line;
  while ( std::getline( requests, line ) )
  {
    if ( !line.empty() && line.back() == '\r' )
    {
      line.pop_back();
    }
    if ( line.empty() || line.starts_with( '#' ) )
    {
      continue;
    }
    if ( !mcp )
    {
      answers.push_back( asRecorded( lines.handleLine( line ) ) );
    }
    else if ( auto answer = mcpServer.handleLine( line ) )
    {
      answers.push_back( asRecorded( std::move( *answer ) ) );
    }
  }

  if ( expected.empty() )
  {
    for ( std::string const& answer : answers )
    {
      std::cout << answer << '\n';
    }
    return 0;
  }
  std::ifstream file{ expected };
  std::vector<std::string> recorded;
  while ( std::getline( file, line ) )
  {
    if ( !line.empty() && line.back() == '\r' )
    {
      line.pop_back();
    }
    recorded.push_back( line );
  }
  for ( std::size_t i = 0; i < std::max( answers.size(), recorded.size() ); ++i )
  {
    std::string const got = i < answers.size() ? answers[i] : "(no answer)";
    std::string const want = i < recorded.size() ? recorded[i] : "(no answer)";
    if ( got != want )
    {
      std::cout << fmt::format(
          "{}: answer {} differs\n  expected: {}\n  got:      {}\n", path.filename().string(), i + 1, want, got );
      return 1;
    }
  }
  std::cout << fmt::format( "{}: {} answers match\n", path.filename().string(), answers.size() );
  return 0;
}

int run( int argc, char** argv )
{
  CLI::App app{ "Headless Commodore PET 3032 emulator", "petmcp-cli" };
  app.set_version_flag( "--version", std::string{ pet::versionString() } );

  pet::Settings settings;
  app.add_option( "--rom-dir", settings.romDirectory, "Where the PET's ROMs are, under the names VICE gives them" )
      ->check( CLI::ExistingDirectory );
  app.add_option( "--card-dir", settings.cardDirectory, "The directory that stands for the SD2PET's SD card" )
      ->check( CLI::ExistingDirectory );
  bool noExpansion = false;
  app.add_flag( "--no-expansion", noExpansion, "Leave the $9000-$BFFF sockets empty, without the expansion board" );
  app.add_option( "--state-dir",
                  settings.stateDirectory,
                  "Where state.save and state.load keep save states given by name; the working directory if not given" )
      ->check( CLI::ExistingDirectory );

  std::string serverSpec;
  auto* const serverFlag =
      app.add_option( "--server",
                      serverSpec,
                      "Answer JSON-lines requests: on stdin and stdout, or with tcp:PORT on 127.0.0.1:PORT until "
                      "the process is ended" )
          ->expected( 0, 1 );
  bool mcp = false;
  app.add_flag( "--mcp", mcp, "Serve the Model Context Protocol on stdio, for an agent" )->excludes( serverFlag );
  std::filesystem::path transcript;
  auto* const transcriptOption =
      app.add_option( "--transcript",
                      transcript,
                      "Answer the requests in a file, one a line (MCP messages with --mcp), and print the answers" )
          ->check( CLI::ExistingFile )
          ->excludes( serverFlag );
  std::filesystem::path expected;
  app.add_option( "--expect", expected, "With --transcript: compare the answers with this file's lines instead" )
      ->check( CLI::ExistingFile )
      ->needs( transcriptOption );

  CLI11_PARSE( app, argc, argv );
  settings.expansionBoard = !noExpansion;

  if ( !serverSpec.empty() && !serverSpec.starts_with( "tcp:" ) )
  {
    std::cerr << "petmcp-cli: --server: expected tcp:PORT, not " << serverSpec << '\n';
    return 1;
  }
  std::uint16_t port = 0;
  if ( !serverSpec.empty() )
  {
    std::string_view const digits = std::string_view{ serverSpec }.substr( 4 );
    auto const [end, error] = std::from_chars( digits.data(), digits.data() + digits.size(), port );
    if ( digits.empty() || error != std::errc{} || end != digits.data() + digits.size() )
    {
      std::cerr << "petmcp-cli: --server: expected tcp:PORT, a port from 0 to 65535, not " << serverSpec << '\n';
      return 1;
    }
  }

  // stdout carries the protocol and nothing else, so every log line goes to
  // stderr; a client parsing stdout must never meet one.
  spdlog::set_default_logger( spdlog::stderr_color_mt( "petmcp" ) );

  bool const server = serverFlag->count() > 0;
  if ( !server && !mcp && transcript.empty() )
  {
    std::cerr << app.help();
    return 1;
  }

  pet::Emulator emulator{ std::move( settings ) };
  pet::control::Dispatcher const dispatcher{ emulator };
  if ( !transcript.empty() )
  {
    return runTranscript( dispatcher, transcript, mcp, expected );
  }
  if ( mcp )
  {
    pet::server::McpServer transport{ dispatcher };
    spdlog::info( "petmcp-cli {} serving MCP on stdio", pet::versionString() );
    transport.serve( std::cin, std::cout );
    return 0;
  }
  if ( !serverSpec.empty() )
  {
    // Connections are served on threads of their own; the dispatcher answers
    // one request at a time.
    std::mutex oneAtATime;
    pet::server::TcpLineServer const transport{ port,
                                                [&]( pet::control::Json const& request )
                                                {
                                                  std::scoped_lock const lock{ oneAtATime };
                                                  return dispatcher.handle( request );
                                                } };
    spdlog::info( "petmcp-cli {} serving JSON-lines on 127.0.0.1:{}", pet::versionString(), transport.port() );
    for ( ;; )
    {
      std::this_thread::sleep_for( std::chrono::hours{ 1 } );
    }
  }
  pet::server::JsonLinesServer const transport{ dispatcher };
  spdlog::info( "petmcp-cli {} serving JSON-lines on stdio", pet::versionString() );
  transport.serve( std::cin, std::cout );
  return 0;
}

} // namespace

// Nothing may escape main: an unhandled exception would abort without telling
// the user anything useful, and CLI11 reports argument problems by throwing.
// The handlers write to stderr directly rather than through spdlog, because
// logging may allocate or throw and would then be the second failure in a row.
int main( int argc, char** argv )
{
  try
  {
    return run( argc, argv );
  }
  catch ( std::exception const& error )
  {
    std::fprintf( stderr, "petmcp-cli: %s\n", error.what() );
    return 2;
  }
  catch ( ... )
  {
    std::fputs( "petmcp-cli: internal error: unknown exception\n", stderr );
    return 2;
  }
}
