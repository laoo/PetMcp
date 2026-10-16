#pragma once

#include "pet/machine/Keyboard.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string_view>

namespace pet::app
{

/// The host's keyboard as the PET's, by symbol
/// (docs/decisions/0014-keyboard-by-symbol.md). A key that makes a character
/// presses the PET key that makes it: a lower-case letter the unshifted key,
/// a capital the shifted one, as on the PET itself. The keys that make no
/// character are bound to PET keys, and SHIFT on the host shifts them.
class HostKeyboard
{
public:
  /// A PET key a host key is bound to.
  struct Binding
  {
    /// What it is called in the Input window.
    std::string_view name;
    machine::Keystroke stroke;
    SDL_Keycode key{ SDLK_UNKNOWN };
  };

  static constexpr std::size_t BINDINGS = 11;

  /// The default bindings.
  HostKeyboard();

  /// The bindings saved at `path`, over the defaults.
  void load( std::filesystem::path const& path );
  void save( std::filesystem::path const& path ) const;

  /// Takes a keyboard event. Keys go to the PET only while `toPet`.
  void handle( SDL_Event const& event, bool toPet );
  /// Lets go of every key: when the PET's screen loses the keyboard.
  void releaseAll();

  /// The PET keys held.
  [[nodiscard]] machine::KeyMatrix matrix() const;

  [[nodiscard]] std::array<Binding, BINDINGS> const& bindings() const;
  /// The next key pressed becomes binding `index`'s; Escape keeps it as it is.
  void capture( std::size_t index );
  [[nodiscard]] std::optional<std::size_t> capturing() const;

private:
  std::array<Binding, BINDINGS> mBindings;
  /// What each host key down holds, by scancode.
  std::map<SDL_Scancode, machine::Keystroke> mHeld;
  std::optional<std::size_t> mCapturing;
};

} // namespace pet::app
