# 0012: The screen's shaders are GLSL, compiled offline to SPIR-V and MSL

**Status:** accepted

## Context

SDL_GPU does not compile shader source ([0008](0008-the-renderer-is-sdl-gpu.md)). PGMEmu writes
its screen shaders once in Vulkan GLSL and commits what glslang and SPIRV-Cross compile them to
(its record 0015). Its presets, sharp, scanlines and CRT, suit a monochrome monitor as well.

## Decision

- The shaders and `scripts/compile-shaders.sh` are PGMEmu's. The compiled SPIR-V and MSL are
  committed under `src/pet_app/shaders/compiled`.
- The GPU device is created for SPIR-V, MSL and metallib: Windows runs on Vulkan.

## Consequences

- A shader is changed in its GLSL and compiled again with the script (`brew install glslang
  spirv-cross`), and both are committed together.
