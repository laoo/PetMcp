#include <catch2/catch_test_macros.hpp>

#include "cpu/Cpu6502.hpp"
#include "pet/control/Dispatcher.hpp"
#include "support/Files.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// The SingleStepTests 65x02 suite's NMOS 6502 tests
// (https://github.com/SingleStepTests/65x02), run against the 6502 adapter:
// every opcode from a random state, compared with the final state, the memory,
// and every bus cycle, its address, its data and its direction.
// scripts/fetch-6502-tests.sh downloads the suite; without it these skip.

using Json = nlohmann::json;
using pet::cpu::Bus;
using pet::cpu::Cpu6502;
using pet::cpu::Registers;

namespace
{

// Where m6502.h and the suite disagree, by file, as measured on suite commit
// 2f6980a: the twelve JAMs, every case. Both halt the CPU; the suite has it
// read $FFFF, $FFFE, $FFFE and then $FFFF for ever, m6502.h $FFFF from the
// start. The counts are held so that a change to m6502.h that moves any of
// them is noticed; docs/hardware/cpu.md says why none of them matters to the
// PET.
std::map<std::string, std::size_t> const& knownDisagreements()
{
  static std::map<std::string, std::size_t> const COUNTS{
    { "02.json.gz", 10000 }, { "12.json.gz", 10000 }, { "22.json.gz", 10000 }, { "32.json.gz", 10000 },
    { "42.json.gz", 10000 }, { "52.json.gz", 10000 }, { "62.json.gz", 10000 }, { "72.json.gz", 10000 },
    { "92.json.gz", 10000 }, { "b2.json.gz", 10000 }, { "d2.json.gz", 10000 }, { "f2.json.gz", 10000 },
  };
  return COUNTS;
}

std::filesystem::path suiteDirectory()
{
  return PET_TEST_6502_DIR;
}

struct Access
{
  std::uint16_t address{};
  std::uint8_t data{};
  bool read{};

  bool operator==( Access const& ) const = default;
};

/// A 6502 on 64 KB of RAM, every access recorded.
class TestBus final : public Bus
{
public:
  std::array<std::uint8_t, 0x10000> memory{};
  std::vector<Access> accesses;

  std::uint8_t read( std::uint16_t address ) override
  {
    accesses.push_back( Access{ .address = address, .data = memory.at( address ), .read = true } );
    return memory.at( address );
  }

