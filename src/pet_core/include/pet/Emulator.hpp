#pragma once

#include "pet/machine/Machine.hpp"

#include <cstdint>
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
  /// Whether the owner's expansion board fills the $9000-$BFFF sockets.
  bool expansionBoard{ true };
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

  /// Reads the ROMs from `directory` from now on, and powers the PET up with
  /// them. On failure the directory and the machine stay as they were.
  std::expected<void, std::string> setRomDirectory( std::filesystem::path const& directory );

  /// The PET, or null when it could not be powered up.
  [[nodiscard]] machine::Machine* machine();
  [[nodiscard]] machine::Machine const* machine() const;
  /// How many times a PET has been powered up: each is a new machine, whose
  /// counts begin afresh.
  [[nodiscard]] std::uint64_t powerUps() const;
  /// Why there is no machine; empty when there is one.
  [[nodiscard]] std::string const& powerUpError() const;

  /// How many cycles the history of execution keeps, this machine's and every
  /// one powered up after it.
  void setTraceCycles( std::uint64_t cycles );
  [[nodiscard]] std::uint64_t traceCycles() const;

  /// Whether the frontend's own run is paused. The emulator runs only when it
  /// is asked to; the desktop application runs it frame after frame, unless
  /// this is set.
  [[nodiscard]] bool paused() const;
  void setPaused( bool paused );

private:
  Settings mSettings;
  bool mPaused{};
  std::unique_ptr<machine::Machine> mMachine;
  std::uint64_t mPowerUps{};
  std::uint64_t mTraceCycles{ machine::Machine::DEFAULT_TRACE_CYCLES };
  std::string mPowerUpError;
};

} // namespace pet
