#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace pet::app
{

/// How the emulated screen is drawn: through which shader, and at what size.
struct DisplaySettings
{
  enum class Preset : std::uint8_t
  {
    /// Each pixel a block of its colour.
    SHARP,
    /// Sharp pixels, each line darkening towards its edges.
    SCANLINES,
    /// A curved tube, its lines drawn by a beam, through a mask of stripes.
    CRT
  };

  /// The colour the PET's dots are lit in: its monitor's phosphor.
  enum class Phosphor : std::uint8_t
  {
    /// The 3032's own, P39's.
    GREEN,
    /// The 2001's.
    WHITE,
    AMBER
  };

  /// How the 320 by 200 dots are shaped on screen.
  enum class Shape : std::uint8_t
  {
    /// Square dots, 8:5.
    SQUARE,
    /// The text filling a 4:3 monitor, the dots taller than wide.
    MONITOR
  };

  Preset preset{ Preset::SHARP };
  Phosphor phosphor{ Phosphor::GREEN };
  Shape shape{ Shape::SQUARE };
  /// Only whole multiples of the screen's height, or as large as fits.
  bool integerScale{ true };
  /// Each 0 to 1: how dark the scanlines are, how curved the tube, how strong
  /// the mask.
  float scanlines{ 0.6F };
  float curvature{ 0.5F };
  float mask{ 0.5F };
};

/// The name of a setting as the interface shows it.
[[nodiscard]] std::string_view nameOf( DisplaySettings::Preset preset );
[[nodiscard]] std::string_view nameOf( DisplaySettings::Phosphor phosphor );
[[nodiscard]] std::string_view nameOf( DisplaySettings::Shape shape );

/// The colour a lit dot is drawn in, RGB.
[[nodiscard]] std::array<std::uint8_t, 3> colourOf( DisplaySettings::Phosphor phosphor );

/// The picture's width over its height on screen.
[[nodiscard]] float aspectOf( DisplaySettings::Shape shape );

/// The settings saved at `path`, or the defaults where there are none to read.
[[nodiscard]] DisplaySettings loadDisplaySettings( std::filesystem::path const& path );
void saveDisplaySettings( DisplaySettings const& settings, std::filesystem::path const& path );

} // namespace pet::app
