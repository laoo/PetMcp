add_library( pet_core
  src/Emulator.cpp
  src/PortabilityChecks.cpp
  src/Version.cpp
  src/control/Dispatcher.cpp
  src/control/CpuMethods.cpp
  src/control/DebugMethods.cpp
  src/control/EmuMethods.cpp
  src/control/InputMethods.cpp
  src/control/MemoryMethods.cpp
  src/control/Params.cpp
  src/control/ProgramMethods.cpp
  src/control/RunMethods.cpp
  src/control/StateMethods.cpp
  src/control/VideoMethods.cpp
  src/cpu/Cpu6502.cpp
  src/cpu/Disassembler.cpp
  src/machine/Keyboard.cpp
  src/machine/Machine.cpp
  src/machine/Pia6520.cpp
  src/machine/Programs.cpp
  src/machine/Roms.cpp
  src/machine/Screen.cpp
  src/machine/TraceHistory.cpp
  src/machine/Via6522.cpp
  src/machine/Video.cpp )

add_library( pet::core ALIAS pet_core )

target_include_directories( pet_core PUBLIC include PRIVATE src )

target_compile_definitions( pet_core PRIVATE PET_VERSION="${PET_VERSION_STRING}" )

# nlohmann_json is PUBLIC because the dispatcher's interface is JSON: a request
# goes in as one and a response comes out as one, whichever transport carries
# them. spdlog is PUBLIC as in NGA: every program built on the core logs through
# the one instance, and linking it twice is what Apple's linker warns about.
# The rest is PRIVATE: no header of the core includes it.
target_link_libraries( pet_core
  PUBLIC  nlohmann_json::nlohmann_json spdlog::spdlog
  PRIVATE pet::warnings miniz chips_m6502 )
