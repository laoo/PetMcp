#pragma once

#include <filesystem>

namespace pet
{

/// Where the emulator finds what it is asked to load. Supplied by whoever
/// starts it, so that the core never decides where to look on its own.
struct Settings
{
  /// The directory the PET's ROMs are read from, under the names VICE gives
  /// them (docs/architecture.md, "The machine").
  std::filesystem::path romDirectory;
  /// The directory that stands for the SD2PET's SD card.
  std::filesystem::path cardDirectory;
  /// Where `state.save` and `state.load` keep save states named without a
  /// directory, and `state.list` looks; the working directory when empty.
  std::filesystem::path stateDirectory;
};

/// The emulated PET as a whole: where its ROMs and its card are, and, as later
/// milestones add it, the machine that runs.
class Emulator
{
public:
  explicit Emulator( Settings settings );

  [[nodiscard]] Settings const& settings() const;

  /// Whether the frontend's own run is paused. The emulator runs only when it
  /// is asked to; the desktop application runs it frame after frame, unless
  /// this is set.
  [[nodiscard]] bool paused() const;
  void setPaused( bool paused );

private:
  Settings mSettings;
  bool mPaused{};
};

} // namespace pet
