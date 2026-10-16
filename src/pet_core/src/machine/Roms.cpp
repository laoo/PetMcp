#include "pet/machine/Roms.hpp"

#include <spdlog/fmt/fmt.h>

#include <fstream>
#include <iterator>
#include <system_error>

namespace pet::machine
{

namespace
{

std::expected<std::vector<std::uint8_t>, std::string>
readRom( std::filesystem::path const& directory, std::string_view name, std::size_t size )
{
  std::filesystem::path const path = directory / name;
  std::error_code failed;
  if ( !std::filesystem::is_regular_file( path, failed ) )
  {
    return std::unexpected( fmt::format( "no {} in {}", name, directory.string() ) );
  }
  std::ifstream stream{ path, std::ios::binary };
  std::vector<std::uint8_t> bytes{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
  if ( bytes.size() != size )
  {
    return std::unexpected( fmt::format( "{} holds {} bytes, not {}", path.string(), bytes.size(), size ) );
  }
  return bytes;
}

} // namespace

std::expected<Roms, std::string> Roms::load( std::filesystem::path const& directory )
{
  auto basic = readRom( directory, BASIC_FILE, 0x2000 );
  if ( !basic )
  {
    return std::unexpected( basic.error() );
  }
  auto editor = readRom( directory, EDITOR_FILE, 0x800 );
  if ( !editor )
  {
    return std::unexpected( editor.error() );
  }
  auto kernal = readRom( directory, KERNAL_FILE, 0x1000 );
  if ( !kernal )
  {
    return std::unexpected( kernal.error() );
  }
  auto characters = readRom( directory, CHARACTERS_FILE, 0x800 );
  if ( !characters )
  {
    return std::unexpected( characters.error() );
  }
  return Roms{ .basic = std::move( *basic ),
               .editor = std::move( *editor ),
               .kernal = std::move( *kernal ),
               .characters = std::move( *characters ) };
}

} // namespace pet::machine
