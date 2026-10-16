#include "Methods.hpp"

#include "pet/machine/Programs.hpp"

#include <spdlog/fmt/fmt.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <vector>

namespace pet::control
{

namespace
{

constexpr std::uint16_t BASIC_START = 0x0401;

Error loadFailed( std::string message )
{
  return Error{ .code = "load_failed", .message = std::move( message ) };
}

std::optional<std::vector<std::uint8_t>> bytesOfHex( std::string const& text )
{
  if ( text.size() % 2 != 0 )
  {
    return std::nullopt;
  }
  std::vector<std::uint8_t> bytes;
  for ( std::size_t at = 0; at < text.size(); at += 2 )
  {
    unsigned value = 0;
    for ( char const c : text.substr( at, 2 ) )
    {
      value <<= 4U;
      if ( c >= '0' && c <= '9' )
      {
        value |= static_cast<unsigned>( c - '0' );
      }
      else if ( c >= 'a' && c <= 'f' )
      {
        value |= static_cast<unsigned>( c - 'a' + 10 );
      }
      else if ( c >= 'A' && c <= 'F' )
      {
        value |= static_cast<unsigned>( c - 'A' + 10 );
      }
      else
      {
        return std::nullopt;
      }
    }
    bytes.push_back( static_cast<std::uint8_t>( value ) );
  }
  return bytes;
}

} // namespace

void addProgramMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add(
      "program.load",
      info( "Puts a .prg into RAM at its load address as BASIC's LOAD does, setting where BASIC's variables begin "
            "after it. With run, types RUN for a program at $0401, BASIC's start, and SYS to its first byte for "
            "any other, into the keyboard buffer: the PET runs it when it next runs, from READY. Give path or "
            "data_hex.",
            { { .name = "path", .type = "string", .description = "A .prg file.", .required = false },
              { .name = "data_hex",
                .type = "string",
                .description = "The .prg's bytes, its load address first, two hex digits each.",
                .required = false },
              { .name = "run",
                .type = "boolean",
                .description = "Run it as well; false when left out.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        if ( params.contains( "path" ) == params.contains( "data_hex" ) )
        {
          return std::unexpected( badRequest( "give one of path and data_hex" ) );
        }
        if ( params.contains( "run" ) && !params.at( "run" ).is_boolean() )
        {
          return std::unexpected( badRequest( "run is true or false" ) );
        }
        std::vector<std::uint8_t> bytes;
        if ( params.contains( "path" ) )
        {
          auto const path = requireString( params, "path" );
          if ( !path )
          {
            return std::unexpected( path.error() );
          }
          std::ifstream file{ std::filesystem::path{ *path }, std::ios::binary };
          if ( !file )
          {
            return std::unexpected( loadFailed( fmt::format( "Cannot read {}", *path ) ) );
          }
          bytes.assign( std::istreambuf_iterator<char>{ file }, std::istreambuf_iterator<char>{} );
        }
        else
        {
          auto const text = requireString( params, "data_hex" );
          auto const parsed = text ? bytesOfHex( *text ) : std::nullopt;
          if ( !parsed )
          {
            return std::unexpected( badRequest( "data_hex is bytes, two hex digits each" ) );
          }
          bytes = *parsed;
        }
        auto const loaded = machine::loadProgram( **machine, bytes );
        if ( !loaded )
        {
          return std::unexpected( loadFailed( loaded.error() ) );
        }
        Json result{ { "start", loaded->start }, { "end", loaded->end } };
        if ( params.value( "run", false ) )
        {
          std::string const command =
              loaded->start == BASIC_START ? std::string{ "RUN\r" } : fmt::format( "SYS{}\r", loaded->start );
          machine::queueKeys( **machine, command );
          result["typed"] = command.substr( 0, command.size() - 1 );
        }
        return result;
      } );
}

} // namespace pet::control
