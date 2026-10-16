// Ported from VICE's src/piacore.c (GPL-2.0-or-later): Copyright (C) Jouko Valta, Andre Fachat
// and the VICE Team (VICE's README). Ported to C++ for PetMcp, 2026.

#include "Pia6520.hpp"

namespace pet::machine
{

namespace
{

constexpr std::uint8_t PORT_A = 0;
constexpr std::uint8_t CONTROL_A = 1;
constexpr std::uint8_t PORT_B = 2;
constexpr std::uint8_t CONTROL_B = 3;

bool isHandshake( std::uint8_t control )
{
  return ( control & 0x30U ) == 0x20;
}

bool isPulseMode( std::uint8_t control )
{
  return ( control & 0x38U ) == 0x28;
}

bool isToggleMode( std::uint8_t control )
{
  return ( control & 0x38U ) == 0x20;
}

} // namespace

Pia6520::Pia6520( PiaPorts& ports ) : mPorts{ ports } {}

void Pia6520::reset( Clock now )
{
  mControlA = 0;
  mControlB = 0;
  mDdrA = 0;
  mDdrB = 0;
  // Nothing to read from the keyboard.
  mPortA = 0xff;
  mPortB = 0xff;
  mPorts.setCa2( true );
  mCaState = true;
  mPorts.setCb2( true );
  mCbState = true;
  mIrq.set( false, now );
}

// mypia_update_irq: a flag whose interrupt is enabled pulls IRQ.
void Pia6520::updateIrq( Clock from )
{
  bool const active = ( mControlA & 0x81U ) == 0x81 || ( mControlA & 0x68U ) == 0x48 || ( mControlB & 0x81U ) == 0x81 ||
                      ( mControlB & 0x68U ) == 0x48;
  mIrq.set( active, from );
}

bool Pia6520::irq( Clock clock ) const
{
  return mIrq.at( clock );
}

void Pia6520::signal( Line line, bool high, Clock now )
{
  // Bit 1 of the control register is the active edge: rising when set.
  if ( line == Line::CA1 )
  {
    if ( ( ( mControlA & 0x02U ) != 0 ) == high )
    {
      mControlA |= 0x80U;
      updateIrq( now );
      if ( isToggleMode( mControlA ) )
      {
        mPorts.setCa2( true );
        mCaState = true;
      }
    }
    return;
  }
  if ( ( ( mControlB & 0x02U ) != 0 ) == high )
  {
    mControlB |= 0x80U;
    updateIrq( now );
    if ( isToggleMode( mControlB ) )
    {
      mPorts.setCb2( true );
      mCbState = true;
    }
  }
}

void Pia6520::store( std::uint16_t address, std::uint8_t byte, Clock now )
{
  // VICE's PIA takes the clock of a store as the CPU core holds it during the
  // store, a cycle past the access, and does not correct it as the VIA does:
  // an interrupt a write changes changes a cycle later than one a read does.
  Clock const at = now + 1;
  switch ( address & 3U )
  {
  case PORT_A:
    if ( ( mControlA & 0x04U ) != 0 )
    {
      mPortA = byte;
    }
    else
    {
      mDdrA = byte;
    }
    mPorts.storePa( static_cast<std::uint8_t>( mPortA | ~mDdrA ) );
    break;

  case PORT_B:
    if ( ( mControlB & 0x04U ) != 0 )
    {
      mPortB = byte;
    }
    else
    {
      mDdrB = byte;
    }
    mPorts.storePb( static_cast<std::uint8_t>( mPortB | ~mDdrB ) );
    if ( isHandshake( mControlB ) )
    {
      mPorts.setCb2( false );
      mCbState = false;
      if ( isPulseMode( mControlB ) )
      {
        mPorts.setCb2( true );
        mCbState = true;
      }
    }
    break;

  case CONTROL_A:
    if ( ( byte & 0x38U ) == 0x30 )
    {
      mPorts.setCa2( false );
      mCaState = false;
    }
    else if ( ( byte & 0x38U ) == 0x38 || ( mControlA & 0x30U ) == 0x30 )
    {
      // A high output, or a manual output changed to toggle or pulse.
      mPorts.setCa2( true );
      mCaState = true;
    }
    mControlA = static_cast<std::uint8_t>( ( mControlA & 0xc0U ) | ( byte & 0x3fU ) );
    if ( ( mControlA & 0x20U ) != 0 )
    {
      // CA2 as an output has no flag.
      mControlA &= 0xbfU;
    }
    updateIrq( at );
    break;

  case CONTROL_B:
    if ( ( byte & 0x38U ) == 0x30 )
    {
      mPorts.setCb2( false );
      mCbState = false;
    }
    else if ( ( byte & 0x38U ) == 0x38 || ( mControlB & 0x30U ) == 0x30 )
    {
      mPorts.setCb2( true );
      mCbState = true;
    }
    mControlB = static_cast<std::uint8_t>( ( mControlB & 0xc0U ) | ( byte & 0x3fU ) );
    if ( ( mControlB & 0x20U ) != 0 )
    {
      mControlB &= 0xbfU;
    }
    updateIrq( at );
    break;

  default:
    break;
  }
}

std::uint8_t Pia6520::readRegister( std::uint16_t address, bool peek, Clock now )
{
  switch ( address & 3U )
  {
  case PORT_A:
    if ( ( mControlA & 0x04U ) == 0 )
    {
      return mDdrA;
    }
    if ( !peek )
    {
      // Reading the port clears CA1's and CA2's flags.
      mControlA &= 0x3fU;
      updateIrq( now );
    }
    return static_cast<std::uint8_t>( ( mPorts.readPa() & ~mDdrA ) | ( mPortA & mDdrA ) );

  case PORT_B:
    if ( ( mControlB & 0x04U ) == 0 )
    {
      return mDdrB;
    }
    if ( !peek )
    {
      mControlB &= 0x3fU;
      updateIrq( now );
    }
    return static_cast<std::uint8_t>( ( mPorts.readPb() & ~mDdrB ) | ( mPortB & mDdrB ) );

  case CONTROL_A:
    return mControlA;

  default:
    return mControlB;
  }
}

std::uint8_t Pia6520::read( std::uint16_t address, Clock now )
{
  return readRegister( address, false, now );
}

std::uint8_t Pia6520::peek( std::uint16_t address )
{
  return readRegister( address, true, 0 );
}

Pia6520::Snapshot Pia6520::snapshot() const
{
  return Snapshot{ .portA = mPortA,
                   .ddrA = mDdrA,
                   .controlA = mControlA,
                   .portB = mPortB,
                   .ddrB = mDdrB,
                   .controlB = mControlB,
                   .ca2 = mCaState,
                   .cb2 = mCbState };
}

} // namespace pet::machine
