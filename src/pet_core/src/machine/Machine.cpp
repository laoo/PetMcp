#include "pet/machine/Machine.hpp"

#include "Clock.hpp"
#include "Pia6520.hpp"
#include "Via6522.hpp"
#include "Video.hpp"
#include "cpu/Cpu6502.hpp"

#include <array>
#include <cassert>
#include <utility>
#include <vector>

namespace pet::machine
{

namespace
{

constexpr std::size_t RAM_SIZE = 0x8000;
constexpr std::size_t VIDEO_RAM_SIZE = 0x400;
constexpr unsigned KEYBOARD_ROWS = 10;

/// RAM as VICE powers it up with its default settings (ram.c, ram_init with
/// RAMInitStartValue 0, RAMInitValueOffset 2, RAMInitValueInvert 4,
/// RAMInitPatternInvert 16384 of 255), without the random bits VICE adds.
std::vector<std::uint8_t> powerUpRam()
{
  std::vector<std::uint8_t> ram( RAM_SIZE );
  for ( std::size_t offset = 0; offset < ram.size(); ++offset )
  {
    std::uint8_t const value = ( ( ( offset + 2 ) / 4 ) & 1U ) != 0 ? 0xff : 0x00;
    std::uint8_t const pattern = ( ( offset / 16384 ) & 1U ) != 0 ? 0xff : 0x00;
    ram[offset] = value ^ pattern;
  }
  return ram;
}

/// Video RAM at power-up. VICE fills it at random; the emulator must be
/// deterministic, so it is a fixed pseudo-random sequence
/// (docs/hardware/machine.md).
std::vector<std::uint8_t> powerUpVideoRam()
{
  std::vector<std::uint8_t> ram( VIDEO_RAM_SIZE );
  std::uint32_t state = 0x50455433; // "PET3"
  for ( std::uint8_t& byte : ram )
  {
    state ^= state << 13U;
    state ^= state >> 17U;
    state ^= state << 5U;
    byte = static_cast<std::uint8_t>( state >> 24U );
  }
  return ram;
}

/// The cycle of the first opcode fetch after power-up, as VICE counts it: its
/// trace of a power-up shows the first instruction at clock 6.
[[maybe_unused]] constexpr Clock FIRST_FETCH = 6;

} // namespace

struct Machine::Board final : cpu::Bus
{
  /// PIA1, $E810: the keyboard, the retrace and IEEE-488's EOI (petpia1.c).
  struct Pia1Ports final : PiaPorts
  {
    Board& board;

    explicit Pia1Ports( Board& owner ) : board{ owner } {}

    std::uint8_t readPa() override
    {
      // Bits 4 and 5 sense the tapes, none here; 6 is EOI in, 7 the
      // diagnostic pin; each reads 0 when active.
      return 0xff;
    }

    std::uint8_t readPb() override
    {
      // The row is the output register's low nibble, through a 4-to-10
      // decoder; rows past 9 select nothing.
      unsigned const row = board.pia1.snapshot().portA & 0x0fU;
      return row < KEYBOARD_ROWS ? static_cast<std::uint8_t>( ~board.keys.at( row ) ) : std::uint8_t{ 0xff };
    }

    void storePa( std::uint8_t /*byte*/ ) override {}

    void storePb( std::uint8_t /*byte*/ ) override {}

    void setCa2( bool /*state*/ ) override {}

    void setCb2( bool /*state*/ ) override {}
  };

  /// PIA2, $E820: IEEE-488's data and NDAC and DAV out (petpia2.c).
  struct Pia2Ports final : PiaPorts
  {
    std::uint8_t readPa() override
    {
      return 0xff;
    }

    std::uint8_t readPb() override
    {
      return 0xff;
    }

    void storePa( std::uint8_t /*byte*/ ) override {}

    void storePb( std::uint8_t /*byte*/ ) override {}

    void setCa2( bool /*state*/ ) override {}

    void setCb2( bool /*state*/ ) override {}
  };

  /// The VIA, $E840: IEEE-488's handshake, the retrace on PB5, the character
  /// set on CA2 and the sound on CB2 (petvia.c).
  struct ViaPortsOfPet final : ViaPorts
  {
    Board& board;

    explicit ViaPortsOfPet( Board& owner ) : board{ owner } {}

    std::uint8_t readPra( bool /*peek*/ ) override
    {
      // The user port, with nothing on it.
      return 0xff;
    }

    std::uint8_t readPrb( bool /*peek*/ ) override
    {
      // NRFD in (6), NDAC in (0) and DAV in (7) read 0 when active; PB5 is low
      // in the vertical blank.
      std::uint8_t byte = 0xff;
      if ( board.video.offScreen() )
      {
        byte = static_cast<std::uint8_t>( byte - 32 );
      }
      return byte;
    }

