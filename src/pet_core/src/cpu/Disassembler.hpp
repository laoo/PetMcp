#pragma once

// 6502 disassembly, on floooh/chips' m6502dasm.h, which stays inside
// Disassembler.cpp. Undocumented opcodes are marked with a `*`.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace pet::cpu
{

/// One instruction as it stands in memory.
struct Instruction
{
  std::uint16_t address{};
  std::vector<std::uint8_t> bytes;
  /// The mnemonic and its operand, such as `LDA $8000,X`.
  std::string text;
};

/// The instruction at `address`, its bytes read through `peek`, which must not
/// disturb what it reads.
[[nodiscard]] Instruction disassemble( std::uint16_t address,
                                       std::function<std::uint8_t( std::uint16_t )> const& peek );

} // namespace pet::cpu
