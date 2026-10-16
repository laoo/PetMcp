#pragma once

#include "pet/machine/Machine.hpp"

#include <expected>
#include <filesystem>
#include <memory>
#include <string>

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

/// The emulated PET as a whole: where its ROMs and its card are, and the
/// machine that runs.
class Emulator
{
public:
  /// Powers the PET up when `settings` name a ROM directory that holds its
  /// ROMs; otherwise there is no machine, and powerUpError() says why.
  explicit Emulator( Settings settings );

  [[nodiscard]] Settings const& settings() const;

  /// Powers a new PET up with the ROMs read again: power off and on. On
  /// failure the machine that ran runs on, and the reason is answered.
  std::expected<void, std::string> powerUp();

  /// The PET, or null when it could not be powered up.
  [[nodiscard]] machine::Machine* machine();
  [[nodiscard]] machine::Machine const* machine() const;
  /// Why there is no machine; empty when there is one.
  [[nodiscard]] std::string const& powerUpError() const;

  /// Whether the frontend's own run is paused. The emulator runs only when it
  /// is asked to; the desktop application runs it frame after frame, unless
  /// this is set.
  [[nodiscard]] bool paused() const;
  void setPaused( bool paused );

private:
  Settings mSettings;
  bool mPaused{};
  std::unique_ptr<machine::Machine> mMachine;
  std::string mPowerUpError;
};

} // namespace pet
