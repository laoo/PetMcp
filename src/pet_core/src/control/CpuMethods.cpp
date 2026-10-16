#include "Methods.hpp"

#include "cpu/Disassembler.hpp"

#include <spdlog/fmt/fmt.h>

namespace pet::control
{

namespace
{

/// The status register's flags as a debugger shows them, NV-BDIZC, a capital
/// for a set flag.
std::string flagsOf( std::uint8_t p )
{
  static constexpr std::string_view NAMES = "NV-BDIZC";
  std::string text;
  for ( std::size_t bit = 0; bit < NAMES.size(); ++bit )
  {
    bool const set = ( p & ( 0x80U >> bit ) ) != 0;
    char const name = NAMES[bit];
    if ( name == '-' || set )
    {
      text += name;
    }
    else
    {
      text += static_cast<char>( name - 'A' + 'a' );
    }
  }
  return text;
}

Json instructionJson( cpu::Instruction const& instruction )
{
  std::string bytes;
  for ( std::uint8_t const byte : instruction.bytes )
  {
    bytes += fmt::format( "{:02x}", byte );
  }
  return Json{ { "address", instruction.address },
               { "length", instruction.bytes.size() },
               { "bytes_hex", std::move( bytes ) },
               { "text", instruction.text } };
}

} // namespace

void addCpuMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add( "cpu.get_state",
                  info( "The 6502's registers at the instruction boundary the PET stands at, the instruction there, "
                        "and the cycle it begins in." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    machine::CpuState const cpu = ( *machine )->cpu();
                    machine::Machine const* const pet = *machine;
                    auto const peek = [pet]( std::uint16_t address ) { return pet->peek( address ); };
                    return Json{ { "pc", cpu.pc },
                                 { "a", cpu.a },
                                 { "x", cpu.x },
                                 { "y", cpu.y },
                                 { "s", cpu.s },
                                 { "p", cpu.p },
                                 { "flags", flagsOf( cpu.p ) },
                                 { "disasm", cpu::disassemble( cpu.pc, peek ).text },
                                 { "cycle", ( *machine )->now() - 1 } };
                  } );

  dispatcher.add(
      "cpu.disassemble",
      info( "Disassembles instructions as the 6502 would read them, without the side effects of reading; "
            "undocumented opcodes are marked with a star.",
            { { .name = "address", .type = "integer", .description = "Where to start." },
              { .name = "count", .type = "integer", .description = "How many instructions, at most 1000." } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        auto const address = requireUnsigned( params, "address" );
        auto const count = requireUnsigned( params, "count" );
        if ( !address || !count )
        {
          return std::unexpected( !address ? address.error() : count.error() );
        }
        if ( *address > 0xffff || *count == 0 || *count > 1000 )
        {
          return std::unexpected( badRequest( "address is 0 to $FFFF, count 1 to 1000" ) );
        }
        machine::Machine const* const pet = *machine;
        auto const peek = [pet]( std::uint16_t at ) { return pet->peek( at ); };
        Json instructions = Json::array();
        auto at = static_cast<std::uint16_t>( *address );
        for ( std::uint64_t i = 0; i < *count; ++i )
        {
          cpu::Instruction const instruction = cpu::disassemble( at, peek );
          at = static_cast<std::uint16_t>( at + instruction.bytes.size() );
          instructions.push_back( instructionJson( instruction ) );
        }
        return Json{ { "instructions", std::move( instructions ) } };
      } );
}

} // namespace pet::control
