#include "Methods.hpp"

#include "pet/Version.hpp"

namespace pet::control
{

void addEmuMethods( Dispatcher& dispatcher, Emulator& emulator )
{
  dispatcher.add( "emu.status",
                  info( "The emulator's version, and whether the window's own run is paused." ),
                  [&emulator]( Json const& /*params*/ ) -> Outcome
                  { return Json{ { "version", versionString() }, { "paused", emulator.paused() } }; } );

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
}

} // namespace pet::control
