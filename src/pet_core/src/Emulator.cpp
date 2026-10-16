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
  mMachine = std::make_unique<machine::Machine>( std::move( *roms ), mSettings.expansionBoard );
  mMachine->setTraceCycles( mTraceCycles );
  if ( !mSettings.cardDirectory.empty() )
  {
    mMachine->setCard( mSettings.cardDirectory );
  }
  ++mPowerUps;
  mPowerUpError.clear();
  return {};
}

std::expected<void, std::string> Emulator::setRomDirectory( std::filesystem::path const& directory )
{
  std::filesystem::path const previous = std::exchange( mSettings.romDirectory, directory );
  auto powered = powerUp();
  if ( !powered )
  {
    mSettings.romDirectory = previous;
  }
  return powered;
}

void Emulator::setCardDirectory( std::filesystem::path const& directory )
{
  mSettings.cardDirectory = directory;
  if ( mMachine )
  {
    mMachine->setCard( directory );
  }
}

machine::Machine* Emulator::machine()
{
  return mMachine.get();
}

machine::Machine const* Emulator::machine() const
{
  return mMachine.get();
}

void Emulator::setTraceCycles( std::uint64_t cycles )
{
  mTraceCycles = cycles;
  if ( mMachine )
  {
    mMachine->setTraceCycles( cycles );
  }
}

std::uint64_t Emulator::traceCycles() const
{
  return mTraceCycles;
}

std::uint64_t Emulator::powerUps() const
{
  return mPowerUps;
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
