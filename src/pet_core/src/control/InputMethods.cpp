#include "Methods.hpp"

#include "pet/machine/Keyboard.hpp"

#include <spdlog/fmt/fmt.h>

namespace pet::control
{

namespace
{

/// Frames a key is held, and let go, when it is typed: the editor scans the
/// whole keyboard once a frame, and takes a key again only after it has been
/// seen up.
constexpr std::uint64_t HOLD_FRAMES = 2;
constexpr std::uint64_t GAP_FRAMES = 2;

Error invalidInput( std::string message )
{
  return Error{ .code = "invalid_input", .message = std::move( message ) };
}

std::expected<machine::PetKey, Error> keyOf( Json const& params )
{
  auto const name = requireString( params, "key" );
  if ( !name )
  {
    return std::unexpected( name.error() );
  }
  auto const key = machine::keyNamed( *name );
  if ( !key )
  {
    return std::unexpected( invalidInput( fmt::format( "No key {}", *name ) ) );
  }
  return *key;
}

/// A number of frames from `params[name]`, or `fallback`.
std::expected<std::uint64_t, Error> framesOf( Json const& params, std::string_view name, std::uint64_t fallback )
{
  if ( !params.contains( name ) )
  {
    return fallback;
  }
  auto const frames = requireUnsigned( params, name );
  if ( frames && ( *frames == 0 || *frames > 600 ) )
  {
    return std::unexpected( badRequest( fmt::format( "{} is 1 to 600 frames", name ) ) );
  }
  return frames;
}

/// Types `stroke`: held for `hold` frames, then let go for `gap`. The run's
/// cycles and frames are added to `total`.
void type( machine::Machine& machine,
           machine::Keystroke const& stroke,
           std::uint64_t hold,
           std::uint64_t gap,
           machine::RunResult& total )
{
  if ( stroke.shift )
  {
    machine.setKey( machine::LEFT_SHIFT, true );
  }
  machine.setKey( stroke.key, true );
  machine::RunResult const held = machine.runFrames( hold );
  machine.setKey( stroke.key, false );
  if ( stroke.shift )
  {
    machine.setKey( machine::LEFT_SHIFT, false );
  }
  machine::RunResult const released = machine.runFrames( gap );
  total.cycles += held.cycles + released.cycles;
  total.frames += held.frames + released.frames;
}

} // namespace

void addInputMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add(
      "input.type",
      info( "Types a text on the PET's keyboard, a key at a time, held 2 frames and let go 2, running the PET as it "
            "goes. Letters in either case are the unshifted keys, which make capitals in the graphics set; "
            "\\n is RETURN. In braces: {RETURN}, {CLR}, {HOME}, {DEL}, {INST}, {UP}, {DOWN}, {LEFT}, {RIGHT}, "
            "{RVS}, {OFF}, {STOP}, {RUN}, {PI}, or a key with SHIFT, {SHIFT-A}.",
            { { .name = "text", .type = "string", .description = "What to type." },
              { .name = "hold_frames",
                .type = "integer",
                .description = "Frames each key is held; 2 when left out.",
                .required = false },
              { .name = "gap_frames",
                .type = "integer",
                .description = "Frames each key is let go before the next; 2 when left out.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        auto const text = requireString( params, "text" );
        if ( !text )
        {
          return std::unexpected( text.error() );
        }
        auto const strokes = machine::keystrokesFor( *text );
        if ( !strokes )
        {
          return std::unexpected( invalidInput( strokes.error() ) );
        }
        auto const hold = framesOf( params, "hold_frames", HOLD_FRAMES );
        auto const gap = framesOf( params, "gap_frames", GAP_FRAMES );
        if ( !hold || !gap )
        {
          return std::unexpected( !hold ? hold.error() : gap.error() );
        }
        machine::RunResult total{ .reason = machine::StopReason::COMPLETED, .cycles = 0, .frames = 0 };
        for ( machine::Keystroke const& stroke : *strokes )
        {
          type( **machine, stroke, *hold, *gap, total );
        }
        Json result = runJson( total );
        result["keystrokes"] = strokes->size();
        return result;
      } );

  dispatcher.add(
      "input.press",
      info( "Presses a PET key for some frames and lets it go, running the PET as it goes.",
            { { .name = "key",
                .type = "string",
                .description = "The key's name: a character on it (A, 7, \") or RETURN, SPACE, LEFT_SHIFT, "
                               "RIGHT_SHIFT, RVS, STOP, HOME, DEL, CRSR_DOWN, CRSR_RIGHT, LEFT_ARROW, UP_ARROW." },
              { .name = "shift",
                .type = "boolean",
                .description = "Hold the left SHIFT with it; false when left out.",
                .required = false },
              { .name = "frames",
                .type = "integer",
                .description = "Frames to hold it; 2 when left out. It is then let go for 2.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        auto const key = keyOf( params );
        if ( !key )
        {
          return std::unexpected( key.error() );
        }
        if ( params.contains( "shift" ) && !params.at( "shift" ).is_boolean() )
        {
          return std::unexpected( badRequest( "shift is true or false" ) );
        }
        auto const hold = framesOf( params, "frames", HOLD_FRAMES );
        if ( !hold )
        {
          return std::unexpected( hold.error() );
        }
        machine::RunResult total{ .reason = machine::StopReason::COMPLETED, .cycles = 0, .frames = 0 };
        type( **machine,
              machine::Keystroke{ .key = *key, .shift = params.value( "shift", false ) },
              *hold,
              GAP_FRAMES,
              total );
        return runJson( total );
      } );

  dispatcher.add(
      "input.set",
      info( "Holds a PET key down, or lets it go, until told otherwise. The PET does not run.",
            { { .name = "key", .type = "string", .description = "The key's name, as input.press takes it." },
              { .name = "down", .type = "boolean", .description = "Down, or up." } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        auto const key = keyOf( params );
        if ( !key )
        {
          return std::unexpected( key.error() );
        }
        if ( !params.contains( "down" ) || !params.at( "down" ).is_boolean() )
        {
          return std::unexpected( badRequest( "Missing or invalid field: down" ) );
        }
        ( *machine )->setKey( *key, params.at( "down" ).get<bool>() );
        return Json::object();
      } );

  dispatcher.add( "input.clear",
                  info( "Lets go every key input.set holds." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    ( *machine )->releaseKeys();
                    return Json::object();
                  } );

  dispatcher.add( "input.get_state",
                  info( "The keys input.set holds, by name, and the matrix as rows of bits, a set bit a key down." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    auto const machine = machineOf( emulator );
                    if ( !machine )
                    {
                      return std::unexpected( machine.error() );
                    }
                    machine::KeyMatrix const keys = ( *machine )->heldKeys();
                    Json held = Json::array();
                    Json rows = Json::array();
                    for ( unsigned row = 0; row < machine::KEYBOARD_ROWS; ++row )
                    {
                      rows.push_back( keys.at( row ) );
                      for ( unsigned column = 0; column < machine::KEYBOARD_COLUMNS; ++column )
                      {
                        if ( ( keys.at( row ) & ( 1U << column ) ) != 0 )
                        {
                          held.push_back(
                              machine::nameOf( machine::PetKey{ .row = static_cast<std::uint8_t>( row ),
                                                                .column = static_cast<std::uint8_t>( column ) } ) );
                        }
                      }
                    }
                    return Json{ { "held", std::move( held ) }, { "matrix", std::move( rows ) } };
                  } );
}

} // namespace pet::control
