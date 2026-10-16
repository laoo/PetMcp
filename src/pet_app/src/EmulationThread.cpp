#include "EmulationThread.hpp"

#include <chrono>
#include <utility>

namespace pet::app
{

namespace
{

/// How long the thread sleeps at most while it has nothing to run, so that it
/// notices a pause lifted.
constexpr std::chrono::milliseconds IDLE_WAIT{ 20 };

control::Json shuttingDown()
{
  return control::Json{ { "id", 0 },
                        { "ok", false },
                        { "error", { { "code", "shutting_down" }, { "message", "The emulator is closing" } } } };
}

} // namespace

EmulationThread::EmulationThread( Settings settings )
    : mEmulator{ std::move( settings ) }, mDispatcher{ mEmulator }, mMethods{ mDispatcher.methods() },
      mThread{ [this] { loop(); } }
{
}

EmulationThread::~EmulationThread()
{
  {
    std::scoped_lock const lock{ mMutex };
    mStop = true;
  }
  mWake.notify_all();
  mThread.join();
}

std::vector<control::Method> const& EmulationThread::methods() const
{
  return mMethods;
}

control::Json EmulationThread::handle( control::Json const& request )
{
  std::future<control::Json> answer;
  {
    std::scoped_lock const lock{ mMutex };
    if ( mStop )
    {
      return shuttingDown();
    }
    mPending.push_back( Pending{ .request = request, .answer = {} } );
    answer = mPending.back().answer.get_future();
  }
  mWake.notify_all();
  return answer.get();
}

bool EmulationThread::paused() const
{
  return mPaused;
}

bool EmulationThread::takePicture( std::int64_t& picturesSeen, std::vector<std::uint8_t>& out )
{
  std::scoped_lock const lock{ mPictureMutex };
  if ( mPicturesDrawn == picturesSeen || mPicture.empty() )
  {
    return false;
  }
  out = mPicture;
  picturesSeen = mPicturesDrawn;
  return true;
}

void EmulationThread::serveRequests()
{
  for ( ;; )
  {
    Pending pending;
    {
      std::scoped_lock const lock{ mMutex };
      if ( mPending.empty() )
      {
        return;
      }
      pending = std::move( mPending.front() );
      mPending.pop_front();
    }
    pending.answer.set_value( mDispatcher.handle( pending.request ) );
  }
}

void EmulationThread::loop()
{
  for ( ;; )
  {
    serveRequests();
    {
      std::scoped_lock const lock{ mMutex };
      if ( mStop )
      {
        break;
      }
    }
    mPaused = mEmulator.paused();

    std::unique_lock lock{ mMutex };
    mWake.wait_for( lock, IDLE_WAIT, [this] { return mStop || !mPending.empty(); } );
  }

  // Whoever is still waiting is told why no answer comes.
  std::scoped_lock const lock{ mMutex };
  for ( Pending& pending : mPending )
  {
    pending.answer.set_value( shuttingDown() );
  }
  mPending.clear();
}

} // namespace pet::app
