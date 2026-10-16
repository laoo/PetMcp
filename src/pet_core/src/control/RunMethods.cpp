#include "Methods.hpp"

#include "pet/machine/Screen.hpp"

#include <algorithm>
#include <optional>
#include <string>

namespace pet::control
{

namespace
{

/// Twenty seconds of the PET's time.
constexpr std::uint64_t DEFAULT_TIMEOUT = 20'000'000;

/// A run's `count`, which must be positive.
std::expected<std::uint64_t, Error> countOf( Json const& params )
{
  auto const count = requireUnsigned( params, "count" );
  if ( count && *count == 0 )
  {
    return std::unexpected( badRequest( "count must be at least 1" ) );
  }
  return count;
}

} // namespace

Json runJson( machine::RunResult const& result )
{
  char const* reason = "completed";
  if ( result.reason == machine::StopReason::CONDITION )
  {
    reason = "condition_met";
  }
  else if ( result.reason == machine::StopReason::TIMEOUT )
  {
    reason = "timeout";
  }
  return Json{ { "reason", reason }, { "cycles_executed", result.cycles }, { "frames_executed", result.frames } };
}

void addRunMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add(
      "emu.run_frames",
      info( "Runs through frame boundaries, where the vertical blank begins, 60.1 a second, and stops at the next "
            "instruction boundary.",
            { { .name = "count", .type = "integer", .description = "Frame boundaries to run through." } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        auto const count = countOf( params );
        if ( !count )
        {
          return std::unexpected( count.error() );
        }
        return runJson( ( *machine )->runFrames( *count ) );
      } );

  dispatcher.add( "emu.run_cycles",
                  info( "Runs cycles of the 1 MHz clock, and stops at the next instruction boundary.",
                        { { .name = "count", .type = "integer", .description = "Cycles to run at least." } } ),
                  [&emulator]( Json const& params ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    auto const count = countOf( params );
                    if ( !count )
                    {
                      return std::unexpected( count.error() );
                    }
                    return runJson( ( *machine )->runCycles( *count ) );
                  } );

  dispatcher.add(
      "emu.run_until",
      info( "Runs until a condition holds at an instruction boundary: the 6502 about to run the instruction at pc, "
            "or the screen showing a text. Give one or both; with both, either stops it.",
            { { .name = "pc",
                .type = "integer",
                .description = "Stop before the instruction at this address.",
                .required = false },
              { .name = "screen_contains",
                .type = "string",
                .description = "Stop when the screen, as video.screen_text reads it, shows this text on one line.",
                .required = false },
              { .name = "timeout_cycles",
                .type = "integer",
                .description = "Cycles after which to give up; 20,000,000 (20 s) when left out.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        std::optional<std::uint16_t> pc;
        if ( params.contains( "pc" ) )
        {
          auto const address = requireUnsigned( params, "pc" );
          if ( !address || *address > 0xffff )
          {
            return std::unexpected( badRequest( "pc is an address, 0 to $FFFF" ) );
          }
          pc = static_cast<std::uint16_t>( *address );
        }
        std::optional<std::string> text;
        if ( params.contains( "screen_contains" ) )
        {
          auto const wanted = requireString( params, "screen_contains" );
          if ( !wanted || wanted->empty() )
          {
            return std::unexpected( badRequest( "screen_contains is a text to look for" ) );
          }
          text = *wanted;
        }
        if ( !pc && !text )
        {
          return std::unexpected( badRequest( "give pc, screen_contains or both" ) );
        }
        std::uint64_t timeout = DEFAULT_TIMEOUT;
        if ( params.contains( "timeout_cycles" ) )
        {
          auto const given = requireUnsigned( params, "timeout_cycles" );
          if ( !given )
          {
            return std::unexpected( given.error() );
          }
          timeout = *given;
        }
        // The screen is read once a frame: it is drawn once a frame.
        std::uint64_t lastFrameRead = ~std::uint64_t{};
        auto const condition = [&]( machine::Machine const& pet )
        {
          if ( pc && pet.cpu().pc == *pc )
          {
            return true;
          }
          if ( text && pet.frame() != lastFrameRead )
          {
            lastFrameRead = pet.frame();
            auto const lines = machine::screenText( pet );
            return std::ranges::any_of( lines, [&]( std::string const& line ) { return line.contains( *text ); } );
          }
          return false;
        };
        return runJson( ( *machine )->runUntil( condition, timeout ) );
      } );
}

} // namespace pet::control
