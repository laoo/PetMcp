#include "HostKeyboard.hpp"

#include "pet/control/Dispatcher.hpp"

#include <SDL3/SDL_keyboard.h>
#include <spdlog/spdlog.h>

#include <fstream>

namespace pet::app
{

namespace
{

/// The keystroke of a key named in the defaults, which are all real keys.
machine::Keystroke stroke( std::string_view key, bool shift = false )
{
  return machine::Keystroke{ .key = machine::keyNamed( key ).value_or( machine::PetKey{} ), .shift = shift };
}

/// The modifiers that choose the character a key makes on the host's layout.
constexpr SDL_Keymod CHARACTER_MODIFIERS = SDL_KMOD_SHIFT | SDL_KMOD_CAPS | SDL_KMOD_MODE | SDL_KMOD_ALT;

/// The keystroke a character typed on the host makes: a letter's case is the
/// SHIFT on the PET's key, as a PET's keyboard has it.
std::optional<machine::Keystroke> keystrokeOfHost( char32_t character )
{
  if ( character >= 'a' && character <= 'z' )
  {
    return stroke( std::string( 1, static_cast<char>( character - 'a' + 'A' ) ) );
  }
  if ( character >= 'A' && character <= 'Z' )
  {
    return stroke( std::string( 1, static_cast<char>( character ) ), true );
  }
  return machine::keystrokeFor( character );
}

} // namespace

HostKeyboard::HostKeyboard()
    : mBindings{ {
          { .name = "RETURN", .stroke = stroke( "RETURN" ), .key = SDLK_RETURN },
          { .name = "INST/DEL", .stroke = stroke( "DEL" ), .key = SDLK_BACKSPACE },
          { .name = "CLR/HOME", .stroke = stroke( "HOME" ), .key = SDLK_HOME },
          { .name = "CRSR down", .stroke = stroke( "CRSR_DOWN" ), .key = SDLK_DOWN },
          { .name = "CRSR up", .stroke = stroke( "CRSR_DOWN", true ), .key = SDLK_UP },
          { .name = "CRSR right", .stroke = stroke( "CRSR_RIGHT" ), .key = SDLK_RIGHT },
          { .name = "CRSR left", .stroke = stroke( "CRSR_RIGHT", true ), .key = SDLK_LEFT },
          { .name = "RUN/STOP", .stroke = stroke( "STOP" ), .key = SDLK_ESCAPE },
          { .name = "RVS/OFF", .stroke = stroke( "RVS" ), .key = SDLK_TAB },
          { .name = "\xe2\x86\x90 (left arrow)", .stroke = stroke( "LEFT_ARROW" ), .key = SDLK_GRAVE },
          { .name = "\xcf\x80 (pi)", .stroke = stroke( "UP_ARROW", true ), .key = SDLK_UNKNOWN },
      } }
{
}

void HostKeyboard::load( std::filesystem::path const& path )
{
  std::ifstream stream{ path };
  if ( !stream )
  {
    return;
  }
  auto const json = control::Json::parse( stream, nullptr, false );
  if ( !json.is_object() )
  {
    spdlog::warn( "{} is not a keyboard map; the defaults are used", path.string() );
    return;
  }
  for ( Binding& binding : mBindings )
  {
    std::string const name{ binding.name };
    if ( json.contains( name ) && json.at( name ).is_string() )
    {
      binding.key = SDL_GetKeyFromName( json.at( name ).get<std::string>().c_str() );
    }
  }
}

void HostKeyboard::save( std::filesystem::path const& path ) const
{
  control::Json json = control::Json::object();
  for ( Binding const& binding : mBindings )
  {
    json[std::string{ binding.name }] = binding.key == SDLK_UNKNOWN ? "" : SDL_GetKeyName( binding.key );
  }
  std::ofstream stream{ path };
  stream << json.dump( 2 ) << '\n';
  if ( !stream )
  {
    spdlog::warn( "cannot write the keyboard map to {}", path.string() );
  }
}

void HostKeyboard::handle( SDL_Event const& event, bool toPet )
{
  if ( event.type == SDL_EVENT_KEY_DOWN && mCapturing )
  {
    if ( event.key.key != SDLK_ESCAPE )
    {
      mBindings.at( *mCapturing ).key = event.key.key;
    }
    mCapturing.reset();
    return;
  }
  if ( event.type == SDL_EVENT_KEY_UP )
  {
    mHeld.erase( event.key.scancode );
    return;
  }
  if ( !toPet )
  {
    return;
  }
  if ( event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat )
  {
    // The application's own shortcuts are not the PET's.
    if ( ( event.key.mod & ( SDL_KMOD_GUI | SDL_KMOD_CTRL ) ) != 0 )
    {
      return;
    }
    SDL_Keycode const key = event.key.key == SDLK_KP_ENTER ? SDLK_RETURN : event.key.key;
    for ( Binding const& binding : mBindings )
    {
      if ( binding.key == key && key != SDLK_UNKNOWN )
      {
        machine::Keystroke held = binding.stroke;
        held.shift = held.shift || ( event.key.mod & SDL_KMOD_SHIFT ) != 0;
        mHeld[event.key.scancode] = held;
        return;
      }
    }
    // The character the key makes on the host's layout, with the modifiers
    // held. Not SDL's text input: ImGui's backend stops it whenever none of
    // its own fields wants text. Not the key event's keycode either, which
    // SDL makes without the modifiers. A printable character's keycode is its
    // code point.
    SDL_Keycode const character = SDL_GetKeyFromScancode(
        event.key.scancode, static_cast<SDL_Keymod>( event.key.mod & CHARACTER_MODIFIERS ), false );
    if ( character >= 0x20 && ( character & SDLK_SCANCODE_MASK ) == 0 )
    {
      if ( auto const held = keystrokeOfHost( static_cast<char32_t>( character ) ) )
      {
        mHeld[event.key.scancode] = *held;
      }
    }
  }
}

void HostKeyboard::releaseAll()
{
  mHeld.clear();
}

machine::KeyMatrix HostKeyboard::matrix() const
{
  machine::KeyMatrix matrix{};
  for ( auto const& [scancode, held] : mHeld )
  {
    matrix.at( held.key.row ) |= static_cast<std::uint8_t>( 1U << held.key.column );
    if ( held.shift )
    {
      matrix.at( machine::LEFT_SHIFT.row ) |= static_cast<std::uint8_t>( 1U << machine::LEFT_SHIFT.column );
    }
  }
  return matrix;
}

std::array<HostKeyboard::Binding, HostKeyboard::BINDINGS> const& HostKeyboard::bindings() const
{
  return mBindings;
}

void HostKeyboard::capture( std::size_t index )
{
  mCapturing = index;
}

std::optional<std::size_t> HostKeyboard::capturing() const
{
  return mCapturing;
}

} // namespace pet::app
