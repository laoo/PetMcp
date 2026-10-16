#pragma once

// The 6522 VIA, ported from VICE's src/core/viacore.c at the owner's VICE
// commit 167bcac (3.10). VICE computes the timers lazily from the clock and
// keeps five alarms; here the alarms are clocks the machine reaches cycle by
// cycle (docs/decisions/0009-stepped-a-cycle-at-a-time.md), run after the
// 6502's access of their cycle, as VICE runs them. What VICE leaves out is
// left out too: the input latches (MYVIA_NEED_LATCHING), PB6 pulse counting,
// and shifting out under CB1.

#include "Clock.hpp"

#include <array>
#include <cstdint>

namespace pet::machine
{

/// What the VIA's pins reach: VICE's via_context_t callbacks.
class ViaPorts
{
public:
  /// The pins of port A and B as inputs; `peek` reads without side effects.
  virtual std::uint8_t readPra( bool peek ) = 0;
  virtual std::uint8_t readPrb( bool peek ) = 0;
  /// The ports' pins after a write: the output register where the pin is an
  /// output, 1 where it is an input.
  virtual void storePra( std::uint8_t byte ) = 0;
  virtual void storePrb( std::uint8_t byte ) = 0;
  virtual void setCa2( bool state ) = 0;
  /// CB2 driven to `state` at cycle `at`, by the PCR or by the shift register.
  virtual void setCb2( bool state, Clock at ) = 0;

  /// The shift register's control in the ACR, and the low byte of T2's latch:
  /// what the PET's sound listens to (petvia.c, store_acr and store_t2l).
  virtual void storeAcr( std::uint8_t /*acr*/ ) {}

  virtual void storeT2l( std::uint8_t /*latch*/ ) {}

protected:
  ViaPorts() = default;
  ~ViaPorts() = default;
  ViaPorts( ViaPorts const& ) = default;
  ViaPorts& operator=( ViaPorts const& ) = default;
  ViaPorts( ViaPorts&& ) = default;
  ViaPorts& operator=( ViaPorts&& ) = default;
};

class Via6522
{
public:
  enum class Line : std::uint8_t
  {
    CA1,
    CA2,
    CB1,
    CB2
  };

  /// The registers' addresses, A0 to A3.
  static constexpr std::uint8_t PRB = 0;
  static constexpr std::uint8_t PRA = 1;
  static constexpr std::uint8_t DDRB = 2;
  static constexpr std::uint8_t DDRA = 3;
  static constexpr std::uint8_t T1CL = 4;
  static constexpr std::uint8_t T1CH = 5;
  static constexpr std::uint8_t T1LL = 6;
  static constexpr std::uint8_t T1LH = 7;
  static constexpr std::uint8_t T2CL = 8;
  static constexpr std::uint8_t T2CH = 9;
  static constexpr std::uint8_t SR = 10;
  static constexpr std::uint8_t ACR = 11;
  static constexpr std::uint8_t PCR = 12;
  static constexpr std::uint8_t IFR = 13;
  static constexpr std::uint8_t IER = 14;
  static constexpr std::uint8_t PRA_NHS = 15;

  /// Powered up, as viacore_setup_context leaves it: registers zero, but the
  /// timers' and their latches' as a VIC-20's read.
  explicit Via6522( ViaPorts& ports );

  /// The reset line: every register but the timers, their latches and the
  /// shift register cleared (viacore_reset).
  void reset( Clock now );

  /// The 6502's access of cycle `now`.
  void store( std::uint16_t address, std::uint8_t byte, Clock now );
  [[nodiscard]] std::uint8_t read( std::uint16_t address, Clock now );
  /// A register as read, without the reading's side effects.
  [[nodiscard]] std::uint8_t peek( std::uint16_t address, Clock now );

  /// An edge on an input line at cycle `now`: rising when `high`.
  void signal( Line line, bool high, Clock now );

  /// Runs what is due at cycle `now`, after the 6502's access of that cycle.
  void cycle( Clock now );

  /// The IRQ output, active when true, as of cycle `clock`.
  [[nodiscard]] bool irq( Clock clock ) const;

  /// The registers as a debugger shows them: the port and control registers
  /// as written, the timers as counting at `now`.
  struct Snapshot
  {
    std::array<std::uint8_t, 16> registers{};
    std::uint16_t t1{};
    std::uint16_t t1Latch{};
    std::uint16_t t2{};
    std::uint8_t ifr{};
    std::uint8_t ier{};
  };

