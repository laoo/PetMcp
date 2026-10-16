#include "pet/Emulator.hpp"

#include <utility>

namespace pet
{

Emulator::Emulator( Settings settings ) : mSettings{ std::move( settings ) } {}

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
