#include "Methods.hpp"

#include <spdlog/fmt/fmt.h>

#include <array>
#include <optional>
#include <string_view>

namespace pet::control
{

namespace
{

/// The largest read or write: the whole address space.
constexpr std::uint64_t MAX_SIZE = 0x10000;

enum class RegionKind : std::uint8_t
{
  CPU,
  RAM,
  VIDEO_RAM,
  BASIC_ROM,
  EDITOR_ROM,
  KERNAL_ROM,
  CHARACTER_ROM,
  EXPANSION_RAM0,
  EXPANSION_RAM1,
  CHARACTER_RAM
};

struct Region
{
  std::string_view name;
  RegionKind kind;
  std::size_t size;
  bool writable;
};

constexpr std::array<Region, 10> REGIONS{ {
    { .name = "CPU", .kind = RegionKind::CPU, .size = 0x10000, .writable = true },
    { .name = "RAM", .kind = RegionKind::RAM, .size = 0x8000, .writable = true },
    { .name = "VIDEO_RAM", .kind = RegionKind::VIDEO_RAM, .size = 0x400, .writable = true },
    { .name = "BASIC_ROM", .kind = RegionKind::BASIC_ROM, .size = 0x2000, .writable = false },
    { .name = "EDITOR_ROM", .kind = RegionKind::EDITOR_ROM, .size = 0x800, .writable = false },
    { .name = "KERNAL_ROM", .kind = RegionKind::KERNAL_ROM, .size = 0x1000, .writable = false },
    { .name = "CHARACTER_ROM", .kind = RegionKind::CHARACTER_ROM, .size = 0x800, .writable = false },
    { .name = "EXPANSION_RAM0", .kind = RegionKind::EXPANSION_RAM0, .size = 0x80000, .writable = false },
    { .name = "EXPANSION_RAM1", .kind = RegionKind::EXPANSION_RAM1, .size = 0x80000, .writable = false },
    { .name = "CHARACTER_RAM", .kind = RegionKind::CHARACTER_RAM, .size = 0x1000, .writable = false },
} };

/// The region `params` name, `CPU` when they name none.
std::expected<Region, Error> regionOf( Json const& params )
{
  if ( !params.contains( "region" ) )
  {
    return REGIONS.front();
  }
  auto const name = requireString( params, "region" );
  if ( !name )
  {
    return std::unexpected( name.error() );
  }
  for ( Region const& region : REGIONS )
  {
    if ( region.name == *name )
    {
      return region;
    }
  }
  return std::unexpected( Error{ .code = "invalid_region", .message = fmt::format( "No region {}", *name ) } );
}

/// The range `params` give within `region`, checked whole.
std::expected<std::pair<std::uint64_t, std::uint64_t>, Error>
rangeOf( Region const& region, std::uint64_t address, std::uint64_t size )
{
  if ( size == 0 || size > MAX_SIZE || address >= region.size || size > region.size - address )
  {
    return std::unexpected(
        Error{ .code = "invalid_range",
               .message = fmt::format(
                   "{} bytes from {:#x} are not all in {}, {:#x} bytes", size, address, region.name, region.size ) } );
  }
  return std::pair{ address, size };
}

std::uint8_t readByte( machine::Machine const& machine, RegionKind kind, std::uint64_t offset )
{
  auto const& roms = machine.roms();
  switch ( kind )
  {
  case RegionKind::CPU:
    return machine.peek( static_cast<std::uint16_t>( offset ) );
  case RegionKind::RAM:
    return machine.ram()[offset];
  case RegionKind::VIDEO_RAM:
    return machine.videoRam()[offset];
  case RegionKind::BASIC_ROM:
    return roms.basic[offset];
  case RegionKind::EDITOR_ROM:
    return roms.editor[offset];
  case RegionKind::KERNAL_ROM:
    return roms.kernal[offset];
  case RegionKind::CHARACTER_ROM:
    return roms.characters[offset];
  case RegionKind::EXPANSION_RAM0:
    return machine.boardMemory( 0 )[offset];
  case RegionKind::EXPANSION_RAM1:
    return machine.boardMemory( 1 )[offset];
  case RegionKind::CHARACTER_RAM:
    return machine.boardMemory( 2 )[offset];
  }
  return 0;
}

/// The CPU address a byte of a writable region is at.
std::uint16_t cpuAddressOf( RegionKind kind, std::uint64_t offset )
{
  return static_cast<std::uint16_t>( kind == RegionKind::VIDEO_RAM ? 0x8000 + offset : offset );
}

std::optional<std::vector<std::uint8_t>> parseHex( std::string const& text )
{
  if ( text.size() % 2 != 0 )
  {
    return std::nullopt;
  }
  auto const digit = []( char c ) -> int
  {
    if ( c >= '0' && c <= '9' )
    {
      return c - '0';
    }
    if ( c >= 'a' && c <= 'f' )
    {
      return c - 'a' + 10;
    }
    if ( c >= 'A' && c <= 'F' )
    {
      return c - 'A' + 10;
    }
    return -1;
  };
  std::vector<std::uint8_t> bytes;
  bytes.reserve( text.size() / 2 );
  for ( std::size_t at = 0; at < text.size(); at += 2 )
  {
    int const high = digit( text[at] );
    int const low = digit( text[at + 1] );
    if ( high < 0 || low < 0 )
    {
      return std::nullopt;
    }
    bytes.push_back( static_cast<std::uint8_t>( ( high << 4 ) | low ) );
  }
  return bytes;
}

} // namespace

void addMemoryMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add( "memory.list_regions",
                  info( "The memory regions memory.read and memory.write address, with their sizes." ),
                  []( Json const& /*params*/ ) -> Outcome
                  {
                    Json regions = Json::array();
                    for ( Region const& region : REGIONS )
                    {
                      regions.push_back(
                          Json{ { "name", region.name }, { "size", region.size }, { "writable", region.writable } } );
                    }
                    return Json{ { "regions", std::move( regions ) } };
                  } );

