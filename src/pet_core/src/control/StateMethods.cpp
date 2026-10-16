#include "Methods.hpp"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

namespace pet::control
{

namespace
{

constexpr std::string_view STATE_EXTENSION = ".petstate";

Error stateFailed( std::string message )
{
  return Error{ .code = "state_failed", .message = std::move( message ) };
}

/// Where `filename` is: in the state directory when it names no directory.
std::filesystem::path pathOf( Emulator const& emulator, std::string const& filename )
{
  std::filesystem::path path{ filename };
  if ( path.has_parent_path() || emulator.settings().stateDirectory.empty() )
  {
    return path;
  }
  return emulator.settings().stateDirectory / path;
}

} // namespace

void addStateMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add( "state.save",
                  info( "Writes the whole PET's state to a file: all a run changes, without the ROMs, breakpoints, "
                        "watchpoints or the trace. A name without a directory is in the state directory.",
                        { { .name = "filename", .type = "string", .description = "Where to write it." } } ),
                  [&emulator]( Json const& params ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    auto const filename = requireString( params, "filename" );
                    if ( !filename )
                    {
                      return std::unexpected( filename.error() );
                    }
                    std::filesystem::path const path = pathOf( emulator, *filename );
                    std::vector<std::uint8_t> const state = ( *machine )->saveState();
                    std::ofstream file{ path, std::ios::binary };
                    file.write( reinterpret_cast<char const*>( state.data() ),
                                static_cast<std::streamsize>( state.size() ) );
                    if ( !file )
                    {
                      return std::unexpected( stateFailed( fmt::format( "Cannot write {}", path.string() ) ) );
                    }
                    return Json{ { "filename", path.string() }, { "size", state.size() } };
                  } );

  dispatcher.add(
      "state.load",
      info( "Restores a state state.save wrote, of a PET with the same ROMs, by this build.",
            { { .name = "filename", .type = "string", .description = "The state's file." } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        auto const filename = requireString( params, "filename" );
        if ( !filename )
        {
          return std::unexpected( filename.error() );
        }
        std::filesystem::path const path = pathOf( emulator, *filename );
        std::ifstream file{ path, std::ios::binary };
        if ( !file )
        {
          return std::unexpected( stateFailed( fmt::format( "Cannot read {}", path.string() ) ) );
        }
        std::vector<std::uint8_t> const state{ std::istreambuf_iterator<char>{ file },
                                               std::istreambuf_iterator<char>{} };
        if ( auto const loaded = ( *machine )->loadState( state ); !loaded )
        {
          return std::unexpected(
              Error{ .code = "state_mismatch", .message = fmt::format( "{}: {}", path.string(), loaded.error() ) } );
        }
        return Json::object();
      } );

  dispatcher.add( "state.list",
                  info( "The state files in the state directory, by name." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    std::filesystem::path const directory = emulator.settings().stateDirectory.empty()
                                                                ? std::filesystem::path{ "." }
                                                                : emulator.settings().stateDirectory;
                    std::vector<std::string> names;
                    std::error_code failed;
                    for ( auto const& entry : std::filesystem::directory_iterator( directory, failed ) )
                    {
                      if ( entry.path().extension() == STATE_EXTENSION )
                      {
                        names.push_back( entry.path().filename().string() );
                      }
                    }
                    std::ranges::sort( names );
                    return Json{ { "states", std::move( names ) } };
                  } );
}

} // namespace pet::control
