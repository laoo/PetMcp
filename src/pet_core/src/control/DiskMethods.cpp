#include "Methods.hpp"

#include "pet/machine/Keyboard.hpp"
#include "pet/machine/Programs.hpp"
#include "pet/machine/Screen.hpp"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>

namespace pet::control
{

namespace
{

/// The frame by which the PET has printed READY. after power-up, with room.
constexpr std::uint64_t READY_FRAME = 100;
/// How long disk.run lets a LOAD take: a minute of the PET's time.
constexpr std::uint64_t LOAD_FRAMES = std::uint64_t{ 60 } * 60;
constexpr std::uint64_t DEFAULT_TRACE = 64;
constexpr std::uint64_t MAX_TRACE = 4096;

Error diskError( std::string code, std::string message )
{
  return Error{ .code = std::move( code ), .message = std::move( message ) };
}

/// The number an error channel's message begins with.
int statusCode( std::string const& status )
{
  int code = 0;
  for ( char const c : status.substr( 0, 2 ) )
  {
    if ( c < '0' || c > '9' )
    {
      return -1;
    }
    code = ( code * 10 ) + ( c - '0' );
  }
  return code;
}

Json driveJson( machine::DriveState const& drive )
{
  return Json{ { "card", drive.card.string() },
               { "directory", drive.directory.string() },
               { "image", drive.image.string() },
               { "device", drive.device },
               { "status", drive.status } };
}

std::string upper( std::string text )
{
  std::ranges::transform(
      text, text.begin(), []( unsigned char c ) { return static_cast<char>( std::toupper( c ) ); } );
  return text;
}

/// A screen line without the spaces after it, in capitals.
std::string trimmed( std::string line )
{
  while ( !line.empty() && line.back() == ' ' )
  {
    line.pop_back();
  }
  return upper( line );
}

/// How a LOAD typed on the screen ended: empty while it runs, else the line
/// that ended it, READY. after LOADING or an error.
std::optional<std::string> loadEnded( machine::Machine const& machine )
{
  std::vector<std::string> const lines = machine::screenText( machine );
  std::optional<std::size_t> typed;
  for ( std::size_t row = 0; row < lines.size(); ++row )
  {
    if ( trimmed( lines[row] ).starts_with( "LOAD\"" ) )
    {
      typed = row;
    }
  }
  if ( !typed )
  {
    return std::nullopt;
  }
  bool loading = false;
  for ( std::size_t row = *typed + 1; row < lines.size(); ++row )
  {
    std::string line = trimmed( lines[row] );
    if ( line.starts_with( '?' ) )
    {
      return line;
    }
    if ( line == "LOADING" )
    {
      loading = true;
    }
    else if ( loading && line == "READY." )
    {
      return line;
    }
  }
  return std::nullopt;
}

/// Holds STOP until the PET is back at READY., for a few seconds at most.
machine::RunResult stopLoad( machine::Machine& machine )
{
  machine::RunResult total{ .reason = machine::StopReason::COMPLETED };
  machine::PetKey const stop = machine::keyNamed( "STOP" ).value_or( machine::PetKey{} );
  machine.setKey( stop, true );
  for ( int frame = 0; frame < 300; ++frame )
  {
    machine::RunResult const run = machine.runFrames( 1 );
    total.cycles += run.cycles;
    total.frames += run.frames;
    total.reason = run.reason;
    std::vector<std::string> const lines = machine::screenText( machine );
    bool const ready =
        std::ranges::any_of( lines, []( std::string const& line ) { return trimmed( line ).contains( "BREAK" ); } );
    if ( run.reason != machine::StopReason::COMPLETED || ready )
    {
      break;
    }
  }
  machine.setKey( stop, false );
  return total;
}

std::string linesOf( std::uint8_t lines )
{
  static constexpr std::array<std::pair<std::uint8_t, std::string_view>, 7> NAMES{ {
      { machine::IEEE_ATN, "ATN" },
      { machine::IEEE_DAV, "DAV" },
      { machine::IEEE_NRFD, "NRFD" },
      { machine::IEEE_NDAC, "NDAC" },
      { machine::IEEE_EOI, "EOI" },
      { machine::IEEE_IFC, "IFC" },
      { machine::IEEE_SRQ, "SRQ" },
  } };
  std::string text;
  for ( auto const& [bit, name] : NAMES )
  {
    if ( ( lines & bit ) != 0 )
    {
      text += text.empty() ? "" : " ";
      text += name;
    }
  }
  return text;
}

} // namespace

void addDiskMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add( "disk.status",
                  info( "The SD2PET on the IEEE-488 port, device 8: the directory standing for its SD card, the "
                        "directory current on it and the image entered there, as host paths, its device number, "
                        "and its error channel's message as the PET would read it now, without reading it." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    return driveJson( ( *machine )->drive() );
                  } );

