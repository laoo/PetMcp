#pragma once

// The SD2PET Future on the PET's IEEE-488 port: NODISKEMU's bus handling,
// src/ieee.c and the petSD+'s ATN interrupt (src/avr/atn-ack-petsd+.S) at
// commit e51a6df, as a state machine stepped with the PET's cycles, around the
// DOS ported beside it (Dos.hpp). Its decision is
// docs/decisions/0010-sd2pet.md, and docs/hardware/sd2pet.md says how it is
// modelled.
//
// The firmware is sequential code that waits in loops on the bus's lines.
// Each of those loops is a state here; a line the loop waits for is acted on
// Timing::reaction cycles after it changes, as the firmware's poll would.
// The DOS runs whole when the firmware calls it, and the device then stays
// busy for the time Timing gives that call, as the ATmega would be, while the
// ATN interrupt still answers. Left out: the IEC bus, the LCD, the buttons,
// the UART, and the sleep the buttons put the bus in.

#include "machine/Clock.hpp"
#include "pet/machine/Machine.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace pet::machine
{
class Ieee488Bus;
}

namespace pet::sd2pet
{

struct Dos;
struct Buffer;

/// How long the SD2PET takes (Machine.hpp).
using Timing = machine::DriveTiming;

class Sd2Pet
{
public:
  /// A drive powered up on `bus`, with no card.
  explicit Sd2Pet( machine::Ieee488Bus& bus );
  ~Sd2Pet();
  Sd2Pet( Sd2Pet const& ) = delete;
  Sd2Pet& operator=( Sd2Pet const& ) = delete;
  Sd2Pet( Sd2Pet&& ) = delete;
  Sd2Pet& operator=( Sd2Pet&& ) = delete;

  /// One cycle, after the PET's access in it.
  void cycle( machine::Clock now );

  /// IFC, which the PET pulls with its reset: ieee488_ProcessIFC.
  void interfaceClear( machine::Clock now );

  /// Whether the firmware has stopped, as it does where NODISKEMU loops for
  /// ever; only a power-up starts it again.
  [[nodiscard]] bool hung() const
  {
    return mHung;
  }

  /// After a save state was loaded: the drive is not part of one, so it lets
  /// go of the bus and waits for the next ATN, its channels as they were.
  void idle( machine::Clock now );

  /// Puts in the card the directory `root` stands for; an empty path takes it
  /// out. The DOS starts afresh on it, at its root, as after a card change.
  void setCard( std::filesystem::path const& root );
  [[nodiscard]] std::filesystem::path const& card() const;

  /// The error channel's message as the PET would read it now, in ASCII,
  /// without reading it.
  [[nodiscard]] std::string status() const;

  /// Runs `command`, PETSCII, as if it had come on channel 15, at once and
  /// beside the bus. Answers the status it leaves.
  std::string command( std::string_view command );

  /// Enters the image file `image`, wherever it is, as CD of it would from
  /// its directory; with no card in, that directory goes in as the card.
  /// Answers the status it leaves.
  std::string mount( std::filesystem::path const& image );

  /// The directory current on the card, and the image mounted there, if any,
  /// as host paths.
  [[nodiscard]] std::filesystem::path directory() const;
  [[nodiscard]] std::filesystem::path image() const;

  [[nodiscard]] std::uint8_t deviceAddress() const;

