// Ported from the owner's VICE (GPL-2.0-or-later), src/pet/petay.c, by Waldemar
// Pawlaszek. Ported to C++ for PetMcp, 2026.

#include "Ym2149.hpp"

namespace pet::machine
{

namespace
{

/// The YM2149's DAC, 32 levels, normalised (petay.c's ym_dac).
constexpr std::array<double, 32> DAC{ 0.0,
                                      0.0,
                                      0.00465400167849,
                                      0.00772106507973,
                                      0.0109559777218,
                                      0.0139620050355,
                                      0.0169985503929,
                                      0.0200198367285,
                                      0.024368657969,
                                      0.029694056611,
                                      0.0350652323186,
                                      0.0403906309606,
                                      0.0485389486534,
                                      0.0583352407111,
                                      0.0680552376593,
                                      0.0777752346075,
                                      0.0925154497597,
                                      0.111085679408,
                                      0.129747463188,
                                      0.148485542077,
                                      0.17666895552,
                                      0.211551079576,
                                      0.246387426566,
                                      0.281101701381,
                                      0.333730067903,
                                      0.400427252613,
                                      0.467383840696,
                                      0.53443198291,
                                      0.635172045472,
                                      0.75800717174,
                                      0.879926756695,
                                      1.0 };

/// The bits each register has, which the generators use.
constexpr std::array<std::uint8_t, 16> MASK{ 0xff, 0x0f, 0xff, 0x0f, 0xff, 0x0f, 0x1f, 0xff,
                                             0x1f, 0x1f, 0x1f, 0xff, 0xff, 0x0f, 0xff, 0xff };

constexpr unsigned CLOCKS_PER_TICK = 8;

} // namespace

Ym2149::Ym2149()
{
  setEnvelopeShape( 0 );
}

void Ym2149::setEnvelopeShape( std::uint8_t shape )
{
  mEnvelopeAttack = ( shape & 0x04U ) != 0 ? 0x1f : 0;
  if ( ( shape & 0x08U ) == 0 )
  {
    // No continue: one ramp, then hold at zero.
    mEnvelopeHold = true;
    mEnvelopeAlternate = mEnvelopeAttack != 0;
  }
  else
  {
    mEnvelopeHold = ( shape & 0x01U ) != 0;
    mEnvelopeAlternate = ( shape & 0x02U ) != 0;
  }
  mEnvelopeStep = 0x1f;
  mEnvelopeCount = 0;
  mEnvelopeHolding = false;
}

void Ym2149::latchAddress( std::uint8_t value )
{
  // A4-A7 are compared with a mask programmed 0000; anything else deselects
  // the chip until a valid address is latched again.
  mSelected = ( value & 0xf0U ) == 0;
  mLatch = value & 0x0fU;
}

void Ym2149::writeData( std::uint8_t value )
{
  if ( !mSelected )
  {
    return;
  }
  mRegisters.at( mLatch ) = value;
  if ( mLatch == 13 )
  {
    setEnvelopeShape( value & MASK[13] );
  }
}

std::uint8_t Ym2149::readData() const
{
  if ( !mSelected )
  {
    return 0xff;
  }
  if ( mLatch == 14 )
  {
    return portA();
  }
  if ( mLatch == 15 )
  {
    return portB();
  }
  return mRegisters.at( mLatch );
}

std::uint8_t Ym2149::portA() const
{
  // Register 7's bit 6: port A an output.
  return ( mRegisters[7] & 0x40U ) != 0 ? mRegisters[14] : std::uint8_t{ 0xff };
}

std::uint8_t Ym2149::portB() const
{
  return ( mRegisters[7] & 0x80U ) != 0 ? mRegisters[15] : std::uint8_t{ 0xff };
}

unsigned Ym2149::period( unsigned low ) const
{
  unsigned const fine = mRegisters.at( low ) & MASK.at( low );
  unsigned const coarse = mRegisters.at( low + 1 ) & MASK.at( low + 1 );
  unsigned const value = fine | ( coarse << 8U );
  return value == 0 ? 1 : value;
}

void Ym2149::clock( unsigned clocks )
{
  mClocks += clocks;
  while ( mClocks >= CLOCKS_PER_TICK )
  {
    mClocks -= CLOCKS_PER_TICK;
    tick();
    mSum += output();
    ++mTicks;
  }
}

void Ym2149::tick()
{
  for ( unsigned channel = 0; channel < 3; ++channel )
  {
    if ( ++mToneCount.at( channel ) >= period( channel * 2 ) )
    {
      mToneCount.at( channel ) = 0;
      mToneOut.at( channel ) ^= 1U;
    }
  }
  unsigned const noisePeriod = mRegisters[6] & MASK[6];
  if ( ++mNoiseCount >= ( noisePeriod == 0 ? 1 : noisePeriod ) )
  {
    mNoiseCount = 0;
    mNoisePrescale ^= 1U;
    if ( mNoisePrescale == 0 )
    {
      mRng = ( mRng >> 1U ) | ( ( ( mRng ^ ( mRng >> 3U ) ) & 1U ) << 16U );
    }
  }
  if ( ++mEnvelopeCount >= period( 11 ) )
  {
    mEnvelopeCount = 0;
    if ( !mEnvelopeHolding && --mEnvelopeStep < 0 )
    {
      if ( mEnvelopeAlternate )
      {
        mEnvelopeAttack ^= 0x1f;
      }
      if ( mEnvelopeHold )
      {
        mEnvelopeHolding = true;
        mEnvelopeStep = 0;
      }
      else
      {
        mEnvelopeStep = 0x1f;
      }
    }
  }
}

double Ym2149::output() const
{
  unsigned const noise = mRng & 1U;
  std::uint8_t const mixer = mRegisters[7];
  double sum = 0.0;
  for ( unsigned channel = 0; channel < 3; ++channel )
  {
    unsigned const toneOff = ( mixer >> channel ) & 1U;
    unsigned const noiseOff = ( mixer >> ( channel + 3 ) ) & 1U;
    if ( ( ( mToneOut.at( channel ) | toneOff ) & ( noise | noiseOff ) ) != 0 )
    {
      std::uint8_t const amplitude = mRegisters.at( 8 + channel ) & MASK.at( 8 + channel );
      int const level = ( amplitude & 0x10U ) != 0 ? ( mEnvelopeStep ^ mEnvelopeAttack )
                                                   : static_cast<int>( ( ( amplitude & 0x0fU ) * 2 ) + 1 );
      sum += DAC.at( static_cast<std::size_t>( level ) );
    }
  }
  return sum;
}

double Ym2149::takeAverage()
{
  if ( mTicks != 0 )
  {
    mLast = mSum / mTicks;
    mSum = 0.0;
    mTicks = 0;
  }
  return mLast;
}

std::array<std::uint8_t, 16> const& Ym2149::registers() const
{
  return mRegisters;
}

std::uint8_t Ym2149::selected() const
{
  return mLatch;
}

} // namespace pet::machine
