#include "Sound.hpp"

#include "ExpansionBoard.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace pet::machine
{

namespace
{

constexpr double CYCLES_PER_SECOND = 1'000'000.0;

/// calculate_alpha in petsound.c: an RC low-pass's coefficient.
double alphaOf( double sampleRate, double limit )
{
  double const deltaT = 1.0 / sampleRate;
  double const tau = 1.0 / ( 2.0 * std::numbers::pi * limit );
  return deltaT / ( deltaT + tau );
}

/// What a high CB2 swings: MAX_SAMPLE, 4095, of the 16-bit range.
constexpr double CB2_GAIN = 4096.0 / 32768.0;

/// What one chip at full volume on all three channels swings (petay.c).
constexpr double AY_GAIN = 16000.0 / 32767.0;

std::int16_t toSample( double level )
{
  return static_cast<std::int16_t>( std::lround( std::clamp( level, -1.0, 1.0 ) * 32767.0 ) );
}

} // namespace

// CB2's low-pass at the CPU's rate, its knee at VICE's default CB2Lowpass, and
// the high-pass that keeps clicks away at the output's rate; the board's
// coupling capacitors, a DC blocker at 10 Hz.
Sound::Sound()
    : mCb2LowpassAlpha{ alphaOf( CYCLES_PER_SECOND, 8000.0 ) }, mCb2HighpassAlpha{ alphaOf( RATE, 160.0 ) },
      mAyHighpass{ std::exp( -2.0 * std::numbers::pi * 10.0 / RATE ) }
{
}

void Sound::setCb2( bool level )
{
  // petsound_store_manual: ignored while the sound is off.
  if ( mActive )
  {
    mCb2 = level;
  }
}

void Sound::setActive( bool active )
{
  mActive = active;
  if ( !mActive )
  {
    mCb2 = false;
  }
}

void Sound::cycle( ExpansionBoard* board )
{
  mLowpass += mCb2LowpassAlpha * ( ( mCb2 ? 1.0 : 0.0 ) - mLowpass );
  mPhase += RATE;
  if ( mPhase < CYCLES_PER_SECOND )
  {
    return;
  }
  mPhase -= static_cast<unsigned>( CYCLES_PER_SECOND );

  mHighpass += mCb2HighpassAlpha * ( mLowpass - mHighpass );
  double const cb2 = ( mLowpass - mHighpass ) * CB2_GAIN;
  std::array<double, 2> ay{};
  if ( board != nullptr )
  {
    for ( unsigned chip = 0; chip < 2; ++chip )
    {
      double const in = board->chip( chip ).takeAverage() / 3.0;
      mAyOut.at( chip ) = mAyHighpass * ( mAyOut.at( chip ) + in - mAyIn.at( chip ) );
      mAyIn.at( chip ) = in;
      ay.at( chip ) = mAyOut.at( chip ) * AY_GAIN;
    }
  }
  mFrames.push_back( AudioFrame{ .left = toSample( cb2 + ay[0] ), .right = toSample( cb2 + ay[1] ) } );
}

std::vector<AudioFrame>& Sound::frames()
{
  return mFrames;
}

} // namespace pet::machine
