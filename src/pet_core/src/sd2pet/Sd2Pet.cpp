// Copyright (c) 2015, 2017 Nils Eilers. All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
// TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// This file ports NODISKEMU's src/ieee.c (Copyright (c) 2015, 2017 Nils
// Eilers) and src/avr/atn-ack-petsd+.S (Copyright (c) 2015 Nils Eilers),
// both under the licence above, to C++ for PetMcp, 2026.

#include "sd2pet/Sd2Pet.hpp"

#include "machine/Ieee488Bus.hpp"
#include "sd2pet/Dos.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <stdexcept>

namespace pet::sd2pet
{

using machine::Clock;
using machine::IEEE_ATN;
using machine::IEEE_DAV;
using machine::IEEE_EOI;
using machine::IEEE_NDAC;
using machine::IEEE_NRFD;

namespace
{

// Upper three bit commands with attached device number
constexpr std::uint8_t IEEE_LISTEN = 0x20; // 0x20 - 0x3E
constexpr std::uint8_t IEEE_UNLISTEN = 0x3F;
constexpr std::uint8_t IEEE_TALK = 0x40; // 0x40 - 0x5E
constexpr std::uint8_t IEEE_UNTALK = 0x5F;

// Upper four bit commands with attached secondary address
constexpr std::uint8_t IEEE_SECONDARY = 0x60;
constexpr std::uint8_t IEEE_CLOSE = 0xE0;
constexpr std::uint8_t IEEE_OPEN = 0xF0;

/// PETSCII as the protocol shows it: the PET's graphics set, where the
/// unshifted letters are capitals.
char asciiOf( std::uint8_t petscii )
{
  if ( petscii >= 0xc1 && petscii <= 0xda )
  {
    return static_cast<char>( 'a' + ( petscii - 0xc1 ) );
  }
  if ( petscii >= 0x20 && petscii < 0x7f )
  {
    return static_cast<char>( petscii );
  }
  return '?';
}

} // namespace

Sd2Pet::Sd2Pet( machine::Ieee488Bus& bus ) : mBus{ bus }, mDos{ std::make_unique<Dos>() }
{
  // main.c: the buffers and the file system, then the bus, and the error
  // channel's 73 that ieee_mainloop sets.
  dosInit( *mDos );
  init();
  apply( 0 );
}

Sd2Pet::~Sd2Pet() = default;

// --------------------------------------------------------------------------
// The ATmega's pins and the bus drivers

/// The drive's end of the bus as the pins and the drivers' directions make
/// it. The control lines' driver (a 75161) sends DAV and EOI when talking and
/// NDAC and NRFD when listening, and turns EOI round under ATN.
void Sd2Pet::apply( Clock now )
{
  machine::IeeeEnd end;
  if ( mDataTalk )
  {
    // ieee488_SetData: the port holds the byte inverted.
    end.data = static_cast<std::uint8_t>( ~mData );
  }
  if ( mCtrlTalk )
  {
    if ( !mDav )
    {
      end.lines |= IEEE_DAV;
    }
    if ( !mEoi && !mBus.low( IEEE_ATN ) )
    {
      end.lines |= IEEE_EOI;
    }
  }
  else
  {
    if ( !mNdac )
    {
      end.lines |= IEEE_NDAC;
    }
    if ( !mNrfd )
    {
      end.lines |= IEEE_NRFD;
    }
  }
  mBus.setDrive( end, now );
}

/// A line as ieee.c reads it: true when high.
bool Sd2Pet::line( std::uint8_t which ) const
{
  return !mBus.low( which );
}

/// ieee488_Data: the lines inverted.
std::uint8_t Sd2Pet::data() const
{
  return static_cast<std::uint8_t>( ~mBus.data() );
}

void Sd2Pet::ctrlPortsListen()
{
  // EOI and DAV as inputs, TE to listen, NDAC and NRFD as outputs.
  mCtrlTalk = false;
}

void Sd2Pet::ctrlPortsTalk()
{
  // NDAC and NRFD as inputs, DAV and EOI high, TE to talk, DAV and EOI as
  // outputs.
  mDav = true;
  mEoi = true;
  mCtrlTalk = true;
}

void Sd2Pet::busIdle()
{
  if ( mCtrlTalk )
  {
    ctrlPortsListen();
  }
  mNdac = true;
  mNrfd = true;
  mDataTalk = false;
}

/// ieee488_Init, as far as it concerns the bus.
void Sd2Pet::init()
{
  mListenActive = 0;
  mTalkingDevice = 0;
  mOpenSa = 0;
  mCommandReceived = false;
  mOpenActive = false;
  mDataTalk = false;
  ctrlPortsListen();
  busIdle();
}

/// atn-ack-petsd+.S: INT0, on ATN's falling edge.
void Sd2Pet::atnInterrupt()
{
  // switch all ports to input to avoid conflicting with bus drivers, the
  // bus drivers to listen mode, then acknowledge ATN
  mDataTalk = false;
  mCtrlTalk = false;
  mNrfd = false;
  mNdac = true;
  mAtnReceived = true;
}

// --------------------------------------------------------------------------
// Stepping

void Sd2Pet::cycle( Clock now )
{
  bool const atnLow = mBus.low( IEEE_ATN );
  if ( atnLow != mAtnWasLow )
  {
    mAtnWasLow = atnLow;
    if ( atnLow )
    {
      mAtnInterruptPending = true;
      mAtnInterruptAt = now + mTiming.atnAcknowledge;
    }
    if ( mCtrlTalk )
    {
      apply( now );
    }
  }
  if ( mAtnInterruptPending && now >= mAtnInterruptAt )
  {
    mAtnInterruptPending = false;
    atnInterrupt();
    apply( now );
  }
  if ( now >= mWakeAt )
  {
    run( now );
  }
}

void Sd2Pet::react( State next, Clock now )
{
  mState = next;
  mWakeAt = now + mTiming.reaction;
}

void Sd2Pet::busy( State next, Clock now, Clock cycles )
{
  mState = next;
  mWakeAt = now + cycles;
}

void Sd2Pet::run( Clock now )
{
  if ( mHung )
  {
    return;
  }
  guarded( [&] { step( now ); } );
}

template <class Call>
void Sd2Pet::guarded( Call const& call )
{
  try
  {
    call();
  }
  catch ( DriveHung const& )
  {
    hang( "the firmware loops for ever" );
  }
  catch ( std::out_of_range const& error )
  {
    // Where the firmware reads or writes past an array, which the ATmega
    // does without a word and the port refuses.
    hang( error.what() );
  }
}

void Sd2Pet::hang( char const* why )
{
  spdlog::warn( "The SD2PET stops: {}", why );
  mHung = true;
}

void Sd2Pet::step( Clock now )
{
  for ( ;; )
  {
    switch ( mState )
    {
    // handle_ieee488
    case State::MAIN:
      // Return to scheduler if ATN is inactive
      if ( !mAtnReceived )
      {
        return;
      }
      mAtnReceived = false;
      // ATN interrupt routine switched to LISTEN mode, released NDAC and
      // pulled NRFD low. We can wait here any time long until we release
      // NRFD.
      mState = State::COMMAND_READY;
      break;

    case State::COMMAND_READY:
      // Fetch all commands sent in the same ATN-low-cycle
      mNdac = false;
      mNrfd = true; // Say ready for data
      mDataTalk = false;
      apply( now );
      mState = State::COMMAND_WAIT_DAV_LOW;
      break;

    case State::COMMAND_WAIT_DAV_LOW:
      if ( mAtnReceived || line( IEEE_ATN ) )
      {
        // new ATN cycle, or ATN cycle aborted
        react( State::COMMAND_ABORT, now );
        return;
      }
      if ( !line( IEEE_DAV ) )
      {
        react( State::COMMAND_TAKE, now );
      }
      return;

    case State::COMMAND_ABORT:
      // Bus Idle if we're neither listener nor talker
      if ( mListenActive == 0 && mTalkingDevice == 0 )
      {
        busIdle();
        apply( now );
      }
      mState = State::MAIN;
      break;

    case State::COMMAND_TAKE:
      mNrfd = false; // Say not ready for data
      mCommand = data();
      mNdac = true; // Say data accepted
      apply( now );
      mState = State::COMMAND_WAIT_DAV_HIGH;
      break;

    case State::COMMAND_WAIT_DAV_HIGH:
      if ( mAtnReceived || line( IEEE_ATN ) )
      {
        react( State::MAIN, now );
        return;
      }
      if ( line( IEEE_DAV ) )
      {
        react( State::COMMAND_DO, now );
      }
      return;

    case State::COMMAND_DO:
    {
      std::uint8_t const cmd3 = mCommand & 0b11100000U;
      std::uint8_t const cmd4 = mCommand & 0b11110000U;
      std::uint8_t const device = mCommand & 0b00011111U;
      std::uint8_t const sa = mCommand & 0b00001111U;
      Clock cost = mTiming.commandByte;
      State next = State::COMMAND_READY;
      if ( mCommand == IEEE_UNLISTEN )
      {
        if ( mCommandReceived )
        {
          cost += mTiming.dosCommand;
        }
        else if ( mOpenActive )
        {
          cost += mTiming.fileOpen;
        }
        unlisten( now );
      }
      else if ( mCommand == IEEE_UNTALK )
      {
        untalk();
        apply( now );
      }
      else if ( cmd3 == IEEE_LISTEN )
      {
        if ( device == mDos->deviceAddress )
        {
          mListenActive = device;
          // Override talk state because we can't be listener and talker at
          // the same time
          mTalkingDevice = 0;
        }
      }
      else if ( cmd3 == IEEE_TALK )
      {
        if ( device == mDos->deviceAddress )
        {
          mTalkingDevice = device;
          // Override listen state because we can't be listener and talker
          // at the same time
          mListenActive = 0;
        }
      }
      else if ( cmd4 == IEEE_SECONDARY )
      {
        mSa = sa;
        next = State::SECONDARY_WAIT_ATN;
      }
      else if ( cmd4 == IEEE_CLOSE )
      {
        if ( mListenActive != 0 )
        {
          cost += mTiming.close;
          close( sa, now );
        }
      }
      else if ( cmd4 == IEEE_OPEN )
      {
        mSa = sa;
        next = State::OPEN_WAIT_ATN;
      }
      busy( next, now, cost );
      return;
    }

    case State::SECONDARY_WAIT_ATN:
      // Wait for ATN high
      if ( line( IEEE_ATN ) )
      {
        react( State::SECONDARY_GO, now );
      }
      return;

    case State::SECONDARY_GO:
      if ( mListenActive != 0 )
      {
        listenLoop( ListenAction::RECEIVE, mSa, now );
      }
      else if ( mTalkingDevice != 0 )
      {
        talkLoop( mSa, now );
      }
      else
      {
        mState = State::COMMAND_READY;
      }
      break;

    case State::OPEN_WAIT_ATN:
      if ( line( IEEE_ATN ) )
      {
        react( State::OPEN_GO, now );
      }
      return;

    case State::OPEN_GO:
      if ( mListenActive != 0 )
      {
        mOpenActive = true;
        mOpenSa = mSa;
        listenLoop( ListenAction::OPEN, mSa, now );
      }
      else
      {
        mState = State::COMMAND_READY;
      }
      break;

    // ieee488_ListenLoop, after ieee488_RxByte
    case State::LISTEN_BYTE:
      if ( mRx == Rx::ATN )
      {
        mState = State::COMMAND_READY; // ATN received, abort
        break;
      }
      listenByte( now );
      if ( now < mWakeAt )
      {
        return;
      }
      break;

    // ieee488_IgnoreBytes
    case State::IGNORE_BYTE:
      if ( mRx == Rx::ATN )
      {
        mState = State::COMMAND_READY;
        break;
      }
      rxByte( State::IGNORE_BYTE );
      busy( State::RX_TOP, now, mTiming.listenByte );
      return;

    // ieee488_RxByte
    case State::RX_TOP:
      mNrfd = true;
      mNdac = false;
      mDataTalk = false;
      apply( now );
      mState = State::RX_WAIT_DAV_LOW;
      break;

    case State::RX_WAIT_DAV_LOW:
      if ( mAtnReceived )
      {
        react( State::RX_ABORT, now ); // ATN became low, abort
        return;
      }
      if ( !line( IEEE_DAV ) )
      {
        react( State::RX_TAKE, now );
      }
      return;

    case State::RX_TAKE:
      // DAV is now low, NDAC must be high in max. 64 ms
      mNrfd = false;
      if ( !line( IEEE_EOI ) )
      {
        mRx = Rx::EOI;
      }
      mRxChar = data();
      if ( line( IEEE_DAV ) )
      {
        // If DAV is high again, we've seen only a glitch
        apply( now );
        mState = State::RX_TOP;
        break;
      }
      mNdac = true;
      apply( now );
      mState = State::RX_WAIT_DAV_HIGH;
      break;

    case State::RX_WAIT_DAV_HIGH:
      if ( mAtnReceived )
      {
        react( State::RX_ABORT, now );
        return;
      }
      if ( line( IEEE_DAV ) )
      {
        react( State::RX_DONE, now ); // wait for DAV high
      }
      return;

    case State::RX_DONE:
      mNdac = false;
      apply( now );
      mState = mRxBack;
      break;

    case State::RX_ABORT:
      mRx = Rx::ATN;
      mState = mRxBack;
      break;

    // ieee488_TalkLoop
    case State::TALK_OUTER:
      if ( !mBuffer->read )
      {
        mState = State::COMMAND_READY;
        break;
      }
      mState = State::TALK_BYTE;
      break;

    case State::TALK_BYTE:
      mDav = true; // Release DAV and EOI
      mEoi = true;
      apply( now );
      mState = State::TALK_WAIT_NDAC_LOW;
      break;

    case State::TALK_WAIT_NDAC_LOW:
      if ( !line( IEEE_NDAC ) )
      {
        react( State::TALK_WAIT_NRFD_HIGH, now );
      }
      else if ( mAtnReceived )
      {
        react( State::TALK_ABORT_IDLE, now ); // T1
      }
      return;

    case State::TALK_WAIT_NRFD_HIGH:
      if ( line( IEEE_NRFD ) )
      {
        // Fetch preloaded byte within less than 64 ms
        mState = State::TALK_SEND;
        mWakeAt = now + mTiming.reaction + mTiming.talkByte;
      }
      else if ( mAtnReceived )
      {
        react( State::TALK_ABORT, now ); // T2
      }
      return;

    case State::TALK_SEND:
      mLastByte = mBuffer->position == mBuffer->lastused;
      mTalkChar = mBuffer->data[mBuffer->position];
      if ( line( IEEE_NDAC ) || mAtnReceived )
      {
        mState = State::TALK_ABORT_IDLE; // NDAC must stay low; T3
        break;
      }
      mEoi = !( mLastByte && mBuffer->sendeoi );
      mDataTalk = true;
      mData = mTalkChar;
      if ( line( IEEE_NDAC ) || mAtnReceived )
      {
        apply( now );
        mState = State::TALK_ABORT; // T4
        break;
      }
      mDav = false; // Say data valid
      apply( now );
      mState = State::TALK_WAIT_NRFD_LOW;
      break;

    case State::TALK_WAIT_NRFD_LOW:
      // Wait for NRFD low, NDAC must stay low
      if ( !line( IEEE_NRFD ) )
      {
        react( State::TALK_WAIT_NDAC_HIGH, now );
      }
      else if ( line( IEEE_NDAC ) || mAtnReceived )
      {
        react( State::TALK_ABORT_RELEASE_IDLE, now ); // T5
      }
      return;

    case State::TALK_WAIT_NDAC_HIGH:
      if ( line( IEEE_NDAC ) )
      {
        // Listeners have received our byte
        react( State::TALK_NEXT, now );
      }
      else if ( mAtnReceived )
      {
        react( State::TALK_ABORT_RELEASE, now ); // T6
      }
      return;

    case State::TALK_NEXT:
    {
      if ( mBuffer->position++ < mBuffer->lastused )
      {
        mState = State::TALK_BYTE;
        break;
      }
      // PET/CBM-II wait here without timeout until DAV=1
      // Perfect for flushing buffers without hurry
      if ( mBuffer->sendeoi && mSa != 15 && mBuffer->recordlen == 0 && mBuffer->refill != directbufferRefill )
      {
        mBuffer->read = false;
        mDav = true;
        mEoi = true; // Release DAV and EOI; T8
        apply( now );
        mState = State::TALK_OUTER;
        break;
      }
      Clock const cost = refillCost();
      if ( mBuffer->refill( *mDos, mBuffer ) != 0 )
      {
        // T9: DAV and EOI let go once the refill has failed.
        busy( State::TALK_ABORT_RELEASE, now, cost );
        return;
      }
      // Search the buffer again, it can change when using large buffers
      mBuffer = findBuffer( *mDos, mSa );
      busy( State::TALK_OUTER, now, cost );
      return;
    }

    case State::TALK_ABORT_IDLE:
      busIdle();
      apply( now );
      mState = State::COMMAND_READY;
      break;

    case State::TALK_ABORT:
      mState = State::COMMAND_READY;
      break;

    case State::TALK_ABORT_RELEASE_IDLE:
      mDav = true;
      mEoi = true;
      busIdle();
      apply( now );
      mState = State::COMMAND_READY;
      break;

    case State::TALK_ABORT_RELEASE:
      mDav = true;
      mEoi = true;
      apply( now );
      mState = State::COMMAND_READY;
      break;
    }
  }
}

// --------------------------------------------------------------------------
// ieee.c's functions

/// What refilling the current buffer costs: a # buffer starts over, any
/// other reads or writes the next sector of its file.
Clock Sd2Pet::refillCost() const
{
  return mBuffer->refill == directbufferRefill ? mTiming.bufferWrap : mTiming.sectorRead;
}

void Sd2Pet::rxByte( State back )
{
  mRx = Rx::DATA;
  mRxBack = back;
  mState = State::RX_TOP;
}

void Sd2Pet::listenLoop( ListenAction action, std::uint8_t sa, Clock now )
{
  mListenAction = action;
  mBuffer = findBuffer( *mDos, sa );
  // Abort if there is no buffer or it's not open for writing and it isn't an
  // OPEN command
  if ( ( mBuffer == nullptr || !mBuffer->write ) && action != ListenAction::OPEN )
  {
    rxByte( State::IGNORE_BYTE );
    return;
  }
  if ( sa == 15 )
  {
    mCommandReceived = true;
  }
  rxByte( State::LISTEN_BYTE );
  static_cast<void>( now );
}

/// ieee488_ListenLoop's body, for the byte ieee488_RxByte took.
void Sd2Pet::listenByte( Clock now )
{
  Clock cost = mTiming.listenByte;
  if ( mListenAction == ListenAction::OPEN || mCommandReceived )
  {
    // RxChar: receive commands and filenames
    auto& commands = mDos->commands;
    if ( commands.commandLength < COMMAND_BUFFER_SIZE )
    {
      commands.commandBuffer.at( commands.commandLength++ ) = mRxChar;
    }
    rxByte( State::LISTEN_BYTE );
    busy( State::RX_TOP, now, cost );
    return;
  }

  // Flush buffer if full
  if ( mBuffer->mustflush )
  {
    cost += refillCost();
    if ( mBuffer->refill( *mDos, mBuffer ) != 0 )
    {
      rxByte( State::IGNORE_BYTE );
      busy( State::RX_TOP, now, cost );
      return;
    }
    // Search the buffer again, it can change when using large buffers
    mBuffer = findBuffer( *mDos, mSa );
  }

  mBuffer->data[mBuffer->position] = mRxChar;
  markBufferDirty( *mDos, mBuffer );

  mBuffer->lastused = std::max( mBuffer->lastused, mBuffer->position );
  mBuffer->position++;

  // Mark buffer for flushing if position wrapped
  if ( mBuffer->position == 0 )
  {
    mBuffer->mustflush = true;
  }

  // REL files must be syncronized on EOI
  if ( mBuffer->recordlen != 0 && mRx == Rx::EOI )
  {
    cost += refillCost();
    if ( mBuffer->refill( *mDos, mBuffer ) != 0 )
    {
      rxByte( State::IGNORE_BYTE );
      busy( State::RX_TOP, now, cost );
      return;
    }
  }
  rxByte( State::LISTEN_BYTE );
  busy( State::RX_TOP, now, cost );
}

void Sd2Pet::talkLoop( std::uint8_t sa, Clock now )
{
  // This function returns immediately on ATN low. The ATN interrupt routine
  // will handle the IEEE ports then and here's nothing left to do.
  mBuffer = findBuffer( *mDos, sa );
  if ( mBuffer == nullptr )
  {
    busIdle(); // T0
    apply( now );
    mState = State::COMMAND_READY;
    return;
  }
  ctrlPortsTalk(); // Set hardware to TALK mode
  apply( now );
  mState = State::TALK_OUTER;
}

void Sd2Pet::unlisten( Clock now )
{
  busIdle();
  apply( now );
  // If we received a command or a file name to open, process it now
  if ( mCommandReceived )
  {
    parseDoscommand( *mDos );
  }
  else if ( mOpenActive )
  {
    mDos->commands.datacrc = 0xffff; // filename in command buffer
    fileOpen( *mDos, mOpenSa );
  }
  mListenActive = 0;
  mCommandReceived = false;
  mOpenActive = false;
  mDos->commands.commandLength = 0;
}

void Sd2Pet::untalk()
{
  busIdle();
  mTalkingDevice = 0; // we don't talk any more
}

void Sd2Pet::close( std::uint8_t sa, Clock now )
{
  static_cast<void>( now );
  if ( sa == 15 )
  {
    freeMultipleBuffers( *mDos, FMB_USER_CLEAN );
    mTalkingDevice = 0;
    return;
  }
  Buffer* const buf = findBuffer( *mDos, sa );
  if ( buf != nullptr )
  {
    buf->cleanup( *mDos, buf );
    freeBuffer( *mDos, buf );
  }
}

// --------------------------------------------------------------------------
// From outside

void Sd2Pet::interfaceClear( Clock now )
{
  // ieee488_ProcessIFC: the PET's reset reaches the drive as IFC, which the
  // main loop polls; a drive that hangs never sees it.
  if ( mHung )
  {
    return;
  }
  guarded( [&] { freeMultipleBuffers( *mDos, FMB_USER_CLEAN ); } );
  init();
  setError( *mDos, ERROR_DOSVERSION );
  mAtnReceived = false;
  mAtnInterruptPending = false;
  mState = State::MAIN;
  mWakeAt = now;
  apply( now );
}

void Sd2Pet::idle( Clock now )
{
  busIdle();
  mAtnReceived = false;
  mAtnInterruptPending = false;
  mAtnWasLow = mBus.low( IEEE_ATN );
  mListenActive = 0;
  mTalkingDevice = 0;
  mCommandReceived = false;
  mOpenActive = false;
  mDos->commands.commandLength = 0;
  mState = State::MAIN;
  mWakeAt = now;
  apply( now );
}

void Sd2Pet::setCard( std::filesystem::path const& root )
{
  // handle_card_changes: the buffers are useless with the card they read.
  guarded(
      [&]
      {
        freeMultipleBuffers( *mDos, FMB_ALL );
        mDos->card.setRoot( root );
        fatopsInit( *mDos, 0 );
      } );
}

std::filesystem::path const& Sd2Pet::card() const
{
  return mDos->card.root();
}

std::string Sd2Pet::status() const
{
  Buffer const& error = mDos->buffers.buffers.at( ERRORBUFFER_IDX );
  std::string text;
  for ( unsigned at = 0; at <= error.lastused; ++at )
  {
    std::uint8_t const byte = error.data[at];
    if ( byte == 0x0d )
    {
      break;
    }
    text.push_back( asciiOf( byte ) );
  }
  return text;
}

std::string Sd2Pet::command( std::string_view command )
{
  if ( mHung )
  {
    return status();
  }
  auto& commands = mDos->commands;
  commands.commandLength = 0;
  for ( char const c : command )
  {
    if ( commands.commandLength < COMMAND_BUFFER_SIZE )
    {
      commands.commandBuffer.at( commands.commandLength++ ) = static_cast<std::uint8_t>( c );
    }
  }
  guarded( [&] { parseDoscommand( *mDos ); } );
  commands.commandLength = 0;
  return status();
}

std::string Sd2Pet::mount( std::filesystem::path const& image )
{
  if ( mHung )
  {
    return status();
  }
  if ( !mDos->card.present() )
  {
    // No card to mount from: the image's directory goes in as one.
    setCard( image.parent_path() );
  }
  guarded( [&] { fatMountHostImage( *mDos, image ); } );
  return status();
}

std::filesystem::path Sd2Pet::directory() const
{
  return mDos->card.pathOf( mDos->parser.partition.at( 0 ).currentDir.fat );
}

std::filesystem::path Sd2Pet::image() const
{
  Partition const& partition = mDos->parser.partition.at( 0 );
  if ( partition.imagetype == 0 )
  {
    return {};
  }
  return mDos->card.pathOfFile( &partition.imagehandle );
}

std::uint8_t Sd2Pet::deviceAddress() const
{
  return mDos->deviceAddress;
}

Timing const& Sd2Pet::timing() const
{
  return mTiming;
}

void Sd2Pet::setTiming( Timing const& timing )
{
  mTiming = timing;
}

} // namespace pet::sd2pet
