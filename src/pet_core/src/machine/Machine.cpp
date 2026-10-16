#include "pet/machine/Machine.hpp"

#include "Clock.hpp"
#include "ExpansionBoard.hpp"
#include "Pia6520.hpp"
#include "Sound.hpp"
#include "StateArchive.hpp"
#include "TraceHistory.hpp"
#include "Via6522.hpp"
#include "Video.hpp"
#include "cpu/Cpu6502.hpp"

#include <miniz.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <optional>
#include <utility>
#include <vector>

namespace pet::machine
{

namespace
{

constexpr std::size_t RAM_SIZE = 0x8000;
constexpr std::size_t VIDEO_RAM_SIZE = 0x400;

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

/// What a state begins with, and the version of its layout as a whole.
constexpr std::array<char, 8> STATE_MAGIC{ 'P', 'E', 'T', 'S', 'T', 'A', 'T', 'E' };
constexpr std::uint32_t STATE_VERSION = 1;

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
      if ( row >= KEYBOARD_ROWS )
      {
        return 0xff;
      }
      return static_cast<std::uint8_t>( ~( board.keys.at( row ) | board.hostKeys.at( row ) ) );
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

    void setCb2( bool state, Clock /*at*/ ) override
    {
      board.sound.setCb2( state );
    }

    void storeAcr( std::uint8_t control ) override
    {
      acr = control;
      board.sound.setActive( soundActive() );
    }

    void storeT2l( std::uint8_t latch ) override
    {
      t2Latch = latch;
      board.sound.setActive( soundActive() );
    }

    /// petvia.c's SOUND_ACTIVE: no sound while the shift register shifts
    /// out under a T2 whose latch is 0, which the real PET makes inaudibly
    /// high and an emulator would make noise of.
    [[nodiscard]] bool soundActive() const
    {
      unsigned const control = acr & 0x1cU;
      bool const shiftedByT2 = control == 0x10 || control == 0x14;
      return !shiftedByT2 || t2Latch != 0;
    }

    std::uint8_t acr{};
    std::uint8_t t2Latch{};
  };

  Board( Roms romSet, bool withBoard )
      : roms{ std::move( romSet ) }, ram{ powerUpRam() }, videoRam{ powerUpVideoRam() },
        expansion{ withBoard ? std::make_unique<ExpansionBoard>() : nullptr }, pia1Ports{ *this }, viaPorts{ *this },
        pia1{ pia1Ports }, pia2{ pia2Ports }, via{ viaPorts }, video{ roms.characters, videoRam }
  {
    // The chips' reset at power-up, at the clock the trace begins at.
    pia1.reset( 0 );
    pia2.reset( 0 );
    via.reset( 0 );
  }

  std::uint8_t read( std::uint16_t address ) override
  {
    lastBus = load( address, false );
    if ( !watchpoints.empty() )
    {
      watch( address, false, lastBus );
    }
    return lastBus;
  }

