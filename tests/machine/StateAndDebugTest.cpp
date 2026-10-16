#include <catch2/catch_test_macros.hpp>

#include "pet/machine/Machine.hpp"
#include "pet/machine/Programs.hpp"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Save states and the debugging aids, on the PET with VICE's ROMs; skip
// without them.

using pet::machine::Machine;
using pet::machine::StopReason;

namespace
{

std::unique_ptr<Machine> booted()
{
  auto roms = pet::machine::Roms::load( PET_TEST_ROM_DIR );
  if ( !roms )
  {
    return nullptr;
  }
  auto machine = std::make_unique<Machine>( std::move( *roms ) );
  machine->runFrames( 100 );
  return machine;
}

/// A sum of everything a frame leaves: RAM, video RAM, the picture, the
/// registers and the time.
std::uint64_t sumOf( Machine const& machine )
{
  std::uint64_t sum = 14695981039346656037ULL;
  auto const mix = [&sum]( std::uint64_t value )
  {
    sum ^= value;
    sum *= 1099511628211ULL;
  };
  for ( auto const bytes : { machine.ram(), machine.videoRam(), machine.picture() } )
  {
    for ( std::uint8_t const byte : bytes )
    {
      mix( byte );
    }
  }
  auto const cpu = machine.cpu();
  for ( unsigned const value : { unsigned{ cpu.pc },
                                 unsigned{ cpu.a },
                                 unsigned{ cpu.x },
                                 unsigned{ cpu.y },
                                 unsigned{ cpu.s },
                                 unsigned{ cpu.p } } )
  {
    mix( value );
  }
  mix( machine.now() );
  mix( machine.frame() );
  return sum;
}

/// A BASIC program that keeps the screen, the VIA's timer and RAM busy:
/// 10 POKE 32768+RND(1)*999,RND(1)*255:A=PEEK(59464):GOTO 10
std::vector<std::uint8_t> busyProgram()
{
  std::string const line = "\x97 32768\xaa\xbb(1)\xac"
                           "999,\xbb(1)\xac"
                           "255:A\xb2\xc2(59464):\x89 10";
  std::vector<std::uint8_t> prg{ 0x01, 0x04 };
  auto const next = static_cast<std::uint16_t>( 0x0401 + 2 + 2 + line.size() + 1 );
  prg.insert( prg.end(),
              { static_cast<std::uint8_t>( next & 0xffU ), static_cast<std::uint8_t>( next >> 8U ), 10, 0 } );
  prg.insert( prg.end(), line.begin(), line.end() );
  prg.insert( prg.end(), { 0, 0, 0 } );
  return prg;
}

} // namespace

TEST_CASE( "a state saved in the middle of a program runs on as the run that saved it", "[roms][state]" )
{
  auto machine = booted();
  if ( !machine )
  {
    SKIP( "no ROMs in " << PET_TEST_ROM_DIR );
  }
  REQUIRE( pet::machine::loadProgram( *machine, busyProgram() ) );
  pet::machine::queueKeys( *machine, "RUN\r" );
  machine->runFrames( 30 );
  // In the middle of a frame and of an instruction's run, not at a boundary
  // of either.
  machine->runCycles( 12345 );

  std::vector<std::uint8_t> const state = machine->saveState();
  machine->runFrames( 120 );
  std::uint64_t const expected = sumOf( *machine );

  REQUIRE( machine->loadState( state ) );
  machine->runFrames( 120 );
  CHECK( sumOf( *machine ) == expected );

  // A machine of its own, powered up anew, takes the state as well.
  auto other = booted();
  REQUIRE( other->loadState( state ) );
  other->runFrames( 120 );
  CHECK( sumOf( *other ) == expected );
}

TEST_CASE( "a state that is not one leaves the machine as it was", "[roms][state]" )
{
  auto machine = booted();
  if ( !machine )
  {
    SKIP( "no ROMs in " << PET_TEST_ROM_DIR );
  }
  std::uint64_t const before = sumOf( *machine );
  std::vector<std::uint8_t> state = machine->saveState();

  CHECK_FALSE( machine->loadState( std::vector<std::uint8_t>{ 1, 2, 3 } ) );
  state.resize( state.size() - 10 );
  CHECK_FALSE( machine->loadState( state ) );
  CHECK( sumOf( *machine ) == before );
}

