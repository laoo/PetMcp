// Ported from VICE's src/piacore.c (GPL-2.0-or-later): Copyright (C) Jouko Valta, Andre Fachat
// and the VICE Team (VICE's README). Ported to C++ for PetMcp, 2026.

#pragma once

// The 6520 PIA, ported from VICE's src/piacore.c at the owner's VICE commit
// 167bcac (3.10), with what VICE leaves out left out: CA2 and CB2 as inputs
// raise no flags, and reading port A in handshake mode does not drive CA2.

#include "Clock.hpp"

#include <cstdint>

namespace pet::machine
{

/// What the PIA's pins reach: the store_*, read_* and pia_set_* functions a
/// VICE PIA is built with.
class PiaPorts
{
public:
  /// The levels the port's pins are driven to from outside. Where a pin is
  /// an output, the PIA reads its output register instead, as both of the
  /// PET's PIAs read port A (petpia1.c, petpia2.c) and piacore.c reads port B.
  virtual std::uint8_t readPa() = 0;
  virtual std::uint8_t readPb() = 0;
  /// The pins after a write: the output register where the pin is an output,
  /// 1 where it is an input.
  virtual void storePa( std::uint8_t byte ) = 0;
  virtual void storePb( std::uint8_t byte ) = 0;
  virtual void setCa2( bool state ) = 0;
  virtual void setCb2( bool state ) = 0;

protected:
  PiaPorts() = default;
  ~PiaPorts() = default;
  PiaPorts( PiaPorts const& ) = default;
  PiaPorts& operator=( PiaPorts const& ) = default;
  PiaPorts( PiaPorts&& ) = default;
  PiaPorts& operator=( PiaPorts&& ) = default;
};

class Pia6520
{
public:
  enum class Line : std::uint8_t
  {
    CA1,
    CB1
  };

  explicit Pia6520( PiaPorts& ports );

  /// The reset line: every register cleared, both control lines high.
  void reset( Clock now );

  /// The 6502's access of cycle `now`.
  void store( std::uint16_t address, std::uint8_t byte, Clock now );
  [[nodiscard]] std::uint8_t read( std::uint16_t address, Clock now );
  /// A register as read, without the reading's side effects.
  [[nodiscard]] std::uint8_t peek( std::uint16_t address );

  /// An edge on CA1 or CB1 at cycle `now`: rising when `high`.
  void signal( Line line, bool high, Clock now );

  /// The IRQ output (IRQA and IRQB wired together), active when true, as of
  /// cycle `clock`.
  [[nodiscard]] bool irq( Clock clock ) const;

  struct Snapshot
  {
    std::uint8_t portA{};
    std::uint8_t ddrA{};
    std::uint8_t controlA{};
    std::uint8_t portB{};
    std::uint8_t ddrB{};
    std::uint8_t controlB{};
    bool ca2{};
    bool cb2{};
  };

  [[nodiscard]] Snapshot snapshot() const;

  template <class Archive>
  void serialize( Archive& archive )
  {
    archive( mPortA );
    archive( mDdrA );
    archive( mControlA );
    archive( mPortB );
    archive( mDdrB );
    archive( mControlB );
    archive( mCaState );
    archive( mCbState );
    mIrq.serialize( archive );
  }

private:
  void updateIrq( Clock from );
  [[nodiscard]] std::uint8_t readRegister( std::uint16_t address, bool peek, Clock now );

  PiaPorts& mPorts;
  std::uint8_t mPortA{ 0xff };
  std::uint8_t mDdrA{};
  std::uint8_t mControlA{};
  std::uint8_t mPortB{ 0xff };
  std::uint8_t mDdrB{};
  std::uint8_t mControlB{};
  bool mCaState{ true };
  bool mCbState{ true };
  InterruptLine mIrq;
};

} // namespace pet::machine