  void write( std::uint16_t address, std::uint8_t value ) override
  {
    lastBus = value;
    if ( !watchpoints.empty() )
    {
      watch( address, true, value );
    }
    if ( address < RAM_SIZE )
    {
      ram[address] = value;
    }
    else if ( address < 0x8800 )
    {
      videoRam[address & 0x3ffU] = value;
    }
    else if ( expansion && ExpansionBoard::decodes( address ) )
    {
      expansion->write( address, value );
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

  /// Notes the first access a watchpoint watches: a data access, not a read
  /// of the instruction stream.
  void watch( std::uint16_t address, bool isWrite, std::uint8_t value )
  {
    if ( watchHit || cpu.sync() )
    {
      return;
    }
    std::uint16_t const pc = cpu.programCounter();
    if ( !isWrite && ( address == pc || address == static_cast<std::uint16_t>( pc - 1 ) ) )
    {
      return;
    }
    for ( Watchpoint const& watchpoint : watchpoints )
    {
      bool const inRange = std::uint32_t{ address } >= watchpoint.address &&
                           std::uint32_t{ address } - watchpoint.address < watchpoint.size;
      auto const wanted = static_cast<unsigned>( isWrite ? Access::WRITE : Access::READ );
      if ( inRange && ( static_cast<unsigned>( watchpoint.access ) & wanted ) != 0 )
      {
        watchHit = WatchHit{ .address = address, .write = isWrite, .value = value, .pc = currentPc };
        return;
      }
    }
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
    if ( expansion && ExpansionBoard::decodes( address ) )
    {
      return expansion->read( address );
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
    switch ( video.cycle( clock, textSet, expansion ? expansion->glyphs() : std::span<std::uint8_t const>{} ) )
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
    if ( expansion )
    {
      expansion->cycle();
    }
    sound.cycle( expansion.get() );
    cpu.setIrq( pia1.irq( clock ) || pia2.irq( clock ) || via.irq( clock ) );
    ++clock;
  }

  /// Lets go of the instructions older than the history keeps.
  void forgetOldTrace()
  {
    if ( clock > traceCycles )
    {
      trace.forgetBefore( clock - traceCycles );
    }
  }

  /// What the machine does at an instruction boundary: notes the instruction,
  /// and answers why the run stops there, if it does.
  std::optional<StopReason> atBoundary()
  {
    cpu::Registers const registers = cpu.registers();
    currentPc = registers.pc;
    trace.push( TracedInstruction{ .cycle = clock - 1,
                                   .registers = CpuState{ .pc = registers.pc,
                                                          .a = registers.a,
                                                          .x = registers.x,
                                                          .y = registers.y,
                                                          .s = registers.s,
                                                          .p = registers.p },
                                   .bytes = { cpu.data(),
                                              load( static_cast<std::uint16_t>( registers.pc + 1 ), true ),
                                              load( static_cast<std::uint16_t>( registers.pc + 2 ), true ) } } );
    forgetOldTrace();
    if ( instructionHook )
    {
      instructionHook( *owner );
    }
    if ( watchHit )
    {
      return StopReason::WATCHPOINT;
    }
    if ( breakpointCount != 0 && breakpointAt.test( currentPc ) )
    {
      return StopReason::BREAKPOINT;
    }
    return std::nullopt;
  }

  template <typename Done>
  RunResult run( Done const& done, std::uint64_t limit )
  {
    Clock const startClock = clock;
    std::uint64_t const startFrame = frame;
    watchHit.reset();
    sound.frames().clear();
    RunResult result{ .reason = StopReason::TIMEOUT };
    for ( ;; )
    {
      step();
      if ( !cpu.sync() || cpu.interrupting() )
      {
        continue;
      }
      if ( auto const stop = atBoundary() )
      {
        result.reason = *stop;
        if ( *stop == StopReason::WATCHPOINT )
        {
          result.watchpoint = watchHit.value_or( WatchHit{} );
          watchHit.reset();
        }
        else
        {
          result.breakpoint = currentPc;
        }
        break;
      }
      if ( done() )
      {
        result.reason = StopReason::CONDITION;
        break;
      }
      if ( clock - startClock >= limit )
      {
        break;
      }
    }
    result.cycles = clock - startClock;
    result.frames = frame - startFrame;
    lastRun = result;
    if ( capturing )
    {
      auto const& made = sound.frames();
      captured.insert( captured.end(), made.begin(), made.end() );
    }
    return result;
  }

  /// RAM, the keys the protocol holds, and the machine's own counters.
  struct Memory
  {
    Board& board;

    template <class Archive>
    void serialize( Archive& archive )
    {
      archive( board.ram );
      archive( board.videoRam );
      archive( board.keys );
      archive( board.clock );
      archive( board.frame );
      archive( board.lastBus );
      archive( board.textSet );
    }
  };

  struct Cpu
  {
    cpu::Cpu6502& cpu;

    template <class Archive>
    void serialize( Archive& archive )
    {
      cpu.serialize( archive );
    }
  };

  /// The whole state, in either direction.
  template <class Archive>
  void serialize( Archive& archive )
  {
    Memory memory{ *this };
    Cpu processor{ cpu };
    archive.chunk( fourCc( "MEM " ), 1, memory );
    archive.chunk( fourCc( "CPU " ), 1, processor );
    archive.chunk( fourCc( "PIA1" ), 1, pia1 );
    archive.chunk( fourCc( "PIA2" ), 1, pia2 );
    archive.chunk( fourCc( "VIA " ), 1, via );
    archive.chunk( fourCc( "VID " ), 1, video );
    archive.chunk( fourCc( "SND " ), 1, sound );
    if ( expansion )
    {
      archive.chunk( fourCc( "XBRD" ), 1, *expansion );
    }
  }

  [[nodiscard]] std::uint32_t romsCrc() const
  {
    mz_ulong crc = MZ_CRC32_INIT;
    for ( auto const* rom : { &roms.basic, &roms.editor, &roms.kernal, &roms.characters } )
    {
      crc = mz_crc32( crc, rom->data(), rom->size() );
    }
    return static_cast<std::uint32_t>( crc );
  }

  Machine const* owner{};
  Roms roms;
  std::vector<std::uint8_t> ram;
  std::vector<std::uint8_t> videoRam;
  /// The expansion board, or null for empty sockets.
  std::unique_ptr<ExpansionBoard> expansion;
  Sound sound;
  bool capturing{};
  std::vector<AudioFrame> captured;
  /// The keys the protocol and the host hold.
  KeyMatrix keys{};
  KeyMatrix hostKeys{};
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

  /// The instruction the 6502 is in, which a watched access is reported of.
  std::uint16_t currentPc{};
  std::bitset<0x10000> breakpointAt;
  std::size_t breakpointCount{};
  std::vector<Watchpoint> watchpoints;
  std::optional<WatchHit> watchHit;
  TraceHistory trace;
  std::uint64_t traceCycles{ DEFAULT_TRACE_CYCLES };
  RunResult lastRun{};
};

/// A run that went the whole way it was asked to, by its count or its limit,
/// is completed; one a breakpoint or a watchpoint stopped says so.
void Machine::completed( RunResult& result )
{
  if ( result.reason == StopReason::CONDITION || result.reason == StopReason::TIMEOUT )
  {
    result.reason = StopReason::COMPLETED;
    mBoard->lastRun = result;
  }
}

Machine::Machine( Roms roms, bool expansionBoard )
    : mBoard{ std::make_unique<Board>( std::move( roms ), expansionBoard ) }
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
  completed( result );
  return result;
}

RunResult Machine::runFrames( std::uint64_t count )
{
  std::uint64_t const target = mBoard->frame + count;
  RunResult result = mBoard->run( [&] { return mBoard->frame >= target; }, ~std::uint64_t{} );
  completed( result );
  return result;
}

RunResult Machine::runUntil( std::function<bool( Machine const& )> const& condition, std::uint64_t timeoutCycles )
{
  return mBoard->run( [&] { return condition( *this ); }, timeoutCycles );
}

RunResult Machine::step( std::uint64_t count )
{
  std::uint64_t instructions = 0;
  RunResult result = mBoard->run( [&] { return ++instructions >= count; }, ~std::uint64_t{} );
  completed( result );
  return result;
}

void Machine::addBreakpoint( std::uint16_t address )
{
  if ( !mBoard->breakpointAt.test( address ) )
  {
    mBoard->breakpointAt.set( address );
    ++mBoard->breakpointCount;
  }
}

bool Machine::removeBreakpoint( std::uint16_t address )
{
  if ( !mBoard->breakpointAt.test( address ) )
  {
    return false;
  }
  mBoard->breakpointAt.reset( address );
  --mBoard->breakpointCount;
  return true;
}

std::vector<std::uint16_t> Machine::breakpoints() const
{
  std::vector<std::uint16_t> addresses;
  for ( std::size_t address = 0; address < mBoard->breakpointAt.size() && addresses.size() < mBoard->breakpointCount;
        ++address )
  {
    if ( mBoard->breakpointAt.test( address ) )
    {
      addresses.push_back( static_cast<std::uint16_t>( address ) );
    }
  }
  return addresses;
}

void Machine::addWatchpoint( Watchpoint watchpoint )
{
  removeWatchpoint( watchpoint.address );
  mBoard->watchpoints.push_back( watchpoint );
  std::ranges::sort( mBoard->watchpoints, {}, &Watchpoint::address );
}

bool Machine::removeWatchpoint( std::uint16_t address )
{
  return std::erase_if( mBoard->watchpoints, [address]( Watchpoint const& w ) { return w.address == address; } ) != 0;
}

std::vector<Watchpoint> Machine::watchpoints() const
{
  return mBoard->watchpoints;
}

void Machine::setTraceCycles( std::uint64_t cycles )
{
  mBoard->traceCycles = cycles;
  mBoard->forgetOldTrace();
}

std::uint64_t Machine::traceCycles() const
{
  return mBoard->traceCycles;
}

std::size_t Machine::traceLength() const
{
  return mBoard->trace.size();
}

TracedInstruction Machine::tracedAt( std::size_t index ) const
{
  return mBoard->trace.at( index );
}

std::vector<TracedInstruction> Machine::trace( std::size_t start, std::size_t count ) const
{
  TraceHistory const& kept = mBoard->trace;
  start = std::min( start, kept.size() );
  count = std::min( count, kept.size() - start );
  std::vector<TracedInstruction> instructions;
  instructions.reserve( count );
  for ( std::size_t i = 0; i < count; ++i )
  {
    instructions.push_back( kept.at( start + i ) );
  }
  return instructions;
}

namespace
{

PiaState stateOf( Pia6520::Snapshot const& pia )
{
  return PiaState{ .portA = pia.portA,
                   .ddrA = pia.ddrA,
                   .controlA = pia.controlA,
                   .portB = pia.portB,
                   .ddrB = pia.ddrB,
                   .controlB = pia.controlB,
                   .ca2 = pia.ca2,
                   .cb2 = pia.cb2 };
}

} // namespace

IoState Machine::io() const
{
  Board& board = *mBoard;
  Via6522::Snapshot const via = board.via.snapshot( board.clock );
  Clock const last = board.clock - 1;
  return IoState{ .pia1 = stateOf( board.pia1.snapshot() ),
                  .pia2 = stateOf( board.pia2.snapshot() ),
                  .via = ViaState{ .registers = via.registers,
                                   .t1 = via.t1,
                                   .t1Latch = via.t1Latch,
                                   .t2 = via.t2,
                                   .ifr = via.ifr,
                                   .ier = via.ier },
                  .irq = board.pia1.irq( last ) || board.pia2.irq( last ) || board.via.irq( last ) };
}

std::span<AudioFrame const> Machine::audio() const
{
  return mBoard->sound.frames();
}

void Machine::startCapture()
{
  mBoard->captured.clear();
  mBoard->capturing = true;
}

bool Machine::capturing() const
{
  return mBoard->capturing;
}

std::vector<AudioFrame> Machine::stopCapture()
{
  mBoard->capturing = false;
  return std::exchange( mBoard->captured, {} );
}

std::optional<BoardState> Machine::board() const
{
  ExpansionBoard const* const expansion = mBoard->expansion.get();
  if ( expansion == nullptr )
  {
    return std::nullopt;
  }
  std::uint8_t const control = expansion->chip( 0 ).portB();
  return BoardState{ .registers = { expansion->chip( 0 ).registers(), expansion->chip( 1 ).registers() },
                     .latches = { expansion->chip( 0 ).selected(), expansion->chip( 1 ).selected() },
                     .banks = { expansion->bank( 0 ), expansion->bank( 1 ) },
                     .romCharacters = ( control & 0x02U ) != 0,
                     .shownBuffer = control & 0x01U,
                     .fullClock = { ( control & 0x04U ) != 0, ( control & 0x08U ) != 0 } };
}

std::span<std::uint8_t const> Machine::boardMemory( unsigned which ) const
{
  ExpansionBoard const* const expansion = mBoard->expansion.get();
  if ( expansion == nullptr )
  {
    return {};
  }
  return which == 2 ? expansion->characterRam() : expansion->ram( which );
}

RunResult const& Machine::lastRun() const
{
  return mBoard->lastRun;
}

std::vector<std::uint8_t> Machine::saveState() const
{
  StateWriter writer;
  writer( STATE_MAGIC );
  writer( STATE_VERSION );
  writer( mBoard->romsCrc() );
  mBoard->serialize( writer );
  return writer.bytes();
}

std::expected<void, std::string> Machine::loadState( std::span<std::uint8_t const> state )
{
  StateReader header{ state };
  std::array<char, 8> magic{};
  std::uint32_t version = 0;
  std::uint32_t crc = 0;
  header( magic );
  header( version );
  header( crc );
  if ( !header.good() || magic != STATE_MAGIC )
  {
    return std::unexpected( std::string{ "not a PET state" } );
  }
  if ( version != STATE_VERSION )
  {
    return std::unexpected( std::string{ "a state of another build's layout" } );
  }
  if ( crc != mBoard->romsCrc() )
  {
    return std::unexpected( std::string{ "a state of a PET with other ROMs" } );
  }
  // Read into the machine, and back out of what it was if the state proves
  // short or of another layout.
  std::vector<std::uint8_t> const before = saveState();
  StateReader reader{ state };
  reader( magic );
  reader( version );
  reader( crc );
  mBoard->serialize( reader );
  if ( reader.good() && reader.atEnd() )
  {
    return {};
  }
  StateReader restore{ before };
  restore( magic );
  restore( version );
  restore( crc );
  mBoard->serialize( restore );
  return std::unexpected( std::string{ "a state of another build's layout, or cut short" } );
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

void Machine::setKey( PetKey key, bool down )
{
  std::uint8_t& row = mBoard->keys.at( key.row );
  auto const bit = static_cast<std::uint8_t>( 1U << key.column );
  row = down ? static_cast<std::uint8_t>( row | bit ) : static_cast<std::uint8_t>( row & ~bit );
}

void Machine::releaseKeys()
{
  mBoard->keys.fill( 0 );
}

KeyMatrix Machine::heldKeys() const
{
  return mBoard->keys;
}

void Machine::setHostKeys( KeyMatrix const& keys )
{
  mBoard->hostKeys = keys;
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
