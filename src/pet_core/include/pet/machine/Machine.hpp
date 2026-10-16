#pragma once

#include "pet/machine/Keyboard.hpp"
#include "pet/machine/Roms.hpp"

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace pet::machine
{

/// The 6502's programmer's registers, at an instruction boundary. `p` holds
/// the status as PHP pushes it.
struct CpuState
{
  std::uint16_t pc{};
  std::uint8_t a{};
  std::uint8_t x{};
  std::uint8_t y{};
  std::uint8_t s{};
  std::uint8_t p{};
};

enum class StopReason : std::uint8_t
{
  COMPLETED,
  CONDITION,
  TIMEOUT,
  BREAKPOINT,
  WATCHPOINT
};

/// The accesses a watchpoint watches.
enum class Access : std::uint8_t
{
  READ = 1,
  WRITE = 2,
  BOTH = 3
};

/// A range of addresses whose data accesses stop a run.
struct Watchpoint
{
  std::uint16_t address{};
  std::uint32_t size{ 1 };
  Access access{ Access::WRITE };
};

/// The access that set a watchpoint off, and the instruction that made it.
struct WatchHit
{
  std::uint16_t address{};
  bool write{};
  std::uint8_t value{};
  std::uint16_t pc{};
};

struct RunResult
{
  StopReason reason{};
  std::uint64_t cycles{};
  std::uint64_t frames{};
  /// With BREAKPOINT, the address; with WATCHPOINT, the access.
  std::uint16_t breakpoint{};
  WatchHit watchpoint{};
};

/// One stereo sample of the PET's sound, at Machine::AUDIO_RATE.
struct AudioFrame
{
  std::int16_t left{};
  std::int16_t right{};
};

/// The expansion board's state as a debugger shows it.
struct BoardState
{
  std::array<std::array<std::uint8_t, 16>, 2> registers{};
  /// The register each chip's address latch selects.
  std::array<std::uint8_t, 2> latches{};
  /// The 4 KB banks of RAM0 and RAM1 at $A000 and $B000.
  std::array<unsigned, 2> banks{};
  /// Whether the video draws the ROM's characters, else the RAM's buffer.
  bool romCharacters{};
  unsigned shownBuffer{};
  /// Each chip at 2 MHz, its divider off, or at 1 MHz.
  std::array<bool, 2> fullClock{};
};

/// A PIA's registers as written, and its control lines' outputs.
struct PiaState
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

/// The VIA's registers as written, and its timers as they count.
struct ViaState
{
  std::array<std::uint8_t, 16> registers{};
  std::uint16_t t1{};
  std::uint16_t t1Latch{};
  std::uint16_t t2{};
  std::uint8_t ifr{};
  std::uint8_t ier{};
};

struct IoState
{
  PiaState pia1;
  PiaState pia2;
  ViaState via;
  /// The IRQ line, pulled by any of them.
  bool irq{};
};

/// An instruction the 6502 began: where, in which cycle, the registers before
/// it, and its bytes as they were when it ran.
struct TracedInstruction
{
  std::uint64_t cycle{};
  CpuState registers{};
  std::array<std::uint8_t, 3> bytes{};
};

/// The PET 3032: the 6502, 32 KB of RAM, the video, the PIAs and the VIA,
/// stepped a cycle at a time (docs/decisions/0009-stepped-a-cycle-at-a-time.md).
/// Every run ends at an instruction boundary, where the registers are whole.
class Machine
{
public:
  /// A machine powered up with `roms`: RAM in VICE's pattern, every chip as
  /// after power-up, the 6502 about to run its reset sequence. With
  /// `expansionBoard`, the owner's board fills the $9000-$BFFF sockets
  /// (docs/hardware/expansion-board.md); without, they are empty.
  explicit Machine( Roms roms, bool expansionBoard = true );

  static constexpr unsigned AUDIO_RATE = 48000;

  /// The sound the last run made.
  [[nodiscard]] std::span<AudioFrame const> audio() const;
  /// Keeps every frame of sound made from now on, until stopCapture() hands
  /// them over.
  void startCapture();
  [[nodiscard]] bool capturing() const;
  std::vector<AudioFrame> stopCapture();

  /// The expansion board's state; nothing without the board.
  [[nodiscard]] std::optional<BoardState> board() const;
  /// The board's memories: 0 and 1 its 512 KB RAMs, 2 its 4 KB of character
  /// RAM, both buffers. Empty without the board.
  [[nodiscard]] std::span<std::uint8_t const> boardMemory( unsigned which ) const;
  ~Machine();

  Machine( Machine const& ) = delete;
  Machine& operator=( Machine const& ) = delete;
  Machine( Machine&& ) = delete;
  Machine& operator=( Machine&& ) = delete;

  /// Pulls the reset line, which reaches the 6502, the PIAs and the VIA; RAM
  /// and the video's counters are left alone. The 6502 resets at its next
  /// instruction boundary.
  void reset();

  /// Runs at least `count` cycles, to the next instruction boundary.
  RunResult runCycles( std::uint64_t count );
  /// Runs through `count` frame boundaries, where the vertical blank begins,
  /// to the next instruction boundary.
  RunResult runFrames( std::uint64_t count );
  /// Runs until `condition` holds at an instruction boundary, or for
  /// `timeoutCycles`.
  RunResult runUntil( std::function<bool( Machine const& )> const& condition, std::uint64_t timeoutCycles );

  /// Runs `count` instructions, stopping before at a breakpoint or after a
  /// watched access.
  RunResult step( std::uint64_t count );

  /// Breakpoints: a run stops before the instruction at their address. The
  /// instruction the PET stands at runs when it runs on.
  void addBreakpoint( std::uint16_t address );
  bool removeBreakpoint( std::uint16_t address );
  [[nodiscard]] std::vector<std::uint16_t> breakpoints() const;

  /// Watchpoints, one per address: a run stops after the instruction whose
  /// data access, a read or a write the instruction makes beyond fetching
  /// itself, falls in one's range. Dummy accesses count, as they reach the
  /// bus.
  void addWatchpoint( Watchpoint watchpoint );
  bool removeWatchpoint( std::uint16_t address );
  [[nodiscard]] std::vector<Watchpoint> watchpoints() const;

  /// The history of execution: every instruction begun in the last
  /// traceCycles() cycles of the PET's time. traceLength() are kept; trace()
  /// answers `count` of them from the `start`th, 0 being the oldest kept.
  /// A second's worth is some 300,000 instructions, of 16 bytes each.
  static constexpr std::uint64_t DEFAULT_TRACE_CYCLES = 1'000'000;
  void setTraceCycles( std::uint64_t cycles );
  [[nodiscard]] std::uint64_t traceCycles() const;
  [[nodiscard]] std::size_t traceLength() const;
  [[nodiscard]] std::vector<TracedInstruction> trace( std::size_t start, std::size_t count ) const;
  /// The `index`th kept, 0 being the oldest; it must be below traceLength().
  [[nodiscard]] TracedInstruction tracedAt( std::size_t index ) const;

  /// The I/O chips' registers, read without side effects.
  [[nodiscard]] IoState io() const;

  /// How the last run ended.
  [[nodiscard]] RunResult const& lastRun() const;

  /// The whole machine's state, all a run changes, without the ROMs, the
  /// debugging aids or the host's keys.
  [[nodiscard]] std::vector<std::uint8_t> saveState() const;
  /// Restores a state saveState() made of a machine with these ROMs, by this
  /// build. On failure the machine is as it was.
  std::expected<void, std::string> loadState( std::span<std::uint8_t const> state );

  /// Called at every instruction boundary from now on, before the instruction
  /// runs; empty to stop. For traces; it slows the run.
  void setInstructionHook( std::function<void( Machine const& )> hook );

  /// Cycles since power-up, and frame boundaries passed.
  [[nodiscard]] std::uint64_t now() const;
  [[nodiscard]] std::uint64_t frame() const;

  [[nodiscard]] CpuState cpu() const;

  /// A byte as the 6502 would read it at `address`, without the read's side
  /// effects; an address nothing answers gives what the bus last carried.
  [[nodiscard]] std::uint8_t peek( std::uint16_t address ) const;
  /// Writes RAM or video RAM as a debugger does: no bus cycle is made. False
  /// where `address` is neither.
  bool poke( std::uint16_t address, std::uint8_t value );

  /// Holds `key` down, or lets it go: the keys the control protocol holds.
  void setKey( PetKey key, bool down );
  /// Lets go every key setKey() holds.
  void releaseKeys();
  /// The keys setKey() holds.
  [[nodiscard]] KeyMatrix heldKeys() const;
  /// The keys the host's keyboard holds, which the PET sees together with
  /// those setKey() holds.
  void setHostKeys( KeyMatrix const& keys );

  [[nodiscard]] std::span<std::uint8_t const> ram() const;
  [[nodiscard]] std::span<std::uint8_t const> videoRam() const;
  [[nodiscard]] Roms const& roms() const;

  /// The last complete picture, SCREEN_WIDTH by SCREEN_HEIGHT RGBA, and how
  /// many have been completed since power-up.
  [[nodiscard]] std::span<std::uint8_t const> picture() const;
  [[nodiscard]] std::int64_t picturesDrawn() const;
  /// Whether the video shows the text set, VIA CA2 high, rather than the
  /// graphics set.
  [[nodiscard]] bool textSet() const;

private:
  void completed( RunResult& result );

  struct Board;
  std::unique_ptr<Board> mBoard;
};

} // namespace pet::machine