  [[nodiscard]] Snapshot snapshot( Clock now );

  template <class Archive>
  void serialize( Archive& archive )
  {
    archive( mVia );
    archive( mIfr );
    archive( mIer );
    archive( mTal );
    archive( mT2cl );
    archive( mT2ch );
    archive( mT1reload );
    archive( mT2zero );
    archive( mT1zero );
    archive( mT2xx00 );
    archive( mT1pb7 );
    archive( mOldpa );
    archive( mOldpb );
    archive( mCa2OutState );
    archive( mCb1InState );
    archive( mCb1OutState );
    archive( mCb2InState );
    archive( mCb2OutState );
    archive( mCb1IsInput );
    archive( mCb2IsInput );
    archive( mShiftState );
    archive( mT2IrqAllowed );
    archive( mLastRead );
    mIrq.serialize( archive );
    archive( mT1ZeroAlarm );
    archive( mT2ZeroAlarm );
    archive( mT2UnderflowAlarm );
    archive( mT2ShiftAlarm );
    archive( mPhi2SrAlarm );
  }

private:
  /// One of VICE's alarms: due at a cycle, or not set.
  struct Alarm
  {
    Clock at{};
    bool set{};

    void schedule( Clock clock )
    {
      at = clock;
      set = true;
    }

    void unset()
    {
      set = false;
    }
  };

  [[nodiscard]] std::uint16_t t1( Clock rclk ) const;
  [[nodiscard]] std::uint16_t t2( Clock rclk ) const;
  void updateT1Latch( Clock rclk );
  void updateIrq( Clock rclk );
  void scheduleT2ZeroAlarm( Clock rclk );
  void setupShifting( Clock rclk );
  void setCb2OutputState( std::uint8_t pcr, Clock at );
  void cacheCb12IoStatus();
  void setCb1( bool data, Clock now );
  void setCb2( bool data, Clock now );

  void t1ZeroAlarm( Clock rclk );
  void t2ZeroAlarm( Clock rclk );
  void t2UnderflowAlarm( Clock rclk );
  void t2ShiftAlarm( Clock rclk );
  void phi2SrAlarm( Clock rclk );
  void doShiftRegister( Clock rclk );

  [[nodiscard]] bool isCa2Handshake() const;
  [[nodiscard]] bool isCa2PulseMode() const;
  [[nodiscard]] bool isCa2ToggleMode() const;
  [[nodiscard]] bool isCa2IndInput() const;
  [[nodiscard]] bool isCb2Handshake() const;
  [[nodiscard]] bool isCb2PulseMode() const;
  [[nodiscard]] bool isCb2ToggleMode() const;
  [[nodiscard]] bool isSrFreeRunning() const;

  ViaPorts& mPorts;
  std::array<std::uint8_t, 16> mVia{};
  std::uint8_t mIfr{};
  std::uint8_t mIer{};
  /// T1's latch.
  unsigned mTal{};
  std::uint8_t mT2cl{};
  std::uint8_t mT2ch{};
  /// When T1 reloads from its latch, and reads its value again.
  Clock mT1reload{};
  /// When T2 reaches, or last reached, 0000 or xx00.
  Clock mT2zero{};
  /// When the T1 alarm sets the T1 flag; 0 when it will not.
  Clock mT1zero{};
  /// T2 is in 8-bit mode, or its next 0000 should interrupt.
  bool mT2xx00{};
  std::uint8_t mT1pb7{};
  std::uint8_t mOldpa{};
  std::uint8_t mOldpb{};
  bool mCa2OutState{};
  bool mCb1InState{ true };
  bool mCb1OutState{};
  bool mCb2InState{ true };
  bool mCb2OutState{};
  bool mCb1IsInput{};
  bool mCb2IsInput{};
  std::uint8_t mShiftState{};
  bool mT2IrqAllowed{};
  std::uint8_t mLastRead{};
  InterruptLine mIrq;

  Alarm mT1ZeroAlarm;
  Alarm mT2ZeroAlarm;
  Alarm mT2UnderflowAlarm;
  Alarm mT2ShiftAlarm;
  Alarm mPhi2SrAlarm;
};

} // namespace pet::machine
