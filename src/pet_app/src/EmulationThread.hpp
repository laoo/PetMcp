#pragma once

#include "pet/Emulator.hpp"
#include "pet/control/Dispatcher.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace pet::app
{

/// The emulator on a thread of its own (docs/architecture.md). It runs the
/// machine at the PET's speed, paced by the host's clock, and answers requests
/// between two frames, so that the interface and every transport attached to it
/// drive the one machine without touching it. Nothing outside reaches the
/// emulator but through handle().
class EmulationThread
{
public:
  /// Starts the thread.
  explicit EmulationThread( Settings settings );
  ~EmulationThread();

  EmulationThread( EmulationThread const& ) = delete;
  EmulationThread& operator=( EmulationThread const& ) = delete;
  EmulationThread( EmulationThread&& ) = delete;
  EmulationThread& operator=( EmulationThread&& ) = delete;

  /// Answers a request of the control protocol, on the emulation thread
  /// between two frames; waits for the answer. Safe from any thread.
  [[nodiscard]] control::Json handle( control::Json const& request );

  /// The dispatcher's methods, which do not change once it is made.
  [[nodiscard]] std::vector<control::Method> const& methods() const;

  /// Whether frames are not being run: the emulator's paused(), which
  /// `emu.pause` and `emu.resume` set. Requests are answered either way.
  [[nodiscard]] bool paused() const;

  /// Copies the last complete picture into `out` if it is newer than
  /// `picturesSeen`, and updates that; false if there is none newer.
  bool takePicture( std::int64_t& picturesSeen, std::vector<std::uint8_t>& out );

private:
  struct Pending
  {
    control::Json request;
    std::promise<control::Json> answer;
  };

  void loop();
  /// Answers every request waiting, on the emulation thread.
  void serveRequests();

  Emulator mEmulator;
  control::Dispatcher mDispatcher;
  std::vector<control::Method> const mMethods;

  std::mutex mMutex;
  std::condition_variable mWake;
  std::deque<Pending> mPending;
  bool mStop{};

  /// The emulator's paused(), as of the last time the thread looked.
  std::atomic<bool> mPaused{};

  std::mutex mPictureMutex;
  std::vector<std::uint8_t> mPicture;
  std::int64_t mPicturesDrawn{ -1 };

  std::thread mThread;
};

} // namespace pet::app
