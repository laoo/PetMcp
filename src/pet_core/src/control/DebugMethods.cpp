#include "Methods.hpp"

#include "cpu/Disassembler.hpp"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cctype>
#include <optional>
#include <ranges>
#include <string>

namespace pet::control
{

namespace
{

constexpr std::uint64_t MAX_STEPS = 1'000'000;
constexpr std::uint64_t DEFAULT_TRACE = 32;
constexpr std::uint64_t MAX_TRACE = 4096;
constexpr double CYCLES_PER_SECOND = 1'000'000.0;
constexpr double MIN_TRACE_SECONDS = 0.01;
constexpr double MAX_TRACE_SECONDS = 60.0;

std::expected<std::uint16_t, Error> addressOf( Json const& params )
{
  auto const address = requireUnsigned( params, "address" );
  if ( !address )
  {
    return std::unexpected( address.error() );
  }
  if ( *address > 0xffff )
  {
    return std::unexpected( badRequest( "address is 0 to $FFFF" ) );
  }
  return static_cast<std::uint16_t>( *address );
}

std::expected<machine::Access, Error> accessOf( Json const& params )
{
  if ( !params.contains( "access" ) )
  {
    return machine::Access::WRITE;
  }
  auto const name = requireString( params, "access" );
  if ( name && *name == "read" )
  {
    return machine::Access::READ;
  }
  if ( name && *name == "write" )
  {
    return machine::Access::WRITE;
  }
  if ( name && *name == "access" )
  {
    return machine::Access::BOTH;
  }
  return std::unexpected( badRequest( "access is read, write or access" ) );
}

char const* nameOf( machine::Access access )
{
  switch ( access )
  {
  case machine::Access::READ:
    return "read";
  case machine::Access::WRITE:
    return "write";
  case machine::Access::BOTH:
    return "access";
  }
  return "?";
}

/// An instruction of the history as debug.trace answers it, its bytes and
/// disassembly as they were when it ran.
Json tracedJson( machine::TracedInstruction const& traced, std::uint64_t index, cpu::Instruction const& instruction )
{
  machine::CpuState const& r = traced.registers;
  std::string bytes;
  for ( std::uint8_t const byte : instruction.bytes )
  {
    bytes += fmt::format( "{:02x}", byte );
  }
  return Json{ { "index", index },
               { "pc", r.pc },
               { "cycle", traced.cycle },
               { "a", r.a },
               { "x", r.x },
               { "y", r.y },
               { "s", r.s },
               { "p", r.p },
               { "bytes_hex", std::move( bytes ) },
               { "disasm", instruction.text } };
}

/// The instruction `traced` ran, from the bytes noted when it ran.
cpu::Instruction disassembled( machine::TracedInstruction const& traced )
{
  auto const peek = [&traced]( std::uint16_t address )
  { return traced.bytes.at( static_cast<std::uint16_t>( address - traced.registers.pc ) % 3U ); };
  return cpu::disassemble( traced.registers.pc, peek );
}

std::string upper( std::string text )
{
  std::ranges::transform(
      text, text.begin(), []( unsigned char c ) { return static_cast<char>( std::toupper( c ) ); } );
  return text;
}

/// An optional byte-sized parameter.
std::expected<std::optional<std::uint8_t>, Error> optionalByte( Json const& params, std::string_view name )
{
  if ( !params.contains( name ) )
  {
    return std::optional<std::uint8_t>{};
  }
  auto const value = requireUnsigned( params, name );
  if ( !value || *value > 0xff )
  {
    return std::unexpected( badRequest( fmt::format( "{} is a byte, 0 to $FF", name ) ) );
  }
  return std::optional<std::uint8_t>{ static_cast<std::uint8_t>( *value ) };
}

Json piaJson( machine::PiaState const& pia )
{
  return Json{ { "port_a", pia.portA }, { "ddr_a", pia.ddrA }, { "control_a", pia.controlA },
               { "port_b", pia.portB }, { "ddr_b", pia.ddrB }, { "control_b", pia.controlB },
               { "ca2", pia.ca2 },      { "cb2", pia.cb2 } };
}

} // namespace

void addDebugMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add(
      "emu.step",
      info( "Runs instructions, one when count is left out, stopping before at a breakpoint or after a watched "
            "access.",
            { { .name = "count",
                .type = "integer",
                .description = "How many instructions, 1 to 1,000,000.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        std::uint64_t count = 1;
        if ( params.contains( "count" ) )
        {
          auto const given = requireUnsigned( params, "count" );
          if ( !given || *given == 0 || *given > MAX_STEPS )
          {
            return std::unexpected( badRequest( "count is 1 to 1,000,000" ) );
          }
          count = *given;
        }
        return runJson( ( *machine )->step( count ) );
      } );

  dispatcher.add( "debug.breakpoint.add",
                  info( "Stops runs before the instruction at an address. The instruction the PET stands at runs "
                        "when it runs on.",
                        { { .name = "address", .type = "integer", .description = "The instruction's address." } } ),
                  [&emulator]( Json const& params ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    auto const address = addressOf( params );
                    if ( !machine || !address )
                    {
                      return std::unexpected( !machine ? machine.error() : address.error() );
                    }
                    ( *machine )->addBreakpoint( *address );
                    return Json::object();
                  } );

  dispatcher.add( "debug.breakpoint.remove",
                  info( "Removes the breakpoint at an address.",
                        { { .name = "address", .type = "integer", .description = "The instruction's address." } } ),
                  [&emulator]( Json const& params ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    auto const address = addressOf( params );
                    if ( !machine || !address )
                    {
                      return std::unexpected( !machine ? machine.error() : address.error() );
                    }
                    ( *machine )->removeBreakpoint( *address );
                    return Json::object();
                  } );

  dispatcher.add( "debug.breakpoint.list",
                  info( "The breakpoints' addresses, ascending." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    return Json{ { "breakpoints", ( *machine )->breakpoints() } };
                  } );

  dispatcher.add(
      "debug.watchpoint.add",
      info( "Stops runs after the instruction that reads or writes data in a range: any access of its own beyond "
            "fetching its bytes, dummy accesses included. One per address; a second replaces the first.",
            { { .name = "address", .type = "integer", .description = "The range's first address." },
              { .name = "size",
                .type = "integer",
                .description = "Bytes in the range; 1 when left out.",
                .required = false },
              { .name = "access",
                .type = "string",
                .description = "read, write (the default) or access for both.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        auto const address = addressOf( params );
        if ( !machine || !address )
        {
          return std::unexpected( !machine ? machine.error() : address.error() );
        }
        auto const access = accessOf( params );
        if ( !access )
        {
          return std::unexpected( access.error() );
        }
        std::uint64_t size = 1;
        if ( params.contains( "size" ) )
        {
          auto const given = requireUnsigned( params, "size" );
          if ( !given || *given == 0 || *given > std::uint64_t{ 0x10000 } - *address )
          {
            return std::unexpected( badRequest( "size is 1 to the end of the address space" ) );
          }
          size = *given;
        }
        ( *machine )
            ->addWatchpoint( machine::Watchpoint{
                .address = *address, .size = static_cast<std::uint32_t>( size ), .access = *access } );
        return Json::object();
      } );

  dispatcher.add( "debug.watchpoint.remove",
                  info( "Removes the watchpoint at an address.",
                        { { .name = "address", .type = "integer", .description = "The range's first address." } } ),
                  [&emulator]( Json const& params ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    auto const address = addressOf( params );
                    if ( !machine || !address )
                    {
                      return std::unexpected( !machine ? machine.error() : address.error() );
                    }
                    ( *machine )->removeWatchpoint( *address );
                    return Json::object();
                  } );

  dispatcher.add( "debug.watchpoint.list",
                  info( "The watchpoints, by address." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    Json watchpoints = Json::array();
                    for ( machine::Watchpoint const& watchpoint : ( *machine )->watchpoints() )
                    {
                      watchpoints.push_back( Json{ { "address", watchpoint.address },
                                                   { "size", watchpoint.size },
                                                   { "access", nameOf( watchpoint.access ) } } );
                    }
                    return Json{ { "watchpoints", std::move( watchpoints ) } };
                  } );

  dispatcher.add(
      "debug.trace",
      info( "The history of execution, counted back from now: index 0 is the newest instruction, the one the PET "
            "stands at, 1 the one that ran before it, and so on, over the last seconds debug.trace.configure sets. "
            "Answers count of them from index from, newest first, each with the cycle it began in, the registers "
            "before it, and its bytes and disassembly as they were when it ran.",
            { { .name = "count",
                .type = "integer",
                .description = "How many, 0 to 4096; 32 when left out.",
                .required = false },
              { .name = "from",
                .type = "integer",
                .description = "The newest one's index, 0 being the newest kept; 0 when left out.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        std::uint64_t count = DEFAULT_TRACE;
        if ( params.contains( "count" ) )
        {
          auto const given = requireUnsigned( params, "count" );
          if ( !given || *given > MAX_TRACE )
          {
            return std::unexpected( badRequest( "count is 0 to 4096" ) );
          }
          count = *given;
        }
        std::uint64_t from = 0;
        if ( params.contains( "from" ) )
        {
          auto const given = requireUnsigned( params, "from" );
          if ( !given )
          {
            return std::unexpected( given.error() );
          }
          from = *given;
        }
        std::uint64_t const length = ( *machine )->traceLength();
        from = std::min( from, length );
        count = std::min( count, length - from );
        // Index i is the (length - 1 - i)th from the oldest.
        auto const kept =
            ( *machine )->trace( static_cast<std::size_t>( length - from - count ), static_cast<std::size_t>( count ) );
        Json instructions = Json::array();
        std::uint64_t index = from;
        for ( machine::TracedInstruction const& traced : std::views::reverse( kept ) )
        {
          instructions.push_back( tracedJson( traced, index++, disassembled( traced ) ) );
        }
        return Json{ { "total", length },
                     { "seconds", static_cast<double>( ( *machine )->traceCycles() ) / CYCLES_PER_SECOND },
                     { "instructions", std::move( instructions ) } };
      } );

  dispatcher.add( "debug.trace.configure",
                  info( "How many seconds of the PET's time the history of execution keeps, 0.01 to 60, from now "
                        "and after every power-up. A second is some 300,000 instructions, 5 MB.",
                        { { .name = "seconds", .type = "number", .description = "The history's length." } } ),
                  [&emulator]( Json const& params ) -> Outcome
                  {
                    auto const field = params.find( "seconds" );
                    if ( field == params.end() || !field->is_number() || field->get<double>() < MIN_TRACE_SECONDS ||
                         field->get<double>() > MAX_TRACE_SECONDS )
                    {
                      return std::unexpected( badRequest( "seconds is 0.01 to 60" ) );
                    }
                    double const seconds = field->get<double>();
                    emulator.setTraceCycles( static_cast<std::uint64_t>( seconds * CYCLES_PER_SECOND ) );
                    return Json{ { "seconds", seconds } };
                  } );

  dispatcher.add(
      "debug.trace.find",
      info( "Searches the history of execution back from index from, 0 being the newest, for instructions that "
            "meet every condition given, and answers up to limit of them, newest first, as debug.trace does. "
            "next_from continues the search where it stopped, or is null when it reached the oldest.",
            { { .name = "pc", .type = "integer", .description = "The instruction's address.", .required = false },
              { .name = "pc_start",
                .type = "integer",
                .description = "With pc_end: the instruction's address is at least this.",
                .required = false },
              { .name = "pc_end",
                .type = "integer",
                .description = "With pc_start: the instruction's address is below this.",
                .required = false },
              { .name = "disasm_contains",
                .type = "string",
                .description = "Its disassembly, as it ran, holds this text, case aside: STA $80, E812, JSR.",
                .required = false },
              { .name = "a", .type = "integer", .description = "A before it.", .required = false },
              { .name = "x", .type = "integer", .description = "X before it.", .required = false },
              { .name = "y", .type = "integer", .description = "Y before it.", .required = false },
              { .name = "s", .type = "integer", .description = "S before it.", .required = false },
              { .name = "from",
                .type = "integer",
                .description = "The index to search back from; 0, the newest, when left out.",
                .required = false },
              { .name = "limit",
                .type = "integer",
                .description = "The most matches to answer, 1 to 4096; 32 when left out.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        // The address range: pc alone, or pc_start and pc_end.
        std::uint32_t pcStart = 0;
        std::uint32_t pcEnd = 0x10000;
        if ( params.contains( "pc" ) )
        {
          auto const pc = requireUnsigned( params, "pc" );
          if ( !pc || *pc > 0xffff )
          {
            return std::unexpected( badRequest( "pc is 0 to $FFFF" ) );
          }
          pcStart = static_cast<std::uint32_t>( *pc );
          pcEnd = pcStart + 1;
        }
        else if ( params.contains( "pc_start" ) || params.contains( "pc_end" ) )
        {
          auto const first = requireUnsigned( params, "pc_start" );
          auto const end = requireUnsigned( params, "pc_end" );
          if ( !first || !end || *first >= *end || *end > 0x10000 )
          {
            return std::unexpected( badRequest( "give pc_start and pc_end, pc_start below pc_end, up to $10000" ) );
          }
          pcStart = static_cast<std::uint32_t>( *first );
          pcEnd = static_cast<std::uint32_t>( *end );
        }
        std::optional<std::string> text;
        if ( params.contains( "disasm_contains" ) )
        {
          auto const given = requireString( params, "disasm_contains" );
          if ( !given || given->empty() )
          {
            return std::unexpected( badRequest( "disasm_contains is a text to look for" ) );
          }
          text = upper( *given );
        }
        auto const a = optionalByte( params, "a" );
        auto const x = optionalByte( params, "x" );
        auto const y = optionalByte( params, "y" );
        auto const sp = optionalByte( params, "s" );
        for ( auto const* byte : { &a, &x, &y, &sp } )
        {
          if ( !*byte )
          {
            return std::unexpected( byte->error() );
          }
        }
        std::uint64_t from = 0;
        if ( params.contains( "from" ) )
        {
          auto const given = requireUnsigned( params, "from" );
          if ( !given )
          {
            return std::unexpected( given.error() );
          }
          from = *given;
        }
        std::uint64_t limit = DEFAULT_TRACE;
        if ( params.contains( "limit" ) )
        {
          auto const given = requireUnsigned( params, "limit" );
          if ( !given || *given == 0 || *given > MAX_TRACE )
          {
            return std::unexpected( badRequest( "limit is 1 to 4096" ) );
          }
          limit = *given;
        }

        auto const matchesByte = []( std::optional<std::uint8_t> const& wanted, std::uint8_t value )
        { return !wanted || *wanted == value; };
        machine::Machine const* const pet = *machine;
        std::uint64_t const length = pet->traceLength();
        Json matches = Json::array();
        std::uint64_t index = from;
        for ( ; index < length && matches.size() < limit; ++index )
        {
          machine::TracedInstruction const traced = pet->tracedAt( static_cast<std::size_t>( length - 1 - index ) );
          machine::CpuState const& r = traced.registers;
          if ( r.pc < pcStart || r.pc >= pcEnd || !matchesByte( *a, r.a ) || !matchesByte( *x, r.x ) ||
               !matchesByte( *y, r.y ) || !matchesByte( *sp, r.s ) )
          {
            continue;
          }
          // The disassembly last: it is what costs.
          cpu::Instruction const instruction = disassembled( traced );
          if ( text && !upper( instruction.text ).contains( *text ) )
          {
            continue;
          }
          matches.push_back( tracedJson( traced, index, instruction ) );
        }
        return Json{ { "total", length },
                     { "matches", std::move( matches ) },
                     { "next_from", index < length ? Json( index ) : Json( nullptr ) } };
      } );

  dispatcher.add( "io.get_state",
                  info( "The PIAs' and the VIA's registers, read without side effects, the VIA's timers as they "
                        "count, and the IRQ line." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    machine::IoState const io = ( *machine )->io();
                    return Json{ { "pia1", piaJson( io.pia1 ) },
                                 { "pia2", piaJson( io.pia2 ) },
                                 { "via",
                                   { { "registers", io.via.registers },
                                     { "t1", io.via.t1 },
                                     { "t1_latch", io.via.t1Latch },
                                     { "t2", io.via.t2 },
                                     { "ifr", io.via.ifr },
                                     { "ier", io.via.ier } } },
                                 { "irq", io.irq } };
                  } );
}

} // namespace pet::control
