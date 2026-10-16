#pragma once

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

  Preset preset{ Preset::SHARP };
  /// Only whole multiples of the screen's height, or as large as fits.
  bool integerScale{ true };
  /// Each 0 to 1: how dark the scanlines are, how curved the tube, how strong
  /// the mask.
  float scanlines{ 0.6F };
  float curvature{ 0.5F };
  float mask{ 0.5F };
};

/// The name of `preset` as the interface shows it.
[[nodiscard]] std::string_view nameOf( DisplaySettings::Preset preset );

/// The settings saved at `path`, or the defaults where there are none to read.
[[nodiscard]] DisplaySettings loadDisplaySettings( std::filesystem::path const& path );
void saveDisplaySettings( DisplaySettings const& settings, std::filesystem::path const& path );

} // namespace pet::app
