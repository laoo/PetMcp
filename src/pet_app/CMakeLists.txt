add_executable( pet_app
  src/Application.cpp
  src/DisplaySettings.cpp
  src/EmulationThread.cpp
  src/GpuTexture.cpp
  src/Main.cpp
  src/ScreenRenderer.cpp )

set_target_properties( pet_app PROPERTIES OUTPUT_NAME petmcp )

target_link_libraries( pet_app
  PRIVATE pet::server imgui SDL3::SDL3-static CLI11::CLI11 pet::warnings )
