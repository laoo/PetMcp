#pragma once

// The PET's 6502, on floooh/chips' cycle-stepped m6502.h
// (docs/decisions/0003-cpu-core.md). One tick is one cycle of the 1 MHz clock,
// and makes the one bus access the 6502 makes in that cycle. m6502.h stays
// inside Cpu6502.cpp.

#include <cstdint>
#include <memory>

namespace pet::cpu
{

/// What the 6502's pins reach. Each access is one call, in the cycle the 6502
/// makes it.
class Bus
{
public:
  virtual std::uint8_t read( std::uint16_t address ) = 0;
  virtual void write( std::uint16_t address, std::uint8_t value ) = 0;

protected:
  Bus() = default;
  ~Bus() = default;
  Bus( Bus const& ) = default;
  Bus& operator=( Bus const& ) = default;
  Bus( Bus&& ) = default;
  Bus& operator=( Bus&& ) = default;
};

/// The programmer's registers. `p` holds the status as PHP pushes it: the
/// break flag and bit 5 set, neither of which is a flip-flop in the chip.
struct Registers
{
  std::uint16_t pc{};
  std::uint8_t a{};
  std::uint8_t x{};
  std::uint8_t y{};
  std::uint8_t s{};
  std::uint8_t p{};

  bool operator==( Registers const& ) const = default;
};

class Cpu6502
{
public:
  /// The status register's flags.
  static constexpr std::uint8_t FLAG_CARRY = 0x01;
  static constexpr std::uint8_t FLAG_ZERO = 0x02;
  static constexpr std::uint8_t FLAG_IRQ_DISABLE = 0x04;
  static constexpr std::uint8_t FLAG_DECIMAL = 0x08;
  static constexpr std::uint8_t FLAG_BREAK = 0x10;
  static constexpr std::uint8_t FLAG_UNUSED = 0x20;
  static constexpr std::uint8_t FLAG_OVERFLOW = 0x40;
  static constexpr std::uint8_t FLAG_NEGATIVE = 0x80;

  /// Powered up: the first ticks are the reset sequence, which ends with the
  /// fetch of the opcode the vector at $FFFC points to.
  Cpu6502();
  ~Cpu6502();

  Cpu6502( Cpu6502 const& ) = delete;
  Cpu6502& operator=( Cpu6502 const& ) = delete;
  Cpu6502( Cpu6502&& ) = delete;
  Cpu6502& operator=( Cpu6502&& ) = delete;

  /// Pulls RES: the reset sequence begins at the next instruction boundary.
  void reset();

  /// The level of the IRQ line, true while any source pulls it. The 6502
  /// samples it as the chip does, so a pull shorter than its sampling window
  /// is not seen.
  void setIrq( bool active );
  /// The level of the NMI line; the 6502 reacts to its edge.
  void setNmi( bool active );

  /// One cycle: the access the 6502 makes in it reaches `bus`.
  void tick( Bus& bus );

  /// Whether the last tick's access was an opcode fetch: an instruction, or
  /// an interrupt sequence in its place, begins with it.
  [[nodiscard]] bool sync() const;
  /// Whether the opcode the last tick fetched will be dropped for an
  /// interrupt or reset sequence, which begins with that fetch. The 6502 has
  /// decided it by then: it acts on what it sampled the cycle before. An
  /// instruction boundary is a sync() that is not this.
  [[nodiscard]] bool interrupting() const;
  /// The address of the last tick's access, and whether it read.
  [[nodiscard]] std::uint16_t address() const;
  [[nodiscard]] bool reading() const;
  /// The byte the last tick's access carried, read or written.
  [[nodiscard]] std::uint8_t data() const;

  /// The registers. They are the programmer's whole state only where sync()
  /// holds: then `pc` is the address of the opcode just fetched.
  [[nodiscard]] Registers registers() const;

  /// Makes `registers` the CPU's, at an instruction boundary: the opcode at
  /// `registers.pc` is fetched from `bus` at once, as that boundary's fetch
  /// cycle, and the next tick is the instruction's second cycle.
  void setRegisters( Registers const& registers, Bus& bus );

private:
  struct State;
  std::unique_ptr<State> mState;
};

} // namespace pet::cpu
