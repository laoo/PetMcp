#pragma once

#include "pet/control/Dispatcher.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace pet::app
{

/// How a window asks the emulator: a method of the control protocol and its
/// params, answered with the whole response.
using Requester = std::function<control::Json( std::string const& method, control::Json params )>;

/// The 6502: its registers, the code around it, breakpoints, and stepping.
/// Everything it shows and does goes through the protocol, as an agent's
/// requests do.
class CpuWindow
{
public:
  explicit CpuWindow( Requester request );
  void draw( bool& open );

private:
  void drawBreakpoints();

  Requester mRequest;
  std::uint16_t mNewBreakpoint{};
  std::string mLastError;
};

/// The 6502's address space as hex and characters, as memory.read reads it:
/// without a read's side effects.
class MemoryWindow
{
public:
  explicit MemoryWindow( Requester request );
  void draw( bool& open );

private:
  Requester mRequest;
  std::uint16_t mGoTo{};
  bool mScrollToGoTo{};
};

/// The history of execution: the last 65536 instructions the 6502 began, as
/// they ran, with the registers before each.
class HistoryWindow
{
public:
  /// `keep` is told the seconds the history is set to keep, to keep them for
  /// the next start.
  HistoryWindow( Requester request, std::function<void( double seconds )> keep );
  void draw( bool& open );

private:
  Requester mRequest;
  std::function<void( double )> mKeep;
  /// The seconds being typed, and whether they have been read from the
  /// emulator yet.
  double mSeconds{ 1.0 };
  bool mSecondsKnown{};
  /// The search: a PC, a text of the disassembly, either or both, and what it
  /// found, as debug.trace.find answers.
  std::uint16_t mFindPc{};
  bool mFindByPc{};
  std::array<char, 32> mFindText{};
  control::Json mFound;
  /// An index of the history to bring into view.
  std::optional<std::int64_t> mShowIndex;
  /// Whether the view keeps to the newest instruction.
  bool mFollow{ true };
};

/// The PIAs' and the VIA's registers, and the VIA's timers.
class IoWindow
{
public:
  explicit IoWindow( Requester request );
  void draw( bool& open );

private:
  Requester mRequest;
};

} // namespace pet::app
