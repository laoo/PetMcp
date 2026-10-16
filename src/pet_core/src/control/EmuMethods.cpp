#include "Methods.hpp"

#include "pet/Version.hpp"

namespace pet::control
{

void addEmuMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add(
      "emu.status",
      info( "The emulator's version, whether the window's own run is paused, and the PET's time: cycles of its 1 "
            "MHz clock and frames since power-up; or, when it is not running, why." ),
      [&emulator]( Json const& /*params*/ ) -> Outcome
      {
        Json status{ { "version", versionString() }, { "paused", emulator.paused() } };
        if ( machine::Machine const* machine = emulator.machine() )
        {
          status["running"] = true;
          status["cycles"] = machine->now();
          status["frame"] = machine->frame();
        }
        else
        {
          status["running"] = false;
          status["error"] = emulator.powerUpError();
        }
        return status;
      } );

  dispatcher.add( "emu.pause",
                  info( "Pauses the PET where it runs by itself: in the desktop application, which then runs it only "
                        "on request. petmcp-cli runs only on request anyway." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    emulator.setPaused( true );
                    return Json::object();
                  } );

  dispatcher.add( "emu.resume",
                  info( "Lets the PET run by itself again, where it does: in the desktop application." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  {
                    emulator.setPaused( false );
                    return Json::object();
                  } );

  dispatcher.add(
      "emu.reset",
      info( "Pulls the PET's reset line: the 6502, the PIAs and the VIA reset, RAM and the video run on. With power, "
            "switches it off and on instead: RAM as at power-up, the ROMs read again.",
            { { .name = "power",
                .type = "boolean",
                .description = "Switch off and on rather than reset; false when left out.",
                .required = false } } ),
      [&emulator]( Json const& params ) -> Outcome
      {
        if ( params.contains( "power" ) && !params.at( "power" ).is_boolean() )
        {
          return std::unexpected( badRequest( "power is true or false" ) );
        }
        if ( params.value( "power", false ) )
        {
          if ( auto const powered = emulator.powerUp(); !powered )
          {
            return std::unexpected( Error{ .code = "power_up_failed", .message = powered.error() } );
          }
          return Json::object();
        }
        auto const machine = machineOf( emulator );
        if ( !machine )
        {
          return std::unexpected( machine.error() );
        }
        ( *machine )->reset();
        return Json::object();
      } );
}

} // namespace pet::control