    void storePra( std::uint8_t /*byte*/ ) override {}

    void storePrb( std::uint8_t /*byte*/ ) override {}

    void setCa2( bool state ) override
    {
      board.textSet = state;
    }

    void setCb2( bool /*state*/, Clock /*at*/ ) override {}
  };

  explicit Board( Roms romSet )
      : roms{ std::move( romSet ) }, ram{ powerUpRam() }, videoRam{ powerUpVideoRam() }, pia1Ports{ *this },
        viaPorts{ *this }, pia1{ pia1Ports }, pia2{ pia2Ports }, via{ viaPorts }, video{ roms.characters, videoRam }
  {
    // The chips' reset at power-up, at the clock the trace begins at.
    pia1.reset( 0 );
    pia2.reset( 0 );
    via.reset( 0 );
  }

  std::uint8_t read( std::uint16_t address ) override
  {
    lastBus = load( address, false );
    return lastBus;
  }

  void write( std::uint16_t address, std::uint8_t value ) override
  {
    lastBus = value;
    if ( address < RAM_SIZE )
    {
      ram[address] = value;
    }
    else if ( address < 0x8800 )
    {
      videoRam[address & 0x3ffU] = value;
    }
    else if ( ( address & 0xff00U ) == 0xe800 )
    {
      // A write reaches every chip its address selects (petmem.c, store_io_e8).
      if ( ( address & 0x10U ) != 0 )
      {
        pia1.store( address, value, clock );
      }
      if ( ( address & 0x20U ) != 0 )
      {
        pia2.store( address, value, clock );
      }
      if ( ( address & 0x40U ) != 0 )
      {
        via.store( address, value, clock );
      }
    }
    // The ROMs and the empty sockets ignore a write.
  }

  /// A read of `address`; a peek has no side effects.
  std::uint8_t load( std::uint16_t address, bool peek )
  {
    if ( address < RAM_SIZE )
    {
      return ram[address];
    }
    if ( address < 0x8800 )
    {
      // 1 KB, mirrored at $8400 (petmem.c, read_vmirror).
      return videoRam[address & 0x3ffU];
    }
    if ( address >= 0xf000 )
    {
      return roms.kernal[address & 0x0fffU];
    }
    if ( address >= 0xc000 && address < 0xe000 )
    {
      return roms.basic[address & 0x1fffU];
    }
    if ( address >= 0xe000 && address < 0xe800 )
    {
      return roms.editor[address & 0x07ffU];
    }
    if ( ( address & 0xff00U ) == 0xe800 )
    {
      return loadIo( address, peek );
    }
    // $8800-$BFFF and $E900-$EFFF: nothing answers, and the bus keeps what it
    // last carried (petmem.c, read_unused).
    return lastBus;
  }

  /// The I/O page as petmem.c's read_io_e8 reads it: one chip selected answers
  /// alone, several answer the AND of their outputs, none leaves the bus as
  /// it was.
  std::uint8_t loadIo( std::uint16_t address, bool peek )
  {
    auto const readPia1 = [&] { return peek ? pia1.peek( address ) : pia1.read( address, clock ); };
    auto const readPia2 = [&] { return peek ? pia2.peek( address ) : pia2.read( address, clock ); };
    auto const readVia = [&] { return peek ? via.peek( address, clock ) : via.read( address, clock ); };
    switch ( address & 0xf0U )
    {
    case 0x10:
      return readPia1();
    case 0x20:
      return readPia2();
    case 0x40:
      return readVia();
    case 0x00:
    case 0x80:
      // No chip at all, and the CRTC this model does not have.
      return lastBus;
    default:
    {
      std::uint8_t const fromPia1 = ( address & 0x10U ) != 0 ? readPia1() : std::uint8_t{ 0xff };
      std::uint8_t const fromPia2 = ( address & 0x20U ) != 0 ? readPia2() : std::uint8_t{ 0xff };
      std::uint8_t const fromVia = ( address & 0x40U ) != 0 ? readVia() : std::uint8_t{ 0xff };
      return fromPia1 & fromPia2 & fromVia;
    }
    }
  }

  /// One cycle: the 6502's access, then what the devices do after it, then
  /// the interrupt line the 6502 samples in the next.
  void step()
  {
    cpu.tick( *this );
    switch ( video.cycle( clock, textSet ) )
    {
    case Video::Event::BLANK_BEGINS:
      pia1.signal( Pia6520::Line::CB1, false, clock );
      break;
    case Video::Event::BLANK_ENDS:
      pia1.signal( Pia6520::Line::CB1, true, clock );
      break;
    case Video::Event::NONE:
      break;
    }
    if ( inFrame( clock ) == VBLANK_START )
    {
      ++frame;
    }
    via.cycle( clock );
    cpu.setIrq( pia1.irq( clock ) || pia2.irq( clock ) || via.irq( clock ) );
    ++clock;
  }

