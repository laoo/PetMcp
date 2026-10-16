#include "Disassembler.hpp"

#define CHIPS_UTIL_IMPL
#include <m6502dasm.h>

namespace pet::cpu
{

namespace
{

struct Context
{
  std::function<std::uint8_t( std::uint16_t )> const& peek;
  Instruction& instruction;
};

std::uint8_t input( void* userData )
{
  auto& context = *static_cast<Context*>( userData );
  auto const at = static_cast<std::uint16_t>( context.instruction.address + context.instruction.bytes.size() );
  std::uint8_t const byte = context.peek( at );
  context.instruction.bytes.push_back( byte );
  return byte;
}

void output( char c, void* userData )
{
  static_cast<Context*>( userData )->instruction.text.push_back( c );
}

} // namespace

Instruction disassemble( std::uint16_t address, std::function<std::uint8_t( std::uint16_t )> const& peek )
{
  Instruction instruction{ .address = address, .bytes = {}, .text = {} };
  Context context{ .peek = peek, .instruction = instruction };
  m6502dasm_op( address, input, output, &context );
  return instruction;
}

} // namespace pet::cpu