  dispatcher.add(
      "disk.card",
      info( "Puts a host directory in the SD2PET as its SD card, for this PET and those powered up after it; an "
            "empty path takes the card out. The drive starts afresh at the card's root, as after a card change.",
            { { .name = "path", .type = "string", .description = "The directory." } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const path = requireString( params, "path" );
        if ( !path )
        {
          return std::unexpected( path.error() );
        }
        if ( !path->empty() && !std::filesystem::is_directory( *path ) )
        {
          return std::unexpected( diskError( "not_found", fmt::format( "No directory {}", *path ) ) );
        }
        emulator.setCardDirectory( *path );
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        return driveJson( ( *machine )->drive() );
      } );

  dispatcher.add(
      "disk.mount",
      info( "Enters a disk image (D64, D71, D81, D80, D82, DNP) in the SD2PET, wherever on the host it is, as CD of "
            "it would from its directory, which becomes the current one. Answers the drive, its status the one "
            "the mount left.",
            { { .name = "path", .type = "string", .description = "The image file." } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        auto const path = requireString( params, "path" );
        if ( !path )
        {
          return std::unexpected( path.error() );
        }
        if ( !std::filesystem::is_regular_file( *path ) )
        {
          return std::unexpected( diskError( "not_found", fmt::format( "No file {}", *path ) ) );
        }
        std::string const status = ( *machine )->mountDisk( std::filesystem::absolute( *path ) );
        if ( statusCode( status ) >= 20 )
        {
          return std::unexpected( diskError( "mount_failed", status ) );
        }
        return driveJson( ( *machine )->drive() );
      } );

  dispatcher.add( "disk.unmount",
                  info( "Leaves the image entered in the SD2PET, as CD\xe2\x86\x90 does. Answers the drive." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    ( *machine )->diskCommand( "CD_" );
                    return driveJson( ( *machine )->drive() );
                  } );

  dispatcher.add(
      "disk.command",
      info( "Runs a DOS command in the SD2PET as if the PET had sent it on channel 15 (CD:NAME, S:NAME, N:NAME,ID, "
            "U1 2 0 18 0 ...), at once and beside the bus, and answers the status it leaves. Letters of either case "
            "are sent as the PET's keyboard types them; \xe2\x86\x90 (or _) is the left arrow.",
            { { .name = "command", .type = "string", .description = "The command." } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        auto const command = requireString( params, "command" );
        if ( !command )
        {
          return std::unexpected( command.error() );
        }
        std::string const status = ( *machine )->diskCommand( machine::petsciiTyped( *command ) );
        return Json{ { "status", status } };
      } );

  dispatcher.add(
      "disk.run",
      info( "Runs a program from the SD2PET as a person would: enters the image given, if one is, waits for the "
            "PET's READY. after power-up, types LOAD\"name\",8 (the first file when no name is given), runs the "
            "PET until the load ends, and types RUN, which runs when the PET next runs. Fails with the screen's "
            "error when the load does.",
            { { .name = "path",
                .type = "string",
                .description = "A disk image to enter first; the drive as it is when left out.",
                .required = false },
              { .name = "file",
                .type = "string",
                .description = "The file to load; * (the first) when left out.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        machine::Machine& pet = **machine;
        if ( params.contains( "path" ) )
        {
          auto const path = requireString( params, "path" );
          if ( !path )
          {
            return std::unexpected( path.error() );
          }
          if ( !std::filesystem::is_regular_file( *path ) )
          {
            return std::unexpected( diskError( "not_found", fmt::format( "No file {}", *path ) ) );
          }
          std::string const status = pet.mountDisk( std::filesystem::absolute( *path ) );
          if ( statusCode( status ) >= 20 )
          {
            return std::unexpected( diskError( "mount_failed", status ) );
          }
        }
        std::string file = "*";
        if ( params.contains( "file" ) )
        {
          auto const given = requireString( params, "file" );
          if ( !given )
          {
            return std::unexpected( given.error() );
          }
          file = *given;
        }

        machine::RunResult total{ .reason = machine::StopReason::COMPLETED };
        auto const add = [&total]( machine::RunResult const& run )
        {
          total.cycles += run.cycles;
          total.frames += run.frames;
          total.reason = run.reason;
          total.breakpoint = run.breakpoint;
          total.watchpoint = run.watchpoint;
        };
        if ( pet.frame() < READY_FRAME )
        {
          add( pet.runFrames( READY_FRAME - pet.frame() ) );
        }
        std::string const load = fmt::format( "LOAD\"{}\",8\r", machine::petsciiTyped( file ) );
        std::string const before = pet.drive().status;
        if ( total.reason == machine::StopReason::COMPLETED )
        {
          add( machine::typeKeys( pet, load, LOAD_FRAMES ) );
        }
        std::optional<std::string> ended;
        std::uint64_t const start = pet.frame();
        while ( total.reason == machine::StopReason::COMPLETED && !( ended = loadEnded( pet ) ) )
        {
          // BASIC 2's LOAD reads on for ever when the drive sends nothing,
          // as an SD2PET does when the file cannot be opened: the drive's
          // error, once it has one, ends it, with STOP as a person would.
          if ( std::string const status = pet.drive().status; status != before && statusCode( status ) >= 20 )
          {
            add( stopLoad( pet ) );
            return std::unexpected( diskError( "load_failed", status ) );
          }
          if ( pet.frame() - start >= LOAD_FRAMES )
          {
            total.reason = machine::StopReason::TIMEOUT;
            break;
          }
          add( pet.runFrames( 1 ) );
        }
        if ( !ended )
        {
          Json result = runJson( total );
          result.update( stopJson( total ) );
          return std::unexpected( diskError( total.reason == machine::StopReason::TIMEOUT ? "load_timeout" : "stopped",
                                             fmt::format( "The LOAD did not end: {}", result.dump() ) ) );
        }
        if ( *ended != "READY." )
        {
          return std::unexpected( diskError( "load_failed", *ended ) );
        }
        machine::queueKeys( pet, "RUN\r" );
        Json result = runJson( total );
        result["typed"] = load.substr( 0, load.size() - 1 ) + " RUN";
        result["drive"] = driveJson( pet.drive() );
        return result;
      } );

  dispatcher.add(
      "ieee.trace",
      info( "The IEEE-488 port's changes, counted back from the newest: index 0 is the last change. Each gives its "
            "cycle, the byte on the data lines (a low line is a 1), the control lines low, and which of them the "
            "PET and the SD2PET each pull. The last 65536 changes are kept.",
            { { .name = "count",
                .type = "integer",
                .description = "How many, 0 to 4096; 64 when left out.",
                .required = false },
              { .name = "from",
                .type = "integer",
                .description = "The newest one's index; 0 when left out.",
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
        std::uint64_t const length = ( *machine )->ieeeTraceLength();
        Json events = Json::array();
        for ( std::uint64_t index = from; index < length && index - from < count; ++index )
        {
          machine::IeeeEvent const event = ( *machine )->ieeeTraced( index );
          auto const data = static_cast<std::uint8_t>( ~( event.petData & event.driveData ) );
          events.push_back( Json{ { "index", index },
                                  { "cycle", event.cycle },
                                  { "data", fmt::format( "${:02X}", data ) },
                                  { "low", linesOf( static_cast<std::uint8_t>( event.petLines | event.driveLines ) ) },
                                  { "pet", linesOf( event.petLines ) },
                                  { "drive", linesOf( event.driveLines ) } } );
        }
        return Json{ { "length", length }, { "events", events } };
      } );
}

} // namespace pet::control
