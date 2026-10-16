#pragma once

#include <cstddef>

namespace pet::video
{

/// The picture every frame the core produces has, and every texture a frontend
/// shows it in: the 320 by 200 dots of the 40 by 25 characters, with a border
/// of 32 dots left and right and 20 lines above and below. A line is 512 dots
/// and a frame 260 lines (docs/architecture.md, "Emulated time"); the border
/// shown is the part of the rest a monitor shows.
inline constexpr std::size_t SCREEN_WIDTH = 384;
inline constexpr std::size_t SCREEN_HEIGHT = 240;

} // namespace pet::video