  template <typename Done>
  RunResult run( Done const& done, std::uint64_t limit )
  {
    Clock const startClock = clock;
    std::uint64_t const startFrame = frame;
    StopReason reason = StopReason::TIMEOUT;
    for ( ;; )
    {
      step();
      if ( !cpu.sync() || cpu.interrupting() )
      {
        continue;
      }
      if ( instructionHook )
      {
        instructionHook( *owner );
      }
      if ( done() )
      {
        reason = StopReason::CONDITION;
        break;
      }
      if ( clock - startClock >= limit )
      {
        break;
      }
    }
    return RunResult{ .reason = reason, .cycles = clock - startClock, .frames = frame - startFrame };
  }

  Machine const* owner{};
  Roms roms;
  std::vector<std::uint8_t> ram;
  std::vector<std::uint8_t> videoRam;
  /// The keys held, a bit a key, by row of the matrix, as VICE's keyarr.
  std::array<std::uint8_t, KEYBOARD_ROWS> keys{};
  Pia1Ports pia1Ports;
  Pia2Ports pia2Ports;
  ViaPortsOfPet viaPorts;
  Pia6520 pia1;
  Pia6520 pia2;
  Via6522 via;
  Video video;
  cpu::Cpu6502 cpu;
  /// The cycle the next step makes; the first fetch is at FIRST_FETCH.
  Clock clock{};
  std::uint64_t frame{};
  std::uint8_t lastBus{};
  bool textSet{ true };
  std::function<void( Machine const& )> instructionHook;
};

Machine::Machine( Roms roms ) : mBoard{ std::make_unique<Board>( std::move( roms ) ) }
{
  mBoard->owner = this;
  // The reset sequence runs from cycle 0, and its last cycle fetches the
  // opcode the vector points to, in cycle FIRST_FETCH as in VICE.
  Board& board = *mBoard;
  do
  {
    board.step();
  } while ( !board.cpu.sync() || board.cpu.interrupting() );
  assert( board.clock == FIRST_FETCH + 1 );
}

Machine::~Machine() = default;

void Machine::reset()
{
  mBoard->cpu.reset();
  mBoard->pia1.reset( mBoard->clock );
  mBoard->pia2.reset( mBoard->clock );
  mBoard->via.reset( mBoard->clock );
}

RunResult Machine::runCycles( std::uint64_t count )
{
  RunResult result = mBoard->run( [] { return false; }, count );
  result.reason = StopReason::COMPLETED;
  return result;
}

RunResult Machine::runFrames( std::uint64_t count )
{
  std::uint64_t const target = mBoard->frame + count;
  RunResult result = mBoard->run( [&] { return mBoard->frame >= target; }, ~std::uint64_t{} );
  result.reason = StopReason::COMPLETED;
  return result;
}

RunResult Machine::runUntil( std::function<bool( Machine const& )> const& condition, std::uint64_t timeoutCycles )
{
  return mBoard->run( [&] { return condition( *this ); }, timeoutCycles );
}

void Machine::setInstructionHook( std::function<void( Machine const& )> hook )
{
  mBoard->instructionHook = std::move( hook );
}

std::uint64_t Machine::now() const
{
  return mBoard->clock;
}

std::uint64_t Machine::frame() const
{
  return mBoard->frame;
}

CpuState Machine::cpu() const
{
  cpu::Registers const registers = mBoard->cpu.registers();
  return CpuState{
    .pc = registers.pc, .a = registers.a, .x = registers.x, .y = registers.y, .s = registers.s, .p = registers.p
  };
}

std::uint8_t Machine::peek( std::uint16_t address ) const
{
  return mBoard->load( address, true );
}

bool Machine::poke( std::uint16_t address, std::uint8_t value )
{
  if ( address < RAM_SIZE )
  {
    mBoard->ram[address] = value;
    return true;
  }
  if ( address < 0x8800 )
  {
    mBoard->videoRam[address & 0x3ffU] = value;
    return true;
  }
  return false;
}

std::span<std::uint8_t const> Machine::ram() const
{
  return mBoard->ram;
}

std::span<std::uint8_t const> Machine::videoRam() const
{
  return mBoard->videoRam;
}

Roms const& Machine::roms() const
{
  return mBoard->roms;
}

std::span<std::uint8_t const> Machine::picture() const
{
  return mBoard->video.picture();
}

std::int64_t Machine::picturesDrawn() const
{
  return mBoard->video.picturesDrawn();
}

bool Machine::textSet() const
{
  return mBoard->textSet;
}

} // namespace pet::machine
