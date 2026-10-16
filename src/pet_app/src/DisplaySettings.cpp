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
constexpr std::array<DisplaySettings::Phosphor, 3> PHOSPHORS{ DisplaySettings::Phosphor::GREEN,
                                                              DisplaySettings::Phosphor::WHITE,
                                                              DisplaySettings::Phosphor::AMBER };
constexpr std::array<DisplaySettings::Shape, 2> SHAPES{ DisplaySettings::Shape::SQUARE,
                                                        DisplaySettings::Shape::MONITOR };

/// The setting `json` names under `name`, or `fallback`.
template <typename Setting, std::size_t N>
Setting named( control::Json const& json, char const* name, std::array<Setting, N> const& all, Setting fallback )
{
  if ( !json.contains( name ) || !json.at( name ).is_string() )
  {
    return fallback;
  }
  auto const text = json.at( name ).get<std::string>();
  auto const found = std::ranges::find_if( all, [&text]( Setting setting ) { return nameOf( setting ) == text; } );
  return found == all.end() ? fallback : *found;
}

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

std::string_view nameOf( DisplaySettings::Phosphor phosphor )
{
  switch ( phosphor )
  {
  case DisplaySettings::Phosphor::GREEN:
    return "Green";
  case DisplaySettings::Phosphor::WHITE:
    return "White";
  case DisplaySettings::Phosphor::AMBER:
    return "Amber";
  }
  return "?";
}

std::string_view nameOf( DisplaySettings::Shape shape )
{
  switch ( shape )
  {
  case DisplaySettings::Shape::SQUARE:
    return "Square dots";
  case DisplaySettings::Shape::MONITOR:
    return "Monitor, 4:3";
  }
  return "?";
}

std::array<std::uint8_t, 3> colourOf( DisplaySettings::Phosphor phosphor )
{
  switch ( phosphor )
  {
  case DisplaySettings::Phosphor::GREEN:
    return { 0x41, 0xff, 0x6a };
  case DisplaySettings::Phosphor::WHITE:
    return { 0xe8, 0xee, 0xff };
  case DisplaySettings::Phosphor::AMBER:
    return { 0xff, 0xb0, 0x00 };
  }
  return { 0xff, 0xff, 0xff };
}

float aspectOf( DisplaySettings::Shape shape )
{
  return shape == DisplaySettings::Shape::MONITOR ? 4.0F / 3.0F : 320.0F / 200.0F;
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
  settings.preset = named( json, "preset", PRESETS, settings.preset );
  settings.phosphor = named( json, "phosphor", PHOSPHORS, settings.phosphor );
  settings.shape = named( json, "shape", SHAPES, settings.shape );
  settings.integerScale = json.value( "integer_scale", settings.integerScale );
  settings.scanlines = fraction( json, "scanlines", settings.scanlines );
  settings.curvature = fraction( json, "curvature", settings.curvature );
  settings.mask = fraction( json, "mask", settings.mask );
  return settings;
}

void saveDisplaySettings( DisplaySettings const& settings, std::filesystem::path const& path )
{
  control::Json const json{ { "preset", nameOf( settings.preset ) },
                            { "phosphor", nameOf( settings.phosphor ) },
                            { "shape", nameOf( settings.shape ) },
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
