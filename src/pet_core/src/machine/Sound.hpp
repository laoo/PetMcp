#pragma once

// The PET's sound, VIA CB2, as VICE's src/pet/petsound.c filters it, mixed
// with the expansion board's two YM2149Fs as the owner's VICE commit 167bcac
// (src/pet/petay.c) does: stereo at 48 kHz, CB2 on both channels, AY1 on the
// left and AY2 on the right.

#include "Clock.hpp"
#include "pet/machine/Machine.hpp"

#include <vector>

namespace pet::machine
{

class ExpansionBoard;

class Sound
{
public:
  static constexpr unsigned RATE = 48000;

  Sound();

  /// CB2 driven to `level`. The PET makes no sound while the VIA's shift
  /// register runs from a T2 of 0 (petvia.c, SOUND_ACTIVE): `active` false.
  void setCb2( bool level );
  void setActive( bool active );

  /// A cycle: the filter follows CB2, and a frame is made when one is due.
  /// `board` is null without the expansion board.
  void cycle( ExpansionBoard* board );

  /// The frames made since the last call.
  [[nodiscard]] std::vector<AudioFrame>& frames();

  template <class Archive>
  void serialize( Archive& archive )
  {
    archive( mActive );
    archive( mCb2 );
    archive( mLowpass );
    archive( mHighpass );
    archive( mPhase );
    archive( mAyIn );
    archive( mAyOut );
  }

private:
  double mCb2LowpassAlpha;
  double mCb2HighpassAlpha;
  double mAyHighpass;
  bool mActive{};
  /// CB2 as the sound sees it: low while the sound is off.
  bool mCb2{};
  double mLowpass{};
  double mHighpass{};
  unsigned mPhase{};
  std::array<double, 2> mAyIn{};
  std::array<double, 2> mAyOut{};
  std::vector<AudioFrame> mFrames;
};

} // namespace pet::machine
