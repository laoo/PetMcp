#pragma once

#include <cstddef>

namespace pet::video
{

/// The picture every frame the core produces has, and every texture a frontend
/// shows it in: the 320 by 200 dots of the 40 by 25 characters. The rest of a
/// line's 512 dots and a frame's 260 lines is never lit on a PET, and is not
/// part of the picture.
inline constexpr std::size_t SCREEN_WIDTH = 320;
inline constexpr std::size_t SCREEN_HEIGHT = 200;

} // namespace pet::video
