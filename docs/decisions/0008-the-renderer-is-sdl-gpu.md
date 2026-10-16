# 0008: The renderer is SDL_GPU

**Status:** accepted

## Context

The owner asked for PGMEmu's frontend: SDL3, Dear ImGui, and its look and feel. PGMEmu renders
through SDL_GPU, SDL3's API over Metal, Vulkan and Direct3D 12, for the reasons its record 0008
gives: OpenGL is deprecated on macOS, and SDL_Renderer offers no shaders of our own.

## Decision

- The frontend renders through SDL_GPU, with ImGui's `imgui_impl_sdl3` and `imgui_impl_sdlgpu3`
  backends, as PGMEmu's does.
- The emulated screen is an `R8G8B8A8_UNORM` texture, staged through one transfer buffer, drawn
  through the screen shaders ([0012](0012-shaders-are-compiled-offline.md)) into a texture the
  size it is shown at, and shown with nearest sampling.

## Consequences

- The application's code is PGMEmu's where the two machines do not differ, and improvements can
  move between them.
