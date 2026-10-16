#include <catch2/catch_test_macros.hpp>

#include "machine/Via6522.hpp"

#include <cstdint>

// The VIA's timers as VICE's viacore.c counts them, with nothing on its pins:
// what the CPU reads in each cycle after a write, and when the interrupt
// follows.

using pet::machine::Clock;
using pet::machine::Via6522;

namespace
{

class NoPins final : public pet::machine::ViaPorts
{
public:
  std::uint8_t readPra( bool /*peek*/ ) override
  {
    return 0xff;
  }

  std::uint8_t readPrb( bool /*peek*/ ) override
  {
    return 0xff;
  }

  void storePra( std::uint8_t /*byte*/ ) override {}

  void storePrb( std::uint8_t /*byte*/ ) override {}

  void setCa2( bool /*state*/ ) override {}

  void setCb2( bool /*state*/, Clock /*at*/ ) override {}
};

/// A VIA reset at cycle 0, whose cycles up to `until` have been run.
struct Bench
{
  NoPins pins;
  Via6522 via{ pins };
  Clock now{};

  Bench()
  {
    via.reset( 0 );
  }

  void runTo( Clock until )
  {
    for ( ; now <= until; ++now )
    {
      via.cycle( now );
    }
  }

  std::uint16_t t1At( Clock at )
  {
    runTo( at - 1 );
    auto const low = via.peek( Via6522::T1CL, at );
    auto const high = via.peek( Via6522::T1CH, at );
    return static_cast<std::uint16_t>( ( high << 8U ) | low );
  }
};

} // namespace

TEST_CASE( "T1 counts down from the cycle after its high byte is written, through 0 and FFFF", "[via]" )
{
  Bench bench;
  bench.via.store( Via6522::T1LL, 3, 10 );
  bench.via.store( Via6522::T1CH, 0, 11 );

  CHECK( bench.t1At( 12 ) == 3 );
  CHECK( bench.t1At( 13 ) == 2 );
  CHECK( bench.t1At( 14 ) == 1 );
  CHECK( bench.t1At( 15 ) == 0 );
  CHECK( bench.t1At( 16 ) == 0xffff );
  // It reloads from the latch, in one-shot mode too.
  CHECK( bench.t1At( 17 ) == 3 );
}

TEST_CASE( "T1's flag is set after it reads 0, and the IRQ line follows a cycle later", "[via]" )
{
  Bench bench;
  bench.via.store( Via6522::IER, 0xc0, 5 );
  bench.via.store( Via6522::T1LL, 3, 10 );
  bench.via.store( Via6522::T1CH, 0, 11 );

  bench.runTo( 14 );
  CHECK( ( bench.via.peek( Via6522::IFR, 15 ) & 0x40U ) == 0 );
  bench.runTo( 15 );
  CHECK( ( bench.via.peek( Via6522::IFR, 16 ) & 0x40U ) != 0 );
  CHECK_FALSE( bench.via.irq( 15 ) );
  CHECK( bench.via.irq( 16 ) );

  // Reading T1's low byte clears it.
  static_cast<void>( bench.via.read( Via6522::T1CL, 20 ) );
  CHECK_FALSE( bench.via.irq( 20 ) );
}

TEST_CASE( "a one-shot T1 interrupts once for each write of its high byte", "[via]" )
{
  Bench bench;
  bench.via.store( Via6522::IER, 0xc0, 5 );
  bench.via.store( Via6522::T1LL, 3, 10 );
  bench.via.store( Via6522::T1CH, 0, 11 );
  bench.runTo( 16 );
  static_cast<void>( bench.via.read( Via6522::T1CL, 17 ) );
  bench.runTo( 40 );
  CHECK_FALSE( bench.via.irq( 40 ) );
}

TEST_CASE( "a free-running T1 interrupts every latch plus two cycles", "[via]" )
{
  Bench bench;
  bench.via.store( Via6522::ACR, 0x40, 5 );
  bench.via.store( Via6522::T1LL, 3, 10 );
  bench.via.store( Via6522::T1CH, 0, 11 );
  bench.runTo( 15 );
  CHECK( ( bench.via.peek( Via6522::IFR, 16 ) & 0x40U ) != 0 );
  static_cast<void>( bench.via.read( Via6522::T1CL, 16 ) );
  bench.runTo( 19 );
  CHECK( ( bench.via.peek( Via6522::IFR, 20 ) & 0x40U ) == 0 );
  bench.runTo( 20 );
  CHECK( ( bench.via.peek( Via6522::IFR, 21 ) & 0x40U ) != 0 );
}

TEST_CASE( "T2 counts down from the cycle after its high byte is written, and interrupts at FFFF", "[via]" )
{
  Bench bench;
  bench.via.store( Via6522::IER, 0xa0, 5 );
  bench.via.store( Via6522::T2CL, 2, 10 );
  bench.via.store( Via6522::T2CH, 0, 11 );

  bench.runTo( 11 );
  CHECK( bench.via.peek( Via6522::T2CL, 12 ) == 2 );
  bench.runTo( 13 );
  CHECK( bench.via.peek( Via6522::T2CL, 14 ) == 0 );
  CHECK_FALSE( bench.via.irq( 13 ) );
  bench.runTo( 14 );
  CHECK( bench.via.peek( Via6522::T2CL, 15 ) == 0xff );
  CHECK( bench.via.peek( Via6522::T2CH, 15 ) == 0xff );
  CHECK( bench.via.irq( 14 ) );
}
