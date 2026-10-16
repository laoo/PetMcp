#include "pet/machine/Programs.hpp"

#include <spdlog/fmt/fmt.h>

namespace pet::machine
{

namespace
{

// BASIC 2's zero page and the kernal's keyboard buffer, as the 3032's ROMs
// place them.
constexpr std::uint16_t VARTAB = 0x2a;
constexpr std::uint16_t KEYBOARD_COUNT = 0x9e;
constexpr std::uint16_t KEYBOARD_BUFFER = 0x026f;
constexpr std::size_t KEYBOARD_SIZE = 10;
constexpr std::uint32_t RAM_END = 0x8000;

} // namespace

std::expected<LoadedProgram, std::string> loadProgram( Machine& machine, std::span<std::uint8_t const> prg )
{
  if ( prg.size() < 2 )
  {
    return std::unexpected( std::string{ "a program starts with its load address, two bytes" } );
  }
  auto const start = static_cast<std::uint16_t>( prg[0] | ( prg[1] << 8U ) );
  std::uint32_t const end = start + static_cast<std::uint32_t>( prg.size() - 2 );
  if ( end > RAM_END )
  {
    return std::unexpected( fmt::format( "${:04X}-${:04X} does not fit in RAM", start, end - 1 ) );
  }
  for ( std::size_t i = 2; i < prg.size(); ++i )
  {
    machine.poke( static_cast<std::uint16_t>( start + i - 2 ), prg[i] );
  }
  machine.poke( VARTAB, static_cast<std::uint8_t>( end & 0xffU ) );
  machine.poke( VARTAB + 1, static_cast<std::uint8_t>( end >> 8U ) );
  return LoadedProgram{ .start = start, .end = end };
}

std::size_t queueKeys( Machine& machine, std::string_view keys )
{
  std::size_t count = machine.peek( KEYBOARD_COUNT );
  std::size_t taken = 0;
  for ( char const key : keys )
  {
    if ( count >= KEYBOARD_SIZE )
    {
      break;
    }
    machine.poke( static_cast<std::uint16_t>( KEYBOARD_BUFFER + count ), static_cast<std::uint8_t>( key ) );
    ++count;
    ++taken;
  }
  machine.poke( KEYBOARD_COUNT, static_cast<std::uint8_t>( count ) );
  return taken;
}

RunResult typeKeys( Machine& machine, std::string_view keys, std::uint64_t timeoutFrames )
{
  RunResult total{ .reason = StopReason::COMPLETED };
  std::uint64_t const start = machine.frame();
  while ( !keys.empty() )
  {
    keys.remove_prefix( queueKeys( machine, keys ) );
    for ( ;; )
    {
      RunResult const run = machine.runFrames( 1 );
      total.cycles += run.cycles;
      total.frames += run.frames;
      total.reason = run.reason;
      if ( run.reason != StopReason::COMPLETED )
      {
        return total;
      }
      if ( machine.peek( KEYBOARD_COUNT ) == 0 )
      {
        break;
      }
      if ( machine.frame() - start >= timeoutFrames )
      {
        total.reason = StopReason::TIMEOUT;
        return total;
      }
    }
  }
  return total;
}

std::string petsciiTyped( std::string_view text )
{
  std::string typed;
  while ( !text.empty() )
  {
    if ( text.starts_with( "\xe2\x86\x90" ) )
    {
      typed.push_back( '\x5f' );
      text.remove_prefix( 3 );
      continue;
    }
    if ( text.starts_with( "\xe2\x86\x91" ) )
    {
      typed.push_back( '\x5e' );
      text.remove_prefix( 3 );
      continue;
    }
    if ( text.starts_with( "\xc2\xa3" ) )
    {
      typed.push_back( '\x5c' );
      text.remove_prefix( 2 );
      continue;
    }
    char c = text.front();
    if ( c >= 'a' && c <= 'z' )
    {
      c = static_cast<char>( c - 'a' + 'A' );
    }
    typed.push_back( c );
    text.remove_prefix( 1 );
  }
  return typed;
}

} // namespace pet::machine