TEST_CASE( "a breakpoint stops a run before its instruction, which runs when the run goes on", "[roms][debug]" )
{
  auto machine = booted();
  if ( !machine )
  {
    SKIP( "no ROMs in " << PET_TEST_ROM_DIR );
  }
  // The kernal's interrupt entry, once a frame.
  machine->addBreakpoint( 0xe61b );
  auto const first = machine->runFrames( 5 );
  REQUIRE( first.reason == StopReason::BREAKPOINT );
  CHECK( first.breakpoint == 0xe61b );
  CHECK( machine->cpu().pc == 0xe61b );

  auto const second = machine->runFrames( 5 );
  REQUIRE( second.reason == StopReason::BREAKPOINT );
  CHECK( second.cycles > 16000 );

  CHECK( machine->removeBreakpoint( 0xe61b ) );
  CHECK( machine->runFrames( 5 ).reason == StopReason::COMPLETED );
}

TEST_CASE( "a watchpoint stops a run after the instruction that read the watched address", "[roms][debug]" )
{
  auto machine = booted();
  if ( !machine )
  {
    SKIP( "no ROMs in " << PET_TEST_ROM_DIR );
  }
  // The keyboard's columns, which the kernal reads in its interrupt.
  machine->addWatchpoint( { .address = 0xe812, .size = 1, .access = pet::machine::Access::READ } );
  auto const run = machine->runFrames( 5 );
  REQUIRE( run.reason == StopReason::WATCHPOINT );
  CHECK( run.watchpoint.address == 0xe812 );
  CHECK_FALSE( run.watchpoint.write );

  // The trace's last instruction but the one the PET stands at made it.
  auto const trace = machine->trace( machine->traceLength() - 2, 2 );
  REQUIRE( trace.size() == 2 );
  CHECK( trace[0].registers.pc == run.watchpoint.pc );
  CHECK( trace[1].registers.pc == machine->cpu().pc );
  // The instruction as it ran: LDA $E812 or another read of it.
  CHECK( trace[0].bytes[1] == 0x12 );
  CHECK( trace[0].bytes[2] == 0xe8 );
}

TEST_CASE( "the history keeps every instruction of the time it is set to, oldest first", "[roms][debug]" )
{
  auto machine = booted();
  if ( !machine )
  {
    SKIP( "no ROMs in " << PET_TEST_ROM_DIR );
  }
  // A frame's worth.
  machine->setTraceCycles( 16640 );
  machine->runFrames( 3 );
  auto const history = machine->trace( 0, machine->traceLength() );
  REQUIRE( history.size() == machine->traceLength() );
  REQUIRE( history.size() > 3000 );
  CHECK( std::ranges::is_sorted( history, {}, &pet::machine::TracedInstruction::cycle ) );
  CHECK( history.front().cycle + 16640 >= machine->now() );
  // A frame's worth of 2- to 7-cycle instructions, and no more.
  CHECK( history.front().cycle < machine->now() - 16000 );
  CHECK( history.back().registers.pc == machine->cpu().pc );
  CHECK( machine->trace( history.size() - 1, 10 ).size() == 1 );

  // Shortened, it lets the oldest go at once.
  machine->setTraceCycles( 1000 );
  CHECK( machine->traceLength() < 500 );
}

TEST_CASE( "a read watchpoint does not stop at reads of the instruction stream", "[roms][debug]" )
{
  auto machine = booted();
  if ( !machine )
  {
    SKIP( "no ROMs in " << PET_TEST_ROM_DIR );
  }
  // The kernal's interrupt code is read as it runs, operands and dummy reads
  // of the next opcode included, but never as data.
  machine->addWatchpoint( { .address = 0xe600, .size = 0x200, .access = pet::machine::Access::READ } );
  CHECK( machine->runFrames( 3 ).reason == StopReason::COMPLETED );
}
