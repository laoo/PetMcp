#include "TraceHistory.hpp"

namespace pet::machine
{

void TraceHistory::push( TracedInstruction const& instruction )
{
  std::size_t const end = mFirst + mSize;
  if ( end == mBlocks.size() * BLOCK )
  {
    mBlocks.push_back( mSpare ? std::move( mSpare ) : std::make_unique<Block>() );
  }
  CpuState const& r = instruction.registers;
  ( *mBlocks.at( end / BLOCK ) ).at( end % BLOCK ) =
      Packed{ .cyclePc = ( instruction.cycle << 16U ) | r.pc,
              .rest = { r.a, r.x, r.y, r.s, r.p, instruction.bytes[0], instruction.bytes[1], instruction.bytes[2] } };
  ++mSize;
}

void TraceHistory::forgetBefore( std::uint64_t cutoff )
{
  while ( mSize > 0 && ( packedAt( 0 ).cyclePc >> 16U ) < cutoff )
  {
    ++mFirst;
    --mSize;
    if ( mFirst == BLOCK )
    {
      mSpare = std::move( mBlocks.front() );
      mBlocks.pop_front();
      mFirst = 0;
    }
  }
}

std::size_t TraceHistory::size() const
{
  return mSize;
}

TraceHistory::Packed const& TraceHistory::packedAt( std::size_t index ) const
{
  std::size_t const place = mFirst + index;
  return ( *mBlocks.at( place / BLOCK ) ).at( place % BLOCK );
}

TracedInstruction TraceHistory::at( std::size_t index ) const
{
  Packed const& packed = packedAt( index );
  auto const& rest = packed.rest;
  return TracedInstruction{ .cycle = packed.cyclePc >> 16U,
                            .registers = CpuState{ .pc = static_cast<std::uint16_t>( packed.cyclePc & 0xffffU ),
                                                   .a = rest[0],
                                                   .x = rest[1],
                                                   .y = rest[2],
                                                   .s = rest[3],
                                                   .p = rest[4] },
                            .bytes = { rest[5], rest[6], rest[7] } };
}

} // namespace pet::machine
