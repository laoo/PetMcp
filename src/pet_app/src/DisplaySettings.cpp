#include "DisplaySettings.hpp"

#include "pet/control/Dispatcher.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <fstream>

namespace pet::app
{

namespace
{

constexpr std::array<DisplaySettings::Preset, 3> PRESETS{ DisplaySettings::Preset::SHARP,
                                                          DisplaySettings::Preset::SCANLINES,
                                                          DisplaySettings::Preset::CRT };

/// The value `json` holds under `name`, taken to 0 to 1, or `fallback`.
float fraction( control::Json const& json, char const* name, float fallback )
{
  if ( !json.contains( name ) || !json.at( name ).is_number() )
  {
    return fallback;
  }
  return std::clamp( json.at( name ).get<float>(), 0.0F, 1.0F );
}

} // namespace

std::string_view nameOf( DisplaySettings::Preset preset )
{
  switch ( preset )
  {
  case DisplaySettings::Preset::SHARP:
    return "Sharp";
  case DisplaySettings::Preset::SCANLINES:
    return "Scanlines";
  case DisplaySettings::Preset::CRT:
    return "CRT";
  }
  return "?";
}

DisplaySettings loadDisplaySettings( std::filesystem::path const& path )
{
  DisplaySettings settings;
  std::ifstream stream{ path };
  if ( !stream )
  {
    return settings;
  }
  auto const json = control::Json::parse( stream, nullptr, false );
  if ( !json.is_object() )
  {
    spdlog::warn( "{} is not display settings; the defaults are used", path.string() );
    return settings;
  }
  if ( json.contains( "preset" ) && json.at( "preset" ).is_string() )
  {
    auto const name = json.at( "preset" ).get<std::string>();
    auto const preset = std::ranges::find( PRESETS, name, nameOf );
    if ( preset != PRESETS.end() )
    {
      settings.preset = *preset;
    }
  }
  settings.integerScale = json.value( "integer_scale", settings.integerScale );
  settings.scanlines = fraction( json, "scanlines", settings.scanlines );
  settings.curvature = fraction( json, "curvature", settings.curvature );
  settings.mask = fraction( json, "mask", settings.mask );
  return settings;
}

void saveDisplaySettings( DisplaySettings const& settings, std::filesystem::path const& path )
{
  control::Json const json{ { "preset", nameOf( settings.preset ) },
                            { "integer_scale", settings.integerScale },
                            { "scanlines", settings.scanlines },
                            { "curvature", settings.curvature },
                            { "mask", settings.mask } };
  std::ofstream stream{ path };
  stream << json.dump( 2 ) << '\n';
  if ( !stream )
  {
    spdlog::warn( "cannot write the display settings to {}", path.string() );
  }
}

} // namespace pet::app