  dispatcher.add(
      "memory.read",
      info( "Reads bytes, without the side effects a read by the 6502 has: by default as the 6502 sees its address "
            "space, I/O registers and the bus left floating included; or from a region of memory.list_regions.",
            { { .name = "address", .type = "integer", .description = "The first byte's address in the region." },
              { .name = "size", .type = "integer", .description = "How many bytes, at most 65536." },
              { .name = "region",
                .type = "string",
                .description = "CPU when left out; RAM, VIDEO_RAM or one of the ROMs.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        auto const region = regionOf( params );
        if ( !region )
        {
          return std::unexpected( region.error() );
        }
        auto const address = requireUnsigned( params, "address" );
        auto const size = requireUnsigned( params, "size" );
        if ( !address || !size )
        {
          return std::unexpected( !address ? address.error() : size.error() );
        }
        auto const range = rangeOf( *region, *address, *size );
        if ( !range )
        {
          return std::unexpected( range.error() );
        }
        bool const onBoard = region->kind == RegionKind::EXPANSION_RAM0 || region->kind == RegionKind::EXPANSION_RAM1 ||
                             region->kind == RegionKind::CHARACTER_RAM;
        if ( onBoard && ( *machine )->boardMemory( 0 ).empty() )
        {
          return std::unexpected(
              Error{ .code = "invalid_region",
                     .message = fmt::format( "{}: the PET runs without the expansion board", region->name ) } );
        }
        std::string hex;
        hex.reserve( *size * 2 );
        for ( std::uint64_t i = 0; i < *size; ++i )
        {
          hex += fmt::format( "{:02x}", readByte( **machine, region->kind, *address + i ) );
        }
        return Json{ { "region", region->name }, { "address", *address }, { "data_hex", std::move( hex ) } };
      } );

  dispatcher.add(
      "memory.write",
      info( "Writes RAM or video RAM as a debugger does: no bus cycle is made and no device runs. The whole range is "
            "checked first, so it is written whole or not at all; in CPU addresses it must lie in RAM, $0000-$7FFF, "
            "or video RAM, $8000-$87FF.",
            { { .name = "address", .type = "integer", .description = "The first byte's address in the region." },
              { .name = "data_hex", .type = "string", .description = "The bytes, two hex digits each." },
              { .name = "region",
                .type = "string",
                .description = "CPU when left out; RAM or VIDEO_RAM.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        auto const region = regionOf( params );
        if ( !region )
        {
          return std::unexpected( region.error() );
        }
        if ( !region->writable )
        {
          return std::unexpected(
              Error{ .code = "invalid_region", .message = fmt::format( "{} cannot be written", region->name ) } );
        }
        auto const address = requireUnsigned( params, "address" );
        auto const text = requireString( params, "data_hex" );
        if ( !address || !text )
        {
          return std::unexpected( !address ? address.error() : text.error() );
        }
        auto const bytes = parseHex( *text );
        if ( !bytes || bytes->empty() )
        {
          return std::unexpected( badRequest( "data_hex is bytes, two hex digits each" ) );
        }
        auto const range = rangeOf( *region, *address, bytes->size() );
        if ( !range )
        {
          return std::unexpected( range.error() );
        }
        if ( region->kind == RegionKind::CPU && *address + bytes->size() > 0x8800 )
        {
          return std::unexpected(
              Error{ .code = "invalid_range", .message = "Only RAM and video RAM can be written, $0000-$87FF" } );
        }
        for ( std::size_t i = 0; i < bytes->size(); ++i )
        {
          ( *machine )->poke( cpuAddressOf( region->kind, *address + i ), ( *bytes )[i] );
        }
        return Json{ { "region", region->name }, { "address", *address }, { "size", bytes->size() } };
      } );
}

} // namespace pet::control
