#pragma once

// The 3032's graphics keyboard: its keys by name, where they sit in the
// 10 by 8 matrix PIA1 scans (VICE's data/PET/gtk3_grus_pos.vkm), and the
// keystrokes that type a text (docs/decisions/0014-keyboard-by-symbol.md).

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pet::machine
{

inline constexpr unsigned KEYBOARD_ROWS = 10;
inline constexpr unsigned KEYBOARD_COLUMNS = 8;

/// The keys held, a bit a key, by row, as VICE's keyarr: a set bit is a key
/// down.
using KeyMatrix = std::array<std::uint8_t, KEYBOARD_ROWS>;

struct PetKey
{
  std::uint8_t row{};
  std::uint8_t column{};

  bool operator==( PetKey const& ) const = default;
};

/// A key, with or without a SHIFT held over it.
struct Keystroke
{
  PetKey key;
  bool shift{};

  bool operator==( Keystroke const& ) const = default;
};

inline constexpr PetKey LEFT_SHIFT{ .row = 8, .column = 0 };

/// The key of that name: a character printed on it (`A`, `7`, `"`) or one of
/// RETURN, SPACE, LEFT_SHIFT, RIGHT_SHIFT, RVS, STOP, HOME, DEL, CRSR_DOWN,
/// CRSR_RIGHT, LEFT_ARROW, UP_ARROW. Letters in either case.
[[nodiscard]] std::optional<PetKey> keyNamed( std::string_view name );

/// Every key's name, by row and column.
[[nodiscard]] std::string_view nameOf( PetKey key );

/// The keystroke that makes `character` on the PET, if one does. Letters are
/// the unshifted keys whichever their case, as they make letters in the
/// graphics set; the PET's own symbols are `←`, `↑` and `π`; `\n` and `\r`
/// are RETURN.
[[nodiscard]] std::optional<Keystroke> keystrokeFor( char32_t character );

/// The keystrokes that type `text`, UTF-8: each character as keystrokeFor()
/// has it, and in braces the keys no character names, {RETURN}, {CLR},
/// {HOME}, {DEL}, {INST}, {UP}, {DOWN}, {LEFT}, {RIGHT}, {RVS}, {OFF},
/// {STOP}, {RUN}, {PI}, or a key shifted, {SHIFT-A}. Fails, naming what it
/// cannot type.
[[nodiscard]] std::expected<std::vector<Keystroke>, std::string> keystrokesFor( std::string_view text );

} // namespace pet::machine
