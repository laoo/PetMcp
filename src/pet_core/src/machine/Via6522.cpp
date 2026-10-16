#include "Via6522.hpp"

namespace pet::machine
{

namespace
{

constexpr std::uint8_t IM_IRQ = 0x80;
constexpr std::uint8_t IM_T1 = 0x40;
constexpr std::uint8_t IM_T2 = 0x20;
constexpr std::uint8_t IM_CB1 = 0x10;
constexpr std::uint8_t IM_CB2 = 0x08;
constexpr std::uint8_t IM_SR = 0x04;
constexpr std::uint8_t IM_CA1 = 0x02;
constexpr std::uint8_t IM_CA2 = 0x01;

constexpr std::uint8_t ACR_T1_PB7_USED = 0x80;
constexpr std::uint8_t ACR_T1_FREE_RUN = 0x40;
constexpr std::uint8_t ACR_T2_CONTROL = 0x20;
constexpr std::uint8_t ACR_T2_COUNTPB6 = 0x20;
constexpr std::uint8_t ACR_SR_CONTROL = 0x1c;
constexpr std::uint8_t ACR_SR_OUT = 0x10;
constexpr std::uint8_t ACR_SR_DISABLED = 0x00;
constexpr std::uint8_t ACR_SR_IN_T2 = 0x04;
constexpr std::uint8_t ACR_SR_IN_PHI2 = 0x08;
constexpr std::uint8_t ACR_SR_IN_CB1 = 0x0c;
constexpr std::uint8_t ACR_SR_OUT_FREE_T2 = 0x10;
constexpr std::uint8_t ACR_SR_OUT_T2 = 0x14;
constexpr std::uint8_t ACR_SR_OUT_PHI2 = 0x18;
constexpr std::uint8_t ACR_SR_OUT_CB1 = 0x1c;

constexpr std::uint8_t PCR_CB2_CONTROL = 0xe0;
constexpr std::uint8_t PCR_CB2_I_OR_O = 0x80;
constexpr std::uint8_t PCR_CB2_INPUT = 0x00;
constexpr std::uint8_t PCR_CB2_INPUT_POS_ACTIVE_EDGE = 0x40;
constexpr std::uint8_t PCR_CB2_LOW_OUTPUT = 0xc0;
constexpr std::uint8_t PCR_CB1_CONTROL = 0x10;
constexpr std::uint8_t PCR_CB1_POS_ACTIVE_EDGE = 0x10;
constexpr std::uint8_t PCR_CA2_CONTROL = 0x0e;
constexpr std::uint8_t PCR_CA2_I_OR_O = 0x08;
constexpr std::uint8_t PCR_CA2_INPUT = 0x00;
constexpr std::uint8_t PCR_CA2_LOW_OUTPUT = 0x0c;
constexpr std::uint8_t PCR_CA1_CONTROL = 0x01;

/// A full T1 cycle is the latch plus 2: N, N-1, ..., 1, 0, FFFF.
constexpr unsigned FULL_CYCLE_2 = 2;

constexpr unsigned SR_PHI2_FIRST_OFFSET = 3;
constexpr unsigned SR_PHI2_NEXT_OFFSET = 1;

constexpr std::uint8_t START_SHIFTING = 0;
constexpr std::uint8_t FINISHED_SHIFTING = 16;

bool isSrT2Controlled( std::uint8_t acr )
{
  return ( acr & 0x0cU ) == 0x04 || ( acr & 0x1cU ) == 0x10;
}

bool isT2Timer( std::uint8_t acr )
{
  return ( acr & ACR_T2_CONTROL ) == 0;
}

} // namespace

Via6522::Via6522( ViaPorts& ports ) : mPorts{ ports }
{
  // Timers and their latches do not hold 0 at power-up; a VIC-20's read so.
  mVia[T1CL] = mVia[T1LL] = 0xff;
  mVia[T1CH] = mVia[T1LH] = 223;
  mVia[T2CL] = 0xff;
  mVia[T2CH] = 0xff;
}

bool Via6522::isCa2Handshake() const
{
  return ( mVia[PCR] & 0x0cU ) == 0x08;
}

bool Via6522::isCa2PulseMode() const
{
  return ( mVia[PCR] & 0x0eU ) == 0x0a;
}

bool Via6522::isCa2ToggleMode() const
{
  return ( mVia[PCR] & 0x0eU ) == 0x08;
}

bool Via6522::isCa2IndInput() const
{
  return ( mVia[PCR] & 0x0aU ) == 0x02;
}

bool Via6522::isCb2Handshake() const
{
  return ( mVia[PCR] & 0xc0U ) == 0x80;
}

bool Via6522::isCb2PulseMode() const
{
  return ( mVia[PCR] & 0xe0U ) == 0xa0;
}

bool Via6522::isCb2ToggleMode() const
{
  return ( mVia[PCR] & 0xe0U ) == 0x80;
}

bool Via6522::isSrFreeRunning() const
{
  return ( mVia[ACR] & 0x1cU ) == 0x10;
}

void Via6522::updateIrq( Clock rclk )
{
  mIrq.set( ( mIfr & mIer & 0x7fU ) != 0, rclk );
}

bool Via6522::irq( Clock clock ) const
{
  return mIrq.at( clock );
}

// viacore_t1: until the reload, the clock's distance to it; after it, the
// latch less the part of the current full cycle gone.
std::uint16_t Via6522::t1( Clock rclk ) const
{
  if ( rclk < mT1reload )
  {
    return static_cast<std::uint16_t>( mT1reload - rclk - FULL_CYCLE_2 );
  }
  unsigned const fullCycle = mTal + FULL_CYCLE_2;
  Clock const timePastLastReload = rclk - mT1reload;
  auto const partialCycle = static_cast<unsigned>( timePastLastReload % fullCycle );
  return static_cast<std::uint16_t>( mTal - partialCycle );
}

// viacore_t2.
std::uint16_t Via6522::t2( Clock rclk ) const
{
  if ( ( mVia[ACR] & ACR_T2_COUNTPB6 ) != 0 )
  {
    return static_cast<std::uint16_t>( ( static_cast<unsigned>( mT2ch ) << 8U ) | mT2cl );
  }
  auto value = static_cast<std::uint16_t>( mT2zero - rclk );
  if ( mT2xx00 )
  {
    value = static_cast<std::uint16_t>( ( static_cast<unsigned>( mT2ch ) << 8U ) | ( value & 0xffU ) );
  }
  return value;
}

// update_via_t1_latch: the new latch only reaches the counter at its next
// reload, so the reload is moved past the present first.
void Via6522::updateT1Latch( Clock rclk )
{
  if ( rclk >= mT1reload )
  {
    unsigned const fullCycle = mTal + FULL_CYCLE_2;
    Clock const timePastLastReload = rclk - mT1reload;
    Clock const cycles = 1 + ( timePastLastReload / fullCycle );
    mT1reload += cycles * fullCycle;
  }
  mTal = mVia[T1LL] + ( static_cast<unsigned>( mVia[T1LH] ) << 8U );
}

void Via6522::reset( Clock now )
{
  for ( std::size_t i = 0; i < 4; ++i )
  {
    mVia.at( i ) = 0;
  }
  // The shift register (10) is kept.
  for ( std::size_t i = 11; i < 16; ++i )
  {
    mVia.at( i ) = 0;
  }
  mTal = 0xffff;
  mT2cl = 0xff;
  mT2ch = 0xff;
  mT1reload = now;
  mT2zero = now;
  mIer = 0;
  mIfr = 0;
  mT1pb7 = 0x80;
  mShiftState = FINISHED_SHIFTING;
  mT2IrqAllowed = false;
  mT1zero = 0;
  mT2xx00 = false;
  mT1ZeroAlarm.unset();
  mT2ZeroAlarm.unset();
  mT2UnderflowAlarm.unset();
  mT2ShiftAlarm.unset();
  mPhi2SrAlarm.unset();
  updateIrq( now );
  mOldpa = 0;
  mOldpb = 0;
  mCa2OutState = true;
  mCb1OutState = true;
  mCb2OutState = true;
  mPorts.setCa2( mCa2OutState );
  mPorts.setCb2( mCb2OutState, now );
  cacheCb12IoStatus();
}

void Via6522::signal( Line line, bool high, Clock now )
{
  switch ( line )
  {
  case Line::CA1:
    if ( ( high ? 1U : 0U ) == ( mVia[PCR] & PCR_CA1_CONTROL ) )
    {
      if ( isCa2ToggleMode() && !mCa2OutState )
      {
        mCa2OutState = true;
        mPorts.setCa2( mCa2OutState );
      }
      mIfr |= IM_CA1;
      updateIrq( now );
    }
    break;
  case Line::CA2:
    if ( ( mVia[PCR] & PCR_CA2_I_OR_O ) == PCR_CA2_INPUT )
    {
      // The PCR's bit 2 is the active edge.
      if ( ( ( ( ( high ? 1U : 0U ) << 2U ) ^ mVia[PCR] ) & 0x04U ) == 0 )
      {
        mIfr |= IM_CA2;
      }
      updateIrq( now );
    }
    break;
  case Line::CB1:
    setCb1( high, now );
    break;
  case Line::CB2:
    setCb2( high, now );
    break;
  }
}

// schedule_t2_zero_alarm: t2cl and t2ch must hold T2 as of rclk.
void Via6522::scheduleT2ZeroAlarm( Clock rclk )
{
  mT2zero = rclk + mT2cl;
  mT2xx00 = true;
  mT2UnderflowAlarm.unset();
  mT2ZeroAlarm.schedule( mT2zero );
}

// setup_shifting.
void Via6522::setupShifting( Clock rclk )
{
  switch ( mVia[ACR] & ACR_SR_CONTROL )
  {
  case ACR_SR_DISABLED:
    // Writing SR while disabled does not start the shift counter.
    break;
  case ACR_SR_IN_T2:
  case ACR_SR_OUT_T2:
  case ACR_SR_IN_CB1:
  case ACR_SR_OUT_CB1:
    if ( mShiftState == FINISHED_SHIFTING )
    {
      mShiftState = START_SHIFTING;
    }
    break;
  case ACR_SR_IN_PHI2:
  case ACR_SR_OUT_PHI2:
    if ( mShiftState == FINISHED_SHIFTING )
    {
      mShiftState = START_SHIFTING;
      // Not in the cycle shifting starts in.
      mPhi2SrAlarm.schedule( rclk + 1 );
    }
    break;
  case ACR_SR_OUT_FREE_T2:
    // Below 16, keeping whether it is even.
    mShiftState &= 0x0fU;
    break;
  default:
    break;
  }
}

void Via6522::store( std::uint16_t address, std::uint8_t byte, Clock now )
{
  // VICE's rclk for a store is the clock less its write offset: the cycle of
  // the access, which is `now` here.
  Clock const rclk = now;
  auto addr = static_cast<std::uint8_t>( address & 0x0fU );

  switch ( addr )
  {
  case PRA:
    mIfr &= static_cast<std::uint8_t>( ~IM_CA1 );
    if ( !isCa2IndInput() )
    {
      mIfr &= static_cast<std::uint8_t>( ~IM_CA2 );
    }
    if ( isCa2Handshake() )
    {
      mCa2OutState = false;
      mPorts.setCa2( mCa2OutState );
      if ( isCa2PulseMode() )
      {
        // A cycle too soon, as in VICE.
        mCa2OutState = true;
        mPorts.setCa2( mCa2OutState );
      }
    }
    if ( ( mIer & ( IM_CA1 | IM_CA2 ) ) != 0 )
    {
      updateIrq( rclk );
    }
    [[fallthrough]];
  case PRA_NHS:
    mVia[PRA_NHS] = byte;
    addr = PRA;
    [[fallthrough]];
  case DDRA:
    mVia.at( addr ) = byte;
    byte = static_cast<std::uint8_t>( mVia[PRA] | ~mVia[DDRA] );
    mPorts.storePra( byte );
    mOldpa = byte;
    break;

  case PRB:
    mIfr &= static_cast<std::uint8_t>( ~IM_CB1 );
    if ( ( mVia[PCR] & 0xa0U ) != 0x20 )
    {
      mIfr &= static_cast<std::uint8_t>( ~IM_CB2 );
    }
    if ( isCb2Handshake() )
    {
      mCb2OutState = false;
      mPorts.setCb2( mCb2OutState, rclk );
      if ( isCb2PulseMode() )
      {
        mCb2OutState = true;
        mPorts.setCb2( mCb2OutState, rclk );
      }
    }
    if ( ( mIer & ( IM_CB1 | IM_CB2 ) ) != 0 )
    {
      updateIrq( rclk );
    }
    [[fallthrough]];
  case DDRB:
    mVia.at( addr ) = byte;
    byte = static_cast<std::uint8_t>( mVia[PRB] | ~mVia[DDRB] );
    if ( ( mVia[ACR] & ACR_T1_PB7_USED ) != 0 )
    {
      byte = static_cast<std::uint8_t>( ( byte & 0x7fU ) | mT1pb7 );
    }
    mPorts.storePrb( byte );
    mOldpb = byte;
    break;

  case SR:
    mVia[SR] = byte;
    setupShifting( rclk );
    if ( ( mIfr & IM_SR ) != 0 )
    {
      mIfr &= static_cast<std::uint8_t>( ~IM_SR );
      updateIrq( rclk );
    }
    break;

  case T1CL:
  case T1LL:
    mVia[T1LL] = byte;
    updateT1Latch( rclk );
    break;

  case T1CH:
    mVia[T1LH] = byte;
    updateT1Latch( rclk );
    // The counter shows the latch from the next cycle; each write allows one
    // interrupt.
    mT1reload = rclk + 1 + mTal + FULL_CYCLE_2;
    mT1zero = rclk + 1 + mTal;
    mT1ZeroAlarm.schedule( mT1zero );
    mT1pb7 = 0;
    mIfr &= static_cast<std::uint8_t>( ~IM_T1 );
    updateIrq( rclk );
    break;

  case T1LH:
    mVia[T1LH] = byte;
    updateT1Latch( rclk );
    // Against the Synertek notes, writing the high latch clears the T1 flag
    // too, as VICE found on the hardware (via_t1irqack).
    mIfr &= static_cast<std::uint8_t>( ~IM_T1 );
    updateIrq( rclk );
    break;

  case T2CL:
    // T2's low latch; it reaches the counter at the next underflow.
    mVia[T2CL] = byte;
    mPorts.storeT2l( byte );
    break;

  case T2CH:
    mVia[T2CH] = byte;
    mT2cl = mVia[T2CL];
    mT2ch = byte;
    if ( ( mVia[ACR] & ACR_T2_COUNTPB6 ) == 0 )
    {
      // Starting the countdown takes a cycle: the start value is seen in the
      // next.
      scheduleT2ZeroAlarm( rclk + 1 );
    }
    mIfr &= static_cast<std::uint8_t>( ~IM_T2 );
    updateIrq( rclk );
    mT2IrqAllowed = true;
    break;

  case IFR:
    mIfr &= static_cast<std::uint8_t>( ~byte );
    updateIrq( rclk );
    break;

  case IER:
    if ( ( byte & IM_IRQ ) != 0 )
    {
      mIer |= static_cast<std::uint8_t>( byte & 0x7fU );
    }
    else
    {
      mIer &= static_cast<std::uint8_t>( ~byte );
    }
    updateIrq( rclk );
    break;

  case ACR:
  {
    if ( ( ( mVia[ACR] ^ byte ) & ACR_T1_PB7_USED ) != 0 && ( byte & ACR_T1_PB7_USED ) != 0 )
    {
      mT1pb7 = 0x80;
    }
    unsigned t2StartupDelay = 0;
    bool restartT2Alarms = false;
    if ( ( ( mVia[ACR] ^ byte ) & ACR_T2_CONTROL ) != 0 )
    {
      if ( ( byte & ACR_T2_COUNTPB6 ) != 0 )
      {
        // Stopping takes a cycle too: it counts one further.
        auto const stop = static_cast<std::uint16_t>( t2( rclk ) - 1U );
        mT2cl = static_cast<std::uint8_t>( stop & 0xffU );
        mT2ch = static_cast<std::uint8_t>( stop >> 8U );
        mT2ZeroAlarm.unset();
        mT2xx00 = false;
      }
      else
      {
        restartT2Alarms = true;
        ++t2StartupDelay;
      }
    }

    switch ( byte & ACR_SR_CONTROL )
    {
    case ACR_SR_DISABLED:
      mPhi2SrAlarm.unset();
      // The SR flag is held at 0 in this mode.
      if ( ( mIfr & IM_SR ) != 0 )
      {
        mIfr &= static_cast<std::uint8_t>( ~IM_SR );
        updateIrq( rclk );
      }
      setCb2OutputState( mVia[PCR], rclk );
      break;
    case ACR_SR_IN_T2:
    case ACR_SR_OUT_T2:
    case ACR_SR_OUT_FREE_T2:
      mPhi2SrAlarm.unset();
      // 8-bit mode needs the zero alarm, which may have stopped at 0000.
      restartT2Alarms = restartT2Alarms || ( !isSrT2Controlled( mVia[ACR] ) && isT2Timer( byte ) );
      break;
    case ACR_SR_IN_PHI2:
    case ACR_SR_OUT_PHI2:
      if ( !mPhi2SrAlarm.set )
      {
        mPhi2SrAlarm.schedule( rclk + SR_PHI2_FIRST_OFFSET );
      }
      break;
    case ACR_SR_IN_CB1:
    case ACR_SR_OUT_CB1:
      mPhi2SrAlarm.unset();
      break;
    default:
      break;
    }

    if ( restartT2Alarms && !mT2ZeroAlarm.set && !mT2UnderflowAlarm.set )
    {
      std::uint16_t const current = t2( rclk );
      mT2cl = static_cast<std::uint8_t>( current & 0xffU );
      mT2ch = static_cast<std::uint8_t>( current >> 8U );
      scheduleT2ZeroAlarm( rclk + t2StartupDelay );
    }

    mVia[ACR] = byte;
    cacheCb12IoStatus();
    mPorts.storeAcr( byte );
    break;
  }

  case PCR:
    // Low output; a high output, and toggle, pulse and input modes, idle high.
    mCa2OutState = ( byte & PCR_CA2_CONTROL ) != PCR_CA2_LOW_OUTPUT;
    mPorts.setCa2( mCa2OutState );
    // The shift register's control of CB2 overrides the PCR's.
    if ( ( mVia[ACR] & ACR_SR_CONTROL ) == ACR_SR_DISABLED )
    {
      setCb2OutputState( byte, rclk );
    }
    mVia[PCR] = byte;
    cacheCb12IoStatus();
    break;

  default:
    mVia.at( addr ) = byte;
    break;
  }
}

std::uint8_t Via6522::read( std::uint16_t address, Clock now )
{
  Clock const rclk = now;
  auto const addr = static_cast<std::uint8_t>( address & 0x0fU );
  std::uint8_t byte = 0xff;

  switch ( addr )
  {
  case PRA:
    mIfr &= static_cast<std::uint8_t>( ~IM_CA1 );
    if ( ( mVia[PCR] & 0x0aU ) != 0x02 )
    {
      mIfr &= static_cast<std::uint8_t>( ~IM_CA2 );
    }
    if ( isCa2Handshake() )
    {
      mCa2OutState = false;
      mPorts.setCa2( mCa2OutState );
      if ( isCa2PulseMode() )
      {
        mCa2OutState = true;
        mPorts.setCa2( mCa2OutState );
      }
    }
    if ( ( mIer & ( IM_CA1 | IM_CA2 ) ) != 0 )
    {
      updateIrq( rclk );
    }
    [[fallthrough]];
  case PRA_NHS:
    // The pins' voltage, not the output register, as VICE reads port A.
    byte = mPorts.readPra( false );
    mLastRead = byte;
    return byte;

  case PRB:
    mIfr &= static_cast<std::uint8_t>( ~IM_CB1 );
    if ( ( mVia[PCR] & 0xa0U ) != 0x20 )
    {
      mIfr &= static_cast<std::uint8_t>( ~IM_CB2 );
    }
    if ( ( mIer & ( IM_CB1 | IM_CB2 ) ) != 0 )
    {
      updateIrq( rclk );
    }
    // Port B reads the output register for its output pins.
    byte = mPorts.readPrb( false );
    byte = static_cast<std::uint8_t>( ( byte & ~mVia[DDRB] ) | ( mVia[PRB] & mVia[DDRB] ) );
    if ( ( mVia[ACR] & ACR_T1_PB7_USED ) != 0 )
    {
      byte = static_cast<std::uint8_t>( ( byte & 0x7fU ) | mT1pb7 );
    }
    mLastRead = byte;
    return byte;

  case T1CL:
    mIfr &= static_cast<std::uint8_t>( ~IM_T1 );
    updateIrq( rclk );
    mLastRead = static_cast<std::uint8_t>( t1( rclk ) & 0xffU );
    return mLastRead;

  case T1CH:
    mLastRead = static_cast<std::uint8_t>( t1( rclk ) >> 8U );
    return mLastRead;

  case T2CL:
    mIfr &= static_cast<std::uint8_t>( ~IM_T2 );
    updateIrq( rclk );
    mLastRead = static_cast<std::uint8_t>( t2( rclk ) & 0xffU );
    return mLastRead;

  case T2CH:
    mLastRead = static_cast<std::uint8_t>( t2( rclk ) >> 8U );
    return mLastRead;

  case SR:
    setupShifting( rclk );
    if ( ( mIfr & IM_SR ) != 0 )
    {
      mIfr &= static_cast<std::uint8_t>( ~IM_SR );
      updateIrq( rclk );
    }
    mLastRead = mVia[SR];
    return mLastRead;

  case IFR:
  {
    std::uint8_t flags = mIfr;
    if ( ( mIfr & mIer ) != 0 )
    {
      flags |= 0x80U;
    }
    mLastRead = flags;
    return flags;
  }

  case IER:
    // Bit 7 reads 1, as Rockwell has it; Commodore's sheet says 0.
    mLastRead = static_cast<std::uint8_t>( mIer | 0x80U );
    return mLastRead;

  default:
    break;
  }
  mLastRead = mVia.at( addr );
  return mLastRead;
}

std::uint8_t Via6522::peek( std::uint16_t address, Clock now )
{
  auto const addr = static_cast<std::uint8_t>( address & 0x0fU );
  switch ( addr )
  {
  case PRA:
  case PRA_NHS:
    return mPorts.readPra( true );
  case PRB:
  {
    std::uint8_t byte = mPorts.readPrb( true );
    byte = static_cast<std::uint8_t>( ( byte & ~mVia[DDRB] ) | ( mVia[PRB] & mVia[DDRB] ) );
    if ( ( mVia[ACR] & ACR_T1_PB7_USED ) != 0 )
    {
      byte = static_cast<std::uint8_t>( ( byte & 0x7fU ) | mT1pb7 );
    }
    return byte;
  }
  case T1CL:
    return static_cast<std::uint8_t>( t1( now ) & 0xffU );
  case T1CH:
    return static_cast<std::uint8_t>( t1( now ) >> 8U );
  case T2CL:
    return static_cast<std::uint8_t>( t2( now ) & 0xffU );
  case T2CH:
    return static_cast<std::uint8_t>( t2( now ) >> 8U );
  case IFR:
    return static_cast<std::uint8_t>( mIfr | ( ( mIfr & mIer ) != 0 ? 0x80U : 0U ) );
  case IER:
    return static_cast<std::uint8_t>( mIer | 0x80U );
  default:
    return mVia.at( addr );
  }
}

Via6522::Snapshot Via6522::snapshot( Clock now )
{
  return Snapshot{ .registers = mVia,
                   .t1 = t1( now ),
                   .t1Latch = static_cast<std::uint16_t>( mTal ),
                   .t2 = t2( now ),
                   .ifr = mIfr,
                   .ier = mIer };
}

void Via6522::cycle( Clock now )
{
  // Alarms due at the same cycle run in the order VICE created them.
  for ( ;; )
  {
    if ( mT1ZeroAlarm.set && mT1ZeroAlarm.at <= now )
    {
      t1ZeroAlarm( mT1ZeroAlarm.at );
    }
    else if ( mT2ZeroAlarm.set && mT2ZeroAlarm.at <= now )
    {
      t2ZeroAlarm( mT2ZeroAlarm.at );
    }
    else if ( mT2UnderflowAlarm.set && mT2UnderflowAlarm.at <= now )
    {
      t2UnderflowAlarm( mT2UnderflowAlarm.at );
    }
    else if ( mT2ShiftAlarm.set && mT2ShiftAlarm.at <= now )
    {
      t2ShiftAlarm( mT2ShiftAlarm.at );
    }
    else if ( mPhi2SrAlarm.set && mPhi2SrAlarm.at <= now )
    {
      phi2SrAlarm( mPhi2SrAlarm.at );
    }
    else
    {
      return;
    }
  }
}

// viacore_t1_zero_alarm: after T1 reads 0000.
void Via6522::t1ZeroAlarm( Clock rclk )
{
  if ( ( mVia[ACR] & ACR_T1_FREE_RUN ) == 0 )
  {
    // One-shot: no further interrupt until T1CH is written again.
    mT1ZeroAlarm.unset();
    mT1zero = 0;
  }
  else
  {
    mT1zero += mTal + FULL_CYCLE_2;
    mT1ZeroAlarm.schedule( mT1zero );
  }
  mT1pb7 ^= 0x80U;
  mIfr |= IM_T1;
  // The interrupt follows the flag by a cycle.
  updateIrq( rclk + 1 );
}

// set_cb2_output_state.
void Via6522::setCb2OutputState( std::uint8_t pcr, Clock at )
{
  std::uint8_t const mode = pcr & PCR_CB2_CONTROL;
  if ( ( mode & PCR_CB2_I_OR_O ) == PCR_CB2_INPUT )
  {
    // An input, but a 1 for whoever listens.
    mCb2OutState = true;
    mPorts.setCb2( true, at );
    return;
  }
  // Low output; high, pulse and handshake outputs idle high.
  mCb2OutState = mode != PCR_CB2_LOW_OUTPUT;
  mPorts.setCb2( mCb2OutState, at );
}

// viacore_cache_cb12_io_status.
void Via6522::cacheCb12IoStatus()
{
  std::uint8_t const acr = mVia[ACR];
  std::uint8_t const pcr = mVia[PCR];
  bool const cb1DrivesShifting =
      ( acr & ACR_SR_CONTROL & 0x0cU ) == ACR_SR_IN_CB1 || ( acr & ACR_SR_CONTROL ) == ACR_SR_DISABLED;
  bool const srIsInput = ( acr & ACR_SR_OUT ) == 0 && ( acr & ACR_SR_CONTROL ) != ACR_SR_DISABLED;
  bool const cb2IsInput = ( pcr & PCR_CB2_I_OR_O ) == PCR_CB2_INPUT;
  mCb1IsInput = cb1DrivesShifting;
  mCb2IsInput = srIsInput || cb2IsInput;
  // The PET's VIA has no CB1 output to set idle (petvia.c sets no set_cb1).
}

// viacore_set_cb1: CB1 as an input, the shift register's external clock.
void Via6522::setCb1( bool data, Clock now )
{
  if ( data != mCb1InState )
  {
    if ( mCb1IsInput )
    {
      if ( !data && mShiftState == FINISHED_SHIFTING )
      {
        mShiftState = START_SHIFTING;
      }
      ++mShiftState;
      if ( data )
      {
        mVia[SR] = static_cast<std::uint8_t>( ( static_cast<unsigned>( mVia[SR] ) << 1U ) | ( mCb2InState ? 1U : 0U ) );
        if ( mShiftState == FINISHED_SHIFTING )
        {
          // viacore_set_sr: a byte shifted in.
          if ( ( mVia[ACR] & ACR_SR_OUT ) == 0 && ( mVia[ACR] & 0x0cU ) != 0 )
          {
            mIfr |= IM_SR;
            updateIrq( now );
            mShiftState = FINISHED_SHIFTING;
          }
          mShiftState = START_SHIFTING;
        }
      }
    }
    mCb1InState = data;
  }
  // Unconditionally, as VICE does: SpeedDOS needs it.
  bool const edge = ( mVia[PCR] & PCR_CB1_CONTROL ) == PCR_CB1_POS_ACTIVE_EDGE;
  if ( data == edge )
  {
    if ( isCb2ToggleMode() && !mCb2OutState )
    {
      mCb2OutState = true;
      mPorts.setCb2( mCb2OutState, now );
    }
    mIfr |= IM_CB1;
    updateIrq( now );
  }
}

// viacore_set_cb2.
void Via6522::setCb2( bool data, Clock now )
{
  if ( mCb2IsInput && data != mCb2InState )
  {
    mCb2InState = data;
    bool const edge = ( mVia[PCR] & PCR_CB2_INPUT_POS_ACTIVE_EDGE ) != 0;
    if ( data == edge )
    {
      mIfr |= IM_CB2;
      updateIrq( now );
    }
  }
}

// viacore_t2_zero_alarm: T2 low has reached 00.
void Via6522::t2ZeroAlarm( Clock rclk )
{
  // The low byte's underflow always takes one from the high byte.
  --mT2ch;
  // A 16-bit underflow interrupts, once for each write of T2CH; in 8-bit mode
  // too (viavarious/via20).
  if ( mT2ch == 0xff && mT2IrqAllowed )
  {
    mIfr |= IM_T2;
    updateIrq( rclk );
    mT2IrqAllowed = false;
  }
  mT2ZeroAlarm.unset();
  mT2UnderflowAlarm.schedule( rclk + 1 );
}

// viacore_t2_underflow_alarm: after T2 shows xxFF; reload or not.
void Via6522::t2UnderflowAlarm( Clock rclk )
{
  unsigned nextAlarm = 0;
  if ( ( mVia[ACR] & 0x0cU ) == 0x04 || isSrFreeRunning() )
  {
    // 8-bit mode under the shift register, or its free-running output: T2
    // low reloads, and every second underflow clocks a bit.
    mT2cl = mVia[T2CL];
    nextAlarm = mVia[T2CL] + FULL_CYCLE_2;
    mT2ShiftAlarm.schedule( rclk + 1 );
  }
  else
  {
    // 16-bit mode: T2 low is in underflow. The next xx00 comes 256 cycles on,
    // until the high byte underflows too.
    mT2cl = 0xff;
    nextAlarm = mT2ch != 0xff ? 256 : 0;
  }

  if ( nextAlarm != 0 )
  {
    mT2zero += nextAlarm;
    mT2xx00 = true;
    mT2ZeroAlarm.schedule( mT2zero );
  }
  else
  {
    mT2ZeroAlarm.unset();
    // From here T2 is reckoned as 16 bits: it reads FFFF a cycle after zero.
    mT2xx00 = false;
  }
  mT2UnderflowAlarm.unset();
}

void Via6522::t2ShiftAlarm( Clock rclk )
{
  doShiftRegister( rclk );
  mT2ShiftAlarm.unset();
}

void Via6522::phi2SrAlarm( Clock rclk )
{
  doShiftRegister( rclk );
  mPhi2SrAlarm.schedule( rclk + SR_PHI2_NEXT_OFFSET );
}

// do_shiftregister: shifting out shifts first and then holds; shifting in
// waits first and then shifts.
void Via6522::doShiftRegister( Clock rclk )
{
  if ( mShiftState >= FINISHED_SHIFTING )
  {
    return;
  }
  bool const shiftOut = ( mVia[ACR] & ACR_SR_OUT ) != 0;
  if ( ( mShiftState & 1U ) == 0 )
  {
    // Even: CB1 low, where it is an output; the PET's is not wired.
    if ( shiftOut )
    {
      auto const cb2 = static_cast<std::uint8_t>( ( mVia[SR] >> 7U ) & 1U );
      mVia[SR] = static_cast<std::uint8_t>( ( static_cast<unsigned>( mVia[SR] ) << 1U ) | cb2 );
      mCb2OutState = cb2 != 0;
      mPorts.setCb2( mCb2OutState, rclk );
    }
  }
  else if ( !shiftOut )
  {
    // Odd: shift in.
    mVia[SR] = static_cast<std::uint8_t>( ( static_cast<unsigned>( mVia[SR] ) << 1U ) | ( mCb2InState ? 1U : 0U ) );
  }

  ++mShiftState;
  if ( mShiftState == FINISHED_SHIFTING )
  {
    // Free-running output sets no flag and goes on.
    if ( isSrFreeRunning() )
    {
      mShiftState = START_SHIFTING;
    }
    else
    {
      mIfr |= IM_SR;
      updateIrq( rclk );
    }
  }
}

} // namespace pet::machine
