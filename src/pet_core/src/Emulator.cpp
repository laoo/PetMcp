#include "pet/Emulator.hpp"

#include <utility>

namespace pet
{

Emulator::Emulator( Settings settings ) : mSettings{ std::move( settings ) }
{
  if ( auto const powered = powerUp(); !powered )
  {
    mPowerUpError = powered.error();
  }
}

std::expected<void, std::string> Emulator::powerUp()
{
  if ( mSettings.romDirectory.empty() )
  {
    return std::unexpected( std::string{ "no ROM directory is given" } );
  }
  auto roms = machine::Roms::load( mSettings.romDirectory );
  if ( !roms )
  {
    return std::unexpected( roms.error() );
  }
  mMachine = std::make_unique<machine::Machine>( std::move( *roms ) );
  mPowerUpError.clear();
  return {};
}

machine::Machine* Emulator::machine()
{
  return mMachine.get();
}

machine::Machine const* Emulator::machine() const
{
  return mMachine.get();
}

std::string const& Emulator::powerUpError() const
{
  return mPowerUpError;
}

Settings const& Emulator::settings() const
{
  return mSettings;
}

bool Emulator::paused() const
{
  return mPaused;
}

void Emulator::setPaused( bool paused )
{
  mPaused = paused;
}

} // namespace pet
