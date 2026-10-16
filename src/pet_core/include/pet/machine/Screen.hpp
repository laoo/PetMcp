#pragma once

#include "pet/machine/Machine.hpp"

#include <string>
#include <vector>

namespace pet::machine
{

/// The 25 lines of the screen as text, a character for each screen code as
/// the character set on show draws it: letters, digits and punctuation as
/// themselves, the graphics as the nearest Unicode character. Reverse video is
/// not marked.
[[nodiscard]] std::vector<std::string> screenText( Machine const& machine );

} // namespace pet::machine
