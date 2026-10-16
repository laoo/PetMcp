#include "Methods.hpp"

#include "pet/machine/Screen.hpp"
#include "pet/video/Screen.hpp"

#include <miniz.h>

#include <filesystem>
#include <fstream>
#include <span>
#include <string_view>
#include <vector>

namespace pet::control
{

namespace
{

/// RGBA pixels as a PNG of RGB pixels.
std::vector<std::uint8_t> encodePng( std::span<std::uint8_t const> rgba, std::size_t width, std::size_t height )
{
  std::vector<std::uint8_t> rgb;
  rgb.reserve( width * height * 3 );
  for ( std::size_t at = 0; at + 3 < rgba.size(); at += 4 )
  {
    rgb.insert( rgb.end(), { rgba[at], rgba[at + 1], rgba[at + 2] } );
  }
  std::size_t size = 0;
  void* const png = tdefl_write_image_to_png_file_in_memory_ex(
      rgb.data(), static_cast<int>( width ), static_cast<int>( height ), 3, &size, MZ_DEFAULT_LEVEL, 0 );
  if ( png == nullptr )
  {
    return {};
  }
  std::vector<std::uint8_t> bytes( static_cast<std::uint8_t*>( png ), static_cast<std::uint8_t*>( png ) + size );
  mz_free( png );
  return bytes;
}

std::string base64( std::span<std::uint8_t const> bytes )
{
  static constexpr std::string_view ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string text;
  text.reserve( ( ( bytes.size() + 2 ) / 3 ) * 4 );
  for ( std::size_t at = 0; at < bytes.size(); at += 3 )
  {
    std::uint32_t const chunk = ( static_cast<std::uint32_t>( bytes[at] ) << 16U ) |
                                ( at + 1 < bytes.size() ? static_cast<std::uint32_t>( bytes[at + 1] ) << 8U : 0U ) |
                                ( at + 2 < bytes.size() ? bytes[at + 2] : 0U );
    text.push_back( ALPHABET[( chunk >> 18U ) & 63U] );
    text.push_back( ALPHABET[( chunk >> 12U ) & 63U] );
    text.push_back( at + 1 < bytes.size() ? ALPHABET[( chunk >> 6U ) & 63U] : '=' );
    text.push_back( at + 2 < bytes.size() ? ALPHABET[chunk & 63U] : '=' );
  }
  return text;
}

Error screenshotFailed( std::string message )
{
  return Error{ .code = "screenshot_failed", .message = std::move( message ) };
}

} // namespace

void addVideoMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add( "video.screenshot",
                  info( "The last complete picture, the 320 by 200 dots of the 40 by 25 characters, completed where "
                        "the vertical blank begins: lit "
                        "dots white, the rest black. Written to path as a PNG, or returned as one.",
                        { { .name = "path",
                            .type = "string",
                            .description = "Where to write the PNG; left out, it is returned.",
                            .required = false } } ),
                  [&emulator]( Json const& params ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    auto const png = encodePng( ( *machine )->picture(), video::SCREEN_WIDTH, video::SCREEN_HEIGHT );
                    if ( png.empty() )
                    {
                      return std::unexpected( screenshotFailed( "The picture could not be encoded" ) );
                    }
                    Json result{ { "width", video::SCREEN_WIDTH },
                                 { "height", video::SCREEN_HEIGHT },
                                 { "frame", ( *machine )->picturesDrawn() } };
                    if ( params.contains( "path" ) )
                    {
                      auto const path = requireString( params, "path" );
                      if ( !path )
                      {
                        return std::unexpected( path.error() );
                      }
                      std::ofstream file{ std::filesystem::path{ *path }, std::ios::binary };
                      file.write( reinterpret_cast<char const*>( png.data() ),
                                  static_cast<std::streamsize>( png.size() ) );
                      if ( !file )
                      {
                        return std::unexpected( screenshotFailed( "Cannot write " + *path ) );
                      }
                      result["path"] = *path;
                      return result;
                    }
                    result["png_base64"] = base64( png );
                    return result;
                  } );

  dispatcher.add( "video.screen_text",
                  info( "The 25 lines of the screen as text, as the character set on show draws them: letters, "
                        "digits and punctuation as themselves, graphics as the nearest Unicode character." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    return Json{ { "lines", machine::screenText( **machine ) },
                                 { "character_set", ( *machine )->textSet() ? "text" : "graphics" } };
                  } );
}

} // namespace pet::control
