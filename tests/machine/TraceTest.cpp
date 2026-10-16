#include <catch2/catch_test_macros.hpp>

#include "pet/machine/Machine.hpp"

#include <spdlog/fmt/fmt.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// The first two seconds after power-up against a trace VICE's xpet wrote of the
// same ROMs (../PETTest/trace_1s.txt): every instruction's address, the cycle
// it begins in and the registers before it. Skips without the trace or the
// ROMs.

using pet::machine::Machine;

namespace
{

struct TracedInstruction
{
  std::uint16_t pc{};
  std::uint64_t clock{};
  std::uint8_t a{};
  std::uint8_t x{};
  std::uint8_t y{};
  std::uint8_t s{};
  std::string line;
};

/// A line of VICE's trace: `.PC raster-line raster-cycle clock bytes...
/// mnemonic AAXXYYSP`. The raster columns count VICE's monitor's frame, not
/// the PET's video, and are not read.
std::optional<TracedInstruction> parse( std::string const& line )
{
  if ( line.size() < 6 || line[0] != '.' )
  {
    return std::nullopt;
  }
  std::istringstream fields{ line.substr( 1 ) };
  std::string pc;
  std::string rasterLine;
  std::string rasterCycle;
  std::uint64_t clock = 0;
  fields >> pc >> rasterLine >> rasterCycle >> clock;
  std::string const registers = line.substr( line.find_last_of( ' ' ) + 1 );
  if ( registers.size() != 8 )
  {
    return std::nullopt;
  }
  auto const byte = [&registers]( std::size_t at )
  { return static_cast<std::uint8_t>( std::stoul( registers.substr( at, 2 ), nullptr, 16 ) ); };
  return TracedInstruction{ .pc = static_cast<std::uint16_t>( std::stoul( pc, nullptr, 16 ) ),
                            .clock = clock,
                            .a = byte( 0 ),
                            .x = byte( 2 ),
                            .y = byte( 4 ),
                            .s = byte( 6 ),
                            .line = line };
}

} // namespace

TEST_CASE( "from power-up the PET runs as VICE's trace has it, instruction by instruction", "[roms][trace]" )
{
  std::filesystem::path const tracePath = PET_TEST_TRACE;
  auto roms = pet::machine::Roms::load( PET_TEST_ROM_DIR );
  if ( !roms || !std::filesystem::exists( tracePath ) )
  {
    SKIP( "needs " << tracePath << " and the ROMs in " << PET_TEST_ROM_DIR );
  }
  std::vector<TracedInstruction> trace;
  {
    std::ifstream stream{ tracePath };
    std::string line;
    while ( std::getline( stream, line ) )
    {
      if ( auto instruction = parse( line ) )
      {
        trace.push_back( std::move( *instruction ) );
      }
    }
  }
  REQUIRE( trace.size() > 500000 );

  Machine machine{ std::move( *roms ) };
  std::size_t next = 0;
  std::string mismatch;
  auto const check = [&]( Machine const& pet )
  {
    if ( !mismatch.empty() || next >= trace.size() )
    {
      return;
    }
    TracedInstruction const& expected = trace[next];
    auto const cpu = pet.cpu();
    // The hook runs after the fetch's cycle has passed.
    std::uint64_t const clock = pet.now() - 1;
    // VICE's reset leaves S at 0 where the 6502's takes 3 from it; the
    // kernal's first TXS, the second instruction, sets it.
    bool const stackSet = next >= 2;
    if ( cpu.pc != expected.pc || clock != expected.clock || cpu.a != expected.a || cpu.x != expected.x ||
         cpu.y != expected.y || ( stackSet && cpu.s != expected.s ) )
    {
      mismatch = fmt::format( "instruction {}: expected {}\n  got .{:04X} at {} {:02x}{:02x}{:02x}{:02x} p={:02x}",
                              next,
                              expected.line,
                              cpu.pc,
                              clock,
                              cpu.a,
                              cpu.x,
                              cpu.y,
                              cpu.s,
                              cpu.p );
    }
    ++next;
  };
  // The machine stands at its first instruction's fetch.
  check( machine );
  machine.setInstructionHook( check );
  machine.runCycles( trace.back().clock - machine.now() );

  INFO( mismatch );
  REQUIRE( mismatch.empty() );
  REQUIRE( next >= trace.size() );
}
