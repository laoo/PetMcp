#pragma once

// A YM2149F, ported from the owner's VICE commit 167bcac (src/pet/petay.c),
// which models its tone, noise and envelope generators and its 32-level DAC
// (Peter Sovietov's measurements). The registers keep all 8 bits written, as a
// YM2149's do, and read back so; the generators use the bits each register
// has. The I/O ports are pulled up inside the chip, so a port set as an input
// shows the board $FF (docs/hardware/expansion-board.md).

#include <array>
#include <cstdint>

namespace pet::machine
{

class Ym2149
{
public:
  /// Powered up: every register 0, both ports inputs.
  Ym2149();

  /// The bus side: the address latch, and the register it selects.
  void latchAddress( std::uint8_t value );
  void writeData( std::uint8_t value );
  [[nodiscard]] std::uint8_t readData() const;

  /// The levels the board sees on port A and port B.
  [[nodiscard]] std::uint8_t portA() const;
  [[nodiscard]] std::uint8_t portB() const;

  /// Runs `clocks` of the chip's own clock, 2 MHz or halved.
  void clock( unsigned clocks );

  /// The three channels' DAC outputs summed, 0 to 3, averaged over the ticks
  /// since the last call; the last value when there were none.
  [[nodiscard]] double takeAverage();

  [[nodiscard]] std::array<std::uint8_t, 16> const& registers() const;
  [[nodiscard]] std::uint8_t selected() const;

  template <class Archive>
  void serialize( Archive& archive )
  {
    archive( mRegisters );
    archive( mLatch );
    archive( mSelected );
    archive( mClocks );
    archive( mToneCount );
    archive( mToneOut );
    archive( mNoiseCount );
    archive( mNoisePrescale );
    archive( mRng );
    archive( mEnvelopeCount );
    archive( mEnvelopeStep );
    archive( mEnvelopeAttack );
    archive( mEnvelopeAlternate );
    archive( mEnvelopeHold );
    archive( mEnvelopeHolding );
    archive( mSum );
    archive( mTicks );
    archive( mLast );
  }

private:
  /// Eight clocks: the generators' step.
  void tick();
  [[nodiscard]] double output() const;
  void setEnvelopeShape( std::uint8_t shape );
  [[nodiscard]] unsigned period( unsigned low ) const;

  std::array<std::uint8_t, 16> mRegisters{};
  std::uint8_t mLatch{};
  /// The latched address had A4-A7 clear.
  bool mSelected{ true };
  unsigned mClocks{};
  std::array<unsigned, 3> mToneCount{};
  std::array<std::uint8_t, 3> mToneOut{};
  unsigned mNoiseCount{};
  std::uint8_t mNoisePrescale{};
  std::uint32_t mRng{ 1 };
  unsigned mEnvelopeCount{};
  int mEnvelopeStep{ 0x1f };
  int mEnvelopeAttack{};
  bool mEnvelopeAlternate{};
  bool mEnvelopeHold{ true };
  bool mEnvelopeHolding{};
  double mSum{};
  unsigned mTicks{};
  double mLast{};
};

} // namespace pet::machine
