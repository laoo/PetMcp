#include <catch2/catch_test_macros.hpp>

#include "cpu/Cpu6502.hpp"
#include "cpu/Disassembler.hpp"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <vector>

using pet::cpu::Bus;
using pet::cpu::Cpu6502;

namespace
{

class Memory final : public Bus
{
public:
  std::array<std::uint8_t, 0x10000> bytes{};
  std::vector<std::uint16_t> reads;

  std::uint8_t read( std::uint16_t address ) override
  {
    reads.push_back( address );
    return bytes.at( address );
  }

  void write( std::uint16_t address, std::uint8_t value ) override
  {
    bytes.at( address ) = value;
  }

  void place( std::uint16_t address, std::initializer_list<std::uint8_t> code )
  {
    for ( std::uint8_t const byte : code )
    {
      bytes.at( address++ ) = byte;
    }
  }
};

/// Ticks until an opcode is fetched from `pc`; the ticks it took, or 0 if it
/// took more than `limit`.
int runTo( Cpu6502& cpu, Memory& memory, std::uint16_t pc, int limit = 100 )
{
  for ( int ticks = 1; ticks <= limit; ++ticks )
  {
    cpu.tick( memory );
    if ( cpu.sync() && cpu.address() == pc )
    {
      return ticks;
    }
  }
  return 0;
}

} // namespace

TEST_CASE( "after power-up the 6502 fetches its first opcode where the reset vector points", "[cpu]" )
{
  Memory memory;
  memory.place( 0xfffc, { 0x00, 0xc0 } );
  Cpu6502 cpu;

  REQUIRE( runTo( cpu, memory, 0xc000 ) > 0 );
  REQUIRE( memory.reads.size() >= 3 );
  REQUIRE( memory.reads[memory.reads.size() - 3] == 0xfffc );
  REQUIRE( memory.reads[memory.reads.size() - 2] == 0xfffd );
  REQUIRE( ( cpu.registers().p & Cpu6502::FLAG_IRQ_DISABLE ) != 0 );
}

TEST_CASE( "an IRQ is taken once CLI has cleared the mask, and pushes the status without the break flag", "[cpu]" )
{
  Memory memory;
  memory.place( 0xfffc, { 0x00, 0xc0 } );
  memory.place( 0xfffe, { 0x00, 0xd0 } );
  // CLI, then a loop on itself.
  memory.place( 0xc000, { 0x58, 0x4c, 0x01, 0xc0 } );
  Cpu6502 cpu;
  REQUIRE( runTo( cpu, memory, 0xc000 ) > 0 );
  REQUIRE( runTo( cpu, memory, 0xc001 ) > 0 );

  cpu.setIrq( true );
  REQUIRE( runTo( cpu, memory, 0xd000 ) > 0 );

  auto const registers = cpu.registers();
  REQUIRE( ( registers.p & Cpu6502::FLAG_IRQ_DISABLE ) != 0 );
  std::uint8_t const pushed = memory.bytes.at( 0x0100U + static_cast<std::uint8_t>( registers.s + 1 ) );
  REQUIRE( ( pushed & Cpu6502::FLAG_BREAK ) == 0 );
  REQUIRE( ( pushed & Cpu6502::FLAG_IRQ_DISABLE ) == 0 );
}

TEST_CASE( "a masked IRQ is not taken", "[cpu]" )
{
  Memory memory;
  memory.place( 0xfffc, { 0x00, 0xc0 } );
  memory.place( 0xfffe, { 0x00, 0xd0 } );
  memory.place( 0xc000, { 0x4c, 0x00, 0xc0 } );
  Cpu6502 cpu;
  REQUIRE( runTo( cpu, memory, 0xc000 ) > 0 );

  cpu.setIrq( true );
  REQUIRE( runTo( cpu, memory, 0xd000, 200 ) == 0 );
}

TEST_CASE( "the disassembler reads an instruction's bytes and names it", "[cpu]" )
{
  Memory memory;
  memory.place( 0x0400, { 0xbd, 0x00, 0x80, 0xa7, 0x10 } );
  auto const peek = [&memory]( std::uint16_t address ) { return memory.bytes.at( address ); };

  auto const load = pet::cpu::disassemble( 0x0400, peek );
  REQUIRE( load.address == 0x0400 );
  REQUIRE( load.bytes == std::vector<std::uint8_t>{ 0xbd, 0x00, 0x80 } );
  REQUIRE( load.text == "LDA $8000,X" );

  auto const undocumented = pet::cpu::disassemble( 0x0403, peek );
  REQUIRE( undocumented.bytes.size() == 2 );
  REQUIRE( undocumented.text.contains( '*' ) );
}
