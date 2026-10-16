#include "pet/machine/Screen.hpp"

#include "Clock.hpp"

#include <array>
#include <string_view>

namespace pet::machine
{

namespace
{

// Screen codes $40-$5F and $60-$7F of the graphics set, as UTF-8: the nearest
// characters of Unicode's box drawing, block and legacy computing ranges.
constexpr std::array<std::string_view, 64> GRAPHICS{
  "─", "♠", "│", "─", "─", "─", "─", "│", "│", "╮", "╰", "╯", "▁", "╲", "╱", "▔", "▔", "●", "▁", "♥", "▏", "╭",
  "╳", "○", "♣", "▕", "♦", "┼", "▒", "│", "π", "◥", " ", "▌", "▄", "▔", "▁", "▏", "▒", "▕", "▒", "◤", "▕", "├",
  "▗", "└", "┐", "▂", "┌", "┴", "┬", "┤", "▎", "▍", "▕", "▔", "▀", "▃", "▟", "▖", "▝", "┘", "▘", "▚",
};

void append( std::string& text, std::uint8_t code, bool textSet )
{
  code &= 0x7fU;
  if ( code == 0x00 )
  {
    text += '@';
  }
  else if ( code <= 0x1a )
  {
    // Letters: lower case in the text set, capitals in the graphics set.
    text += static_cast<char>( ( textSet ? 'a' : 'A' ) + code - 1 );
  }
  else if ( code < 0x20 )
  {
    static constexpr std::array<std::string_view, 5> PUNCTUATION{ "[", "\\", "]", "↑", "←" };
    text += PUNCTUATION.at( code - 0x1bU );
  }
  else if ( code < 0x40 )
  {
    text += static_cast<char>( code );
  }
  else if ( textSet && code >= 0x41 && code <= 0x5a )
  {
    text += static_cast<char>( 'A' + code - 0x41 );
  }
  else
  {
    text += GRAPHICS.at( code - 0x40U );
  }
}

} // namespace

std::vector<std::string> screenText( Machine const& machine )
{
  auto const videoRam = machine.videoRam();
  std::vector<std::string> lines;
  lines.reserve( TEXT_ROWS );
  for ( unsigned row = 0; row < TEXT_ROWS; ++row )
  {
    std::string line;
    for ( unsigned column = 0; column < TEXT_COLUMNS; ++column )
    {
      append( line, videoRam[( row * TEXT_COLUMNS ) + column], machine.textSet() );
    }
    lines.push_back( std::move( line ) );
  }
  return lines;
}

} // namespace pet::machine