  void write( std::uint16_t address, std::uint8_t value ) override
  {
    accesses.push_back( Access{ .address = address, .data = value, .read = false } );
    memory.at( address ) = value;
  }
};

Registers registersOf( Json const& state )
{
  return Registers{ .pc = state.at( "pc" ).get<std::uint16_t>(),
                    .a = state.at( "a" ).get<std::uint8_t>(),
                    .x = state.at( "x" ).get<std::uint8_t>(),
                    .y = state.at( "y" ).get<std::uint8_t>(),
                    .s = state.at( "s" ).get<std::uint8_t>(),
                    .p = static_cast<std::uint8_t>( state.at( "p" ).get<unsigned>() | Cpu6502::FLAG_BREAK |
                                                    Cpu6502::FLAG_UNUSED ) };
}

/// Runs the test's one instruction: its cycles as the suite counts them, the
/// first being the opcode fetch, then the next instruction's fetch, at which
/// the registers are final. An instruction that ends early stops there.
void execute( Cpu6502& cpu, TestBus& bus, Json const& test )
{
  bus.memory.fill( 0 );
  for ( auto const& cell : test.at( "initial" ).at( "ram" ) )
  {
    bus.memory.at( cell.at( 0 ).get<std::uint16_t>() ) = cell.at( 1 ).get<std::uint8_t>();
  }
  bus.accesses.clear();
  cpu.setRegisters( registersOf( test.at( "initial" ) ), bus );
  std::size_t const cycles = test.at( "cycles" ).size();
  for ( std::size_t cycle = 1; cycle <= cycles; ++cycle )
  {
    cpu.tick( bus );
    if ( cpu.sync() )
    {
      break;
    }
  }
}

/// What differs between the 6502 after the instruction and the test's final
/// state, or nothing.
std::optional<std::string> mismatch( Cpu6502 const& cpu, TestBus const& bus, Json const& test )
{
  std::ostringstream out;
  auto const check = [&]( char const* name, unsigned have, unsigned want )
  {
    if ( have != want )
    {
      out << name << " " << std::hex << have << " != " << want << "; ";
    }
  };

  Json const& cycles = test.at( "cycles" );
  if ( !cpu.sync() )
  {
    out << "no fetch after " << cycles.size() << " cycles; ";
  }
  // The last access recorded is the next instruction's fetch.
  std::vector<Access> wanted;
  for ( auto const& cycle : cycles )
  {
    wanted.push_back( Access{ .address = cycle.at( 0 ).get<std::uint16_t>(),
                              .data = cycle.at( 1 ).get<std::uint8_t>(),
                              .read = cycle.at( 2 ).get<std::string>() == "read" } );
  }
  std::vector<Access> const made{ bus.accesses.begin(),
                                  bus.accesses.end() - ( cpu.sync() && !bus.accesses.empty() ? 1 : 0 ) };
  for ( std::size_t i = 0; i < std::max( made.size(), wanted.size() ); ++i )
  {
    if ( i >= made.size() || i >= wanted.size() || made[i] != wanted[i] )
    {
      out << "cycle " << i << " differs; ";
      break;
    }
  }

  Registers const actual = cpu.registers();
  Registers const expected = registersOf( test.at( "final" ) );
  check( "pc", actual.pc, expected.pc );
  check( "a", actual.a, expected.a );
  check( "x", actual.x, expected.x );
  check( "y", actual.y, expected.y );
  check( "s", actual.s, expected.s );
  check( "p", actual.p, expected.p );
  for ( auto const& cell : test.at( "final" ).at( "ram" ) )
  {
    check( "ram", bus.memory.at( cell.at( 0 ).get<std::uint16_t>() ), cell.at( 1 ).get<unsigned>() );
  }
  std::string const text = out.str();
  return text.empty() ? std::nullopt : std::optional<std::string>{ text };
}

} // namespace

TEST_CASE( "the 6502 passes the SingleStepTests 65x02 suite's NMOS 6502", "[cpu-suite]" )
{
  std::vector<std::filesystem::path> files;
  std::error_code failed;
  for ( auto const& entry : std::filesystem::directory_iterator( suiteDirectory(), failed ) )
  {
    if ( entry.path().string().ends_with( ".json.gz" ) )
    {
      files.push_back( entry.path() );
    }
  }
  if ( files.empty() )
  {
    SKIP( "no suite in " << suiteDirectory() << "; fetch it with scripts/fetch-6502-tests.sh" );
  }
  std::ranges::sort( files );

  Cpu6502 cpu;
  TestBus bus;
  std::ostringstream report;
  std::size_t total = 0;
  std::size_t passed = 0;
  bool unexpected = false;
  for ( auto const& file : files )
  {
    Json const tests = Json::parse( pet::test::readGzip( file ) );
    std::size_t failedHere = 0;
    std::string firstFailure;
    for ( auto const& test : tests )
    {
      execute( cpu, bus, test );
      auto const problem = mismatch( cpu, bus, test );
      ++total;
      if ( problem )
      {
        if ( failedHere++ == 0 )
        {
          firstFailure = test.at( "name" ).get<std::string>() + ": " + *problem;
        }
      }
      else
      {
        ++passed;
      }
    }
    auto const known = knownDisagreements().find( file.filename().string() );
    std::size_t const expected = known == knownDisagreements().end() ? 0 : known->second;
    if ( failedHere != expected )
    {
      report << file.filename().string() << ": " << failedHere << " of " << tests.size() << " fail where " << expected
             << " are known to, first " << firstFailure << "\n";
      unexpected = true;
    }
  }
  INFO( report.str() << passed << " of " << total << " pass" );
  CHECK_FALSE( unexpected );
}
