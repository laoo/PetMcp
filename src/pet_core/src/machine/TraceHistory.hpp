#pragma once

// The history of execution, as Machine keeps it: every instruction begun, 16
// bytes each, in a ring of 1 MB blocks. A block every instruction in which has
// been let go is kept for the next ones rather than given back, so that a
// history of steady length allocates nothing once it is full.

#include "pet/machine/Machine.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

namespace pet::machine
{

class TraceHistory
{
public:
  /// Notes an instruction, after the last.
  void push( TracedInstruction const& instruction );
  /// Lets go of the instructions begun before cycle `cutoff`, oldest first.
  void forgetBefore( std::uint64_t cutoff );

  [[nodiscard]] std::size_t size() const;
  /// The `index`th kept, 0 being the oldest.
  [[nodiscard]] TracedInstruction at( std::size_t index ) const;

private:
  /// An instruction packed: the cycle in the high 48 bits of a word, the PC in
  /// its low 16, then A, X, Y, S, P and the instruction's 3 bytes.
  struct Packed
  {
    std::uint64_t cyclePc;
    std::array<std::uint8_t, 8> rest;
  };

  static_assert( sizeof( Packed ) == 16 );

  static constexpr std::size_t BLOCK = 65536;
  using Block = std::array<Packed, BLOCK>;

  [[nodiscard]] Packed const& packedAt( std::size_t index ) const;

  std::deque<std::unique_ptr<Block>> mBlocks;
  /// A block let go, kept for the next.
  std::unique_ptr<Block> mSpare;
  /// The oldest kept's place in the first block, and how many are kept.
  std::size_t mFirst{};
  std::size_t mSize{};
};

} // namespace pet::machine
