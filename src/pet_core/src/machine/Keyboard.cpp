#include "pet/machine/Keyboard.hpp"

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <cctype>

namespace pet::machine
{

namespace
{

/// The graphics keyboard's matrix, row by row, as VICE's keymap draws it;
/// empty where no key is.
constexpr std::array<std::array<std::string_view, KEYBOARD_COLUMNS>, KEYBOARD_ROWS> MATRIX{ {
    { "!", "#", "%", "&", "(", "LEFT_ARROW", "HOME", "CRSR_RIGHT" },
    { "\"", "$", "'", "\\", ")", "", "CRSR_DOWN", "DEL" },
    { "Q", "E", "T", "U", "O", "UP_ARROW", "7", "9" },
    { "W", "R", "Y", "I", "P", "", "8", "/" },
    { "A", "D", "G", "J", "L", "", "4", "6" },
    { "S", "F", "H", "K", ":", "", "5", "*" },
    { "Z", "C", "B", "M", ";", "RETURN", "1", "3" },
    { "X", "V", "N", ",", "?", "", "2", "+" },
    { "LEFT_SHIFT", "@", "]", "", ">", "RIGHT_SHIFT", "0", "-" },
    { "RVS", "[", "SPACE", "<", "STOP", "", ".", "=" },
} };

/// A key named here, which MATRIX has.
PetKey keyOf( std::string_view name )
{
  return keyNamed( name ).value_or( PetKey{} );
}

/// The keys in braces, and what they type.
struct Named
{
  std::string_view name;
  Keystroke stroke;
};

std::array<Named, 14> namedKeystrokes()
{
  return { {
      { .name = "RETURN", .stroke = { .key = keyOf( "RETURN" ), .shift = false } },
      { .name = "HOME", .stroke = { .key = keyOf( "HOME" ), .shift = false } },
      { .name = "CLR", .stroke = { .key = keyOf( "HOME" ), .shift = true } },
      { .name = "DEL", .stroke = { .key = keyOf( "DEL" ), .shift = false } },
      { .name = "INST", .stroke = { .key = keyOf( "DEL" ), .shift = true } },
      { .name = "DOWN", .stroke = { .key = keyOf( "CRSR_DOWN" ), .shift = false } },
      { .name = "UP", .stroke = { .key = keyOf( "CRSR_DOWN" ), .shift = true } },
      { .name = "RIGHT", .stroke = { .key = keyOf( "CRSR_RIGHT" ), .shift = false } },
      { .name = "LEFT", .stroke = { .key = keyOf( "CRSR_RIGHT" ), .shift = true } },
      { .name = "RVS", .stroke = { .key = keyOf( "RVS" ), .shift = false } },
      { .name = "OFF", .stroke = { .key = keyOf( "RVS" ), .shift = true } },
      { .name = "STOP", .stroke = { .key = keyOf( "STOP" ), .shift = false } },
      { .name = "RUN", .stroke = { .key = keyOf( "STOP" ), .shift = true } },
      { .name = "PI", .stroke = { .key = keyOf( "UP_ARROW" ), .shift = true } },
  } };
}

/// The next character of UTF-8 `text` at `at`, which it moves past; a byte
/// that starts no valid sequence is taken as itself.
char32_t nextCharacter( std::string_view text, std::size_t& at )
{
  auto const byte = static_cast<unsigned char>( text[at++] );
  int extra = 0;
  char32_t value = byte;
  if ( ( byte & 0xe0U ) == 0xc0 )
  {
    extra = 1;
    value = byte & 0x1fU;
  }
  else if ( ( byte & 0xf0U ) == 0xe0 )
  {
    extra = 2;
    value = byte & 0x0fU;
  }
  else if ( ( byte & 0xf8U ) == 0xf0 )
  {
    extra = 3;
    value = byte & 0x07U;
  }
  for ( ; extra > 0 && at < text.size(); --extra )
  {
    value = ( value << 6U ) | ( static_cast<unsigned char>( text[at++] ) & 0x3fU );
  }
  return value;
}

} // namespace

std::optional<PetKey> keyNamed( std::string_view name )
{
  std::string upper{ name };
  if ( upper.size() == 1 )
  {
    upper[0] = static_cast<char>( std::toupper( static_cast<unsigned char>( upper[0] ) ) );
  }
  for ( std::size_t row = 0; row < MATRIX.size(); ++row )
  {
    auto const& columns = MATRIX.at( row );
    auto const found = std::ranges::find( columns, std::string_view{ upper } );
    if ( !upper.empty() && found != columns.end() )
    {
      return PetKey{ .row = static_cast<std::uint8_t>( row ),
                     .column = static_cast<std::uint8_t>( found - columns.begin() ) };
    }
  }
  return std::nullopt;
}

std::string_view nameOf( PetKey key )
{
  return MATRIX.at( key.row ).at( key.column );
}

std::optional<Keystroke> keystrokeFor( char32_t character )
{
  if ( character == '\n' || character == '\r' )
  {
    return Keystroke{ .key = keyOf( "RETURN" ), .shift = false };
  }
  if ( character == ' ' )
  {
    return Keystroke{ .key = keyOf( "SPACE" ), .shift = false };
  }
  if ( character == U'←' || character == '_' )
  {
    return Keystroke{ .key = keyOf( "LEFT_ARROW" ), .shift = false };
  }
  if ( character == U'↑' || character == '^' )
  {
    return Keystroke{ .key = keyOf( "UP_ARROW" ), .shift = false };
  }
  if ( character == U'π' )
  {
    return Keystroke{ .key = keyOf( "UP_ARROW" ), .shift = true };
  }
  if ( character > 0x20 && character < 0x7f )
  {
    if ( auto const key = keyNamed( std::string( 1, static_cast<char>( character ) ) ) )
    {
      return Keystroke{ .key = *key, .shift = false };
    }
  }
  return std::nullopt;
}

std::expected<std::vector<Keystroke>, std::string> keystrokesFor( std::string_view text )
{
  std::vector<Keystroke> strokes;
  std::size_t at = 0;
  while ( at < text.size() )
  {
    if ( text[at] == '{' )
    {
      std::size_t const close = text.find( '}', at );
      if ( close == std::string_view::npos )
      {
        return std::unexpected( fmt::format( "a {{ at {} is not closed", at ) );
      }
      std::string name{ text.substr( at + 1, close - at - 1 ) };
      std::ranges::transform( name, name.begin(), []( unsigned char c ) { return std::toupper( c ); } );
      at = close + 1;
      if ( name.starts_with( "SHIFT-" ) )
      {
        auto const key = keyNamed( std::string_view{ name }.substr( 6 ) );
        if ( !key )
        {
          return std::unexpected( fmt::format( "no key {}", name.substr( 6 ) ) );
        }
        strokes.push_back( Keystroke{ .key = *key, .shift = true } );
        continue;
      }
      auto const named = namedKeystrokes();
      auto const found = std::ranges::find( named, std::string_view{ name }, &Named::name );
      if ( found == named.end() )
      {
        return std::unexpected( fmt::format( "no key {{{}}}", name ) );
      }
      strokes.push_back( found->stroke );
      continue;
    }
    std::size_t const start = at;
    char32_t const character = nextCharacter( text, at );
    auto const stroke = keystrokeFor( character );
    if ( !stroke )
    {
      return std::unexpected( fmt::format( "the PET has no key for '{}'", text.substr( start, at - start ) ) );
    }
    strokes.push_back( *stroke );
  }
  return strokes;
}

} // namespace pet::machine
