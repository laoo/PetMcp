#include "Cpu6502.hpp"

#include "machine/StateArchive.hpp"

#define CHIPS_IMPL
#include <m6502.h>

namespace pet::cpu
{

namespace
{

/// The bits of the status register that are no flip-flops: m6502.h keeps the
/// break flag set and bit 5 clear, and pushes bit 5 set.
constexpr std::uint8_t PUSHED_ONLY = Cpu6502::FLAG_BREAK | Cpu6502::FLAG_UNUSED;

/// The store of SHA, SHX, SHY and TAS where its index carries into the high
/// byte. The NMOS 6502 stores the register ANDed with the base address's high
/// byte plus one, and puts what it stores on the high address lines as well,
/// as VICE's STORE_ABS_SH_* has it and SingleStepTests records. m6502.h
/// writes to the carried address, and ANDs with that address's high byte plus
/// one; this redirects its one write cycle. Without a carry the two agree.
void correctUnstableStore( m6502_t const& cpu, std::uint64_t& pins )
{
  std::uint8_t index = 0;
  std::uint8_t value = 0;
  switch ( cpu.IR >> 3U )
  {
  case 0x93: // SHA (zp),Y
  case 0x9f: // SHA abs,Y
    index = cpu.Y;
    value = cpu.A & cpu.X;
    break;
  case 0x9b: // TAS abs,Y: S is A AND X already
    index = cpu.Y;
    value = cpu.S;
    break;
  case 0x9e: // SHX abs,Y
    index = cpu.Y;
    value = cpu.X;
    break;
  case 0x9c: // SHY abs,X
    index = cpu.X;
    value = cpu.Y;
    break;
  default:
    return;
  }
  // AD holds the base address throughout the instruction.
  auto const base = static_cast<std::uint16_t>( cpu.AD );
  auto const target = static_cast<std::uint16_t>( base + index );
  value &= static_cast<std::uint8_t>( ( base >> 8U ) + 1U );
  M6502_SET_DATA( pins, static_cast<std::uint64_t>( value ) );
  if ( ( ( base ^ target ) & 0xff00U ) != 0 )
  {
    M6502_SET_ADDR( pins, static_cast<std::uint64_t>( ( static_cast<unsigned>( value ) << 8U ) | ( target & 0xffU ) ) );
  }
}

} // namespace

struct Cpu6502::State
{
  m6502_t cpu{};
  std::uint64_t pins{};
};

Cpu6502::Cpu6502() : mState{ std::make_unique<State>() }
{
  m6502_desc_t const desc{};
  mState->pins = m6502_init( &mState->cpu, &desc );
}

Cpu6502::~Cpu6502() = default;

void Cpu6502::reset()
{
  mState->pins |= M6502_RES;
}

void Cpu6502::setIrq( bool active )
{
  if ( active )
  {
    mState->pins |= M6502_IRQ;
  }
  else
  {
    mState->pins &= ~M6502_IRQ;
  }
}

void Cpu6502::setNmi( bool active )
{
  if ( active )
  {
    mState->pins |= M6502_NMI;
  }
  else
  {
    mState->pins &= ~M6502_NMI;
  }
}

void Cpu6502::tick( Bus& bus )
{
  // m6502_tick() takes the data of the last cycle's access, and answers the
  // address of this cycle's, which is made here.
  std::uint64_t pins = m6502_tick( &mState->cpu, mState->pins );
  if ( ( pins & M6502_RW ) == 0 )
  {
    correctUnstableStore( mState->cpu, pins );
  }
  std::uint16_t const address = M6502_GET_ADDR( pins );
  // The pins are the CPU's while the access is made, so that the bus can ask
  // what it is: an opcode fetch, a read or a write.
  mState->pins = pins;
  if ( ( pins & M6502_RW ) != 0 )
  {
    M6502_SET_DATA( pins, static_cast<std::uint64_t>( bus.read( address ) ) );
  }
  else
  {
    bus.write( address, M6502_GET_DATA( pins ) );
  }
  mState->pins = pins;
}

namespace
{

template <class Archive, class Core>
void serializeCore( Archive& archive, Core& cpu, std::uint64_t& pins )
{
  archive( cpu.IR );
  archive( cpu.PC );
  archive( cpu.AD );
  archive( cpu.A );
  archive( cpu.X );
  archive( cpu.Y );
  archive( cpu.S );
  archive( cpu.P );
  archive( cpu.PINS );
  archive( cpu.irq_pip );
  archive( cpu.nmi_pip );
  archive( cpu.brk_flags );
  archive( cpu.bcd_enabled );
  archive( pins );
}

} // namespace

void Cpu6502::serialize( machine::StateWriter& archive ) const
{
  m6502_t const& cpu = mState->cpu;
  std::uint64_t pins = mState->pins;
  serializeCore( archive, cpu, pins );
}

void Cpu6502::serialize( machine::StateReader& archive )
{
  serializeCore( archive, mState->cpu, mState->pins );
}

bool Cpu6502::sync() const
{
  return ( mState->pins & M6502_SYNC ) != 0;
}

std::uint16_t Cpu6502::programCounter() const
{
  return mState->cpu.PC;
}

bool Cpu6502::interrupting() const
{
  // What m6502_tick() tests at the start of the next tick, before it shifts
  // the pipelines again: an IRQ sampled two cycles before, an NMI edge in any
  // cycle before, or RES.
  m6502_t const& cpu = mState->cpu;
  return sync() &&
         ( ( cpu.irq_pip & 0x400U ) != 0 || ( cpu.nmi_pip & 0xfc00U ) != 0 || ( mState->pins & M6502_RES ) != 0 );
}

std::uint16_t Cpu6502::address() const
{
  return M6502_GET_ADDR( mState->pins );
}

bool Cpu6502::reading() const
{
  return ( mState->pins & M6502_RW ) != 0;
}

std::uint8_t Cpu6502::data() const
{
  return M6502_GET_DATA( mState->pins );
}

Registers Cpu6502::registers() const
{
  m6502_t const& cpu = mState->cpu;
  return Registers{
    .pc = cpu.PC, .a = cpu.A, .x = cpu.X, .y = cpu.Y, .s = cpu.S, .p = static_cast<std::uint8_t>( cpu.P | PUSHED_ONLY )
  };
}

void Cpu6502::setRegisters( Registers const& registers, Bus& bus )
{
  m6502_t& cpu = mState->cpu;
  cpu.A = registers.a;
  cpu.X = registers.x;
  cpu.Y = registers.y;
  cpu.S = registers.s;
  // Kept as m6502.h keeps it: PLP sets the break flag and clears bit 5.
  cpu.P = static_cast<std::uint8_t>( ( registers.p | FLAG_BREAK ) & ~FLAG_UNUSED );
  cpu.PC = registers.pc;
  // A boundary set from outside has nothing pending: no interrupt being
  // recognised, no reset.
  cpu.brk_flags = 0;
  cpu.irq_pip = 0;
  cpu.nmi_pip = 0;
  // The fetch m6502.h's documentation prescribes for a jump: the pins of an
  // opcode fetch from the new PC, with the opcode on the data bus. IRQ and NMI
  // keep their levels; a reset still pending is dropped.
  std::uint64_t pins = ( mState->pins & ( M6502_IRQ | M6502_NMI ) ) | M6502_SYNC | M6502_RW;
  M6502_SET_ADDR( pins, registers.pc );
  M6502_SET_DATA( pins, static_cast<std::uint64_t>( bus.read( registers.pc ) ) );
  mState->pins = pins;
}

} // namespace pet::cpu