  [[nodiscard]] Timing const& timing() const;
  void setTiming( Timing const& timing );

private:
  /// Where the firmware is: each loop it waits in, and the steps between.
  enum class State : std::uint8_t
  {
    /// ieee_mainloop: handle_ieee488 finds no ATN received.
    MAIN,
    /// handle_ieee488's for(;;): ready for the next command byte.
    COMMAND_READY,
    COMMAND_WAIT_DAV_LOW,
    COMMAND_ABORT,
    COMMAND_TAKE,
    COMMAND_WAIT_DAV_HIGH,
    COMMAND_DO,
    /// After a secondary address or OPEN: while (!ieee488_ATN()).
    SECONDARY_WAIT_ATN,
    SECONDARY_GO,
    OPEN_WAIT_ATN,
    OPEN_GO,
    /// ieee488_ListenLoop and ieee488_IgnoreBytes, each after a byte.
    LISTEN_BYTE,
    IGNORE_BYTE,
    /// ieee488_RxByte.
    RX_TOP,
    RX_WAIT_DAV_LOW,
    RX_TAKE,
    RX_WAIT_DAV_HIGH,
    RX_DONE,
    RX_ABORT,
    /// ieee488_TalkLoop.
    TALK_OUTER,
    TALK_BYTE,
    TALK_WAIT_NDAC_LOW,
    TALK_WAIT_NRFD_HIGH,
    TALK_SEND,
    TALK_WAIT_NRFD_LOW,
    TALK_WAIT_NDAC_HIGH,
    TALK_NEXT,
    /// Its returns on ATN: T1 and T3 with the bus idle, T2 and T4 without;
    /// T5 releasing DAV and EOI with it idle, T6 releasing them without.
    TALK_ABORT_IDLE,
    TALK_ABORT,
    TALK_ABORT_RELEASE_IDLE,
    TALK_ABORT_RELEASE
  };

  /// ieee488_RxByte's answers.
  enum class Rx : std::uint8_t
  {
    DATA,
    EOI,
    ATN
  };

  /// ieee488_ListenLoop's actions.
  enum class ListenAction : std::uint8_t
  {
    RECEIVE,
    OPEN
  };

  void apply( machine::Clock now );
  void atnInterrupt();
  void init();
  void busIdle();
  void ctrlPortsListen();
  void ctrlPortsTalk();
  [[nodiscard]] bool line( std::uint8_t which ) const;
  [[nodiscard]] std::uint8_t data() const;

  /// Runs the firmware from where it is until it waits or is busy.
  void run( machine::Clock now );
  void step( machine::Clock now );
  /// Runs a call into the DOS, and stops the drive where the firmware would
  /// stop: an endless loop, or an access past an array.
  template <class Call>
  void guarded( Call const& call );
  void hang( char const* why );
  /// Leaves `state` for `next`, Timing::reaction cycles from now: a poll
  /// that saw its line change.
  void react( State next, machine::Clock now );
  void busy( State next, machine::Clock now, machine::Clock cycles );

  void listenLoop( ListenAction action, std::uint8_t sa, machine::Clock now );
  void listenByte( machine::Clock now );
  void talkLoop( std::uint8_t sa, machine::Clock now );
  void unlisten( machine::Clock now );
  void untalk();
  void close( std::uint8_t sa, machine::Clock now );
  void rxByte( State back );
  [[nodiscard]] machine::Clock refillCost() const;

  machine::Ieee488Bus& mBus;
  std::unique_ptr<Dos> mDos;
  Timing mTiming;

  // The ATmega's pins, as ieee.c drives them: true is high, let go.
  bool mNdac{ true };
  bool mNrfd{ true };
  bool mDav{ true };
  bool mEoi{ true };
  std::uint8_t mData{};
  /// The bus drivers' directions: ieee488_TE75160 for the data lines and
  /// ieee488_TE75161 for the control lines; true to talk.
  bool mDataTalk{};
  bool mCtrlTalk{};

  /// The firmware stopped: only the ATN interrupt answers, until power-up.
  bool mHung{};
  bool mAtnReceived{};
  bool mAtnWasLow{};
  /// When the ATN interrupt runs, while one is pending.
  machine::Clock mAtnInterruptAt{};
  bool mAtnInterruptPending{};

  State mState{ State::MAIN };
  machine::Clock mWakeAt{};

  // handle_ieee488's and the loops' variables.
  std::uint8_t mListenActive{};
  std::uint8_t mTalkingDevice{};
  bool mCommandReceived{};
  bool mOpenActive{};
  std::uint8_t mOpenSa{};
  std::uint8_t mCommand{};
  std::uint8_t mSa{};
  ListenAction mListenAction{};
  Buffer* mBuffer{};
  /// ieee488_RxByte's byte and answer, and the state it answers to.
  std::uint8_t mRxChar{};
  Rx mRx{};
  State mRxBack{};
  /// ieee488_TalkLoop's byte.
  std::uint8_t mTalkChar{};
  bool mLastByte{};
};

} // namespace pet::sd2pet
