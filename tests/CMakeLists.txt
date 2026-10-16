add_executable( pet_tests
  VersionTest.cpp
  control/DispatcherTest.cpp
  control/ProgramsAndInputTest.cpp
  cpu/Cpu6502SingleStepTest.cpp
  cpu/Cpu6502Test.cpp
  machine/BoardAndSoundTest.cpp
  machine/BenchTest.cpp
  machine/DriveTest.cpp
  machine/KeyboardTest.cpp
  machine/MachineTest.cpp
  machine/StateAndDebugTest.cpp
  machine/TraceTest.cpp
  machine/Via6522Test.cpp
  server/JsonLinesServerTest.cpp
  server/McpServerTest.cpp
  server/NetworkServersTest.cpp
  support/DiskImage.cpp
  support/Files.cpp )

# The core's private headers as well as its public ones: the devices of the
# machine are tested one by one, without the 6502 in front of them.
target_include_directories( pet_tests PRIVATE . "${PROJECT_SOURCE_DIR}/src/pet_core/src" )

# miniz's headers only: the suite writes zips and reads gzips with it, and its
# code arrives with pet_core, which links it. Linking it here as well puts the
# archive on the link line twice, which Apple's linker warns about.
target_include_directories( pet_tests SYSTEM PRIVATE $<TARGET_PROPERTY:miniz,INTERFACE_INCLUDE_DIRECTORIES> )

# Where the [cpu-suite] tests find the SingleStepTests suite
# scripts/fetch-6502-tests.sh downloads; they skip without it.
target_compile_definitions( pet_tests
  PRIVATE PET_TEST_6502_DIR="${CMAKE_SOURCE_DIR}/build/tools/6502" )

# Where the [roms] tests find the PET's ROMs and the owner's references beside
# the repository; they skip without them.
target_compile_definitions( pet_tests
  PRIVATE PET_TEST_ROM_DIR="${CMAKE_SOURCE_DIR}/../vice/data/PET"
          PET_TEST_TRACE="${CMAKE_SOURCE_DIR}/../PETTest/trace_1s.txt"
          PET_TEST_PETTEST_DIR="${CMAKE_SOURCE_DIR}/../PETTest" )

target_link_libraries( pet_tests
  PRIVATE pet::server Catch2::Catch2WithMain pet::warnings httplib )

include( Catch )
catch_discover_tests( pet_tests )

# The control protocol's transcripts: each one's requests answered by
# petmcp-cli --transcript and compared with the answers recorded beside it
# (docs/spec/control-protocol.md §1).
foreach( transcript IN ITEMS lines mcp )
  set( transport )
  if( transcript STREQUAL "mcp" )
    set( transport --mcp )
  endif()
  add_test( NAME "control.${transcript}"
            COMMAND pet_cli --transcript "control/${transcript}.jsonl" ${transport}
                    --expect "control/${transcript}.expect"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" )
  set_tests_properties( "control.${transcript}" PROPERTIES LABELS control )
endforeach()

# The disk the SD2PET's transcript and the regression suite run beside, written
# by pet_test_disk into the build's tests directory: no disk image is carried
# in the tree.
add_executable( pet_test_disk support/MakeTestDisk.cpp support/DiskImage.cpp )
target_include_directories( pet_test_disk PRIVATE . )
target_link_libraries( pet_test_disk PRIVATE pet::warnings )
# The disk regression script writes to its own copy, so that the transcript,
# which runs beside it, reads the disk as written.
add_test( NAME "test_disk" COMMAND pet_test_disk "${CMAKE_CURRENT_BINARY_DIR}/disk" )
add_test( NAME "test_disk_rw" COMMAND pet_test_disk "${CMAKE_CURRENT_BINARY_DIR}/disk-rw" )
set_tests_properties( "test_disk" "test_disk_rw" PROPERTIES FIXTURES_SETUP disk_image )

# The SD2PET's transcript needs the PET running, so VICE's ROMs beside the
# repository; without them it is left out, as the [roms] tests skip.
set( PET_TEST_ROMS "${CMAKE_SOURCE_DIR}/../vice/data/PET" )
if( EXISTS "${PET_TEST_ROMS}" )
  add_test( NAME "control.disk"
            COMMAND pet_cli --rom-dir "${PET_TEST_ROMS}"
                    --transcript "${CMAKE_CURRENT_SOURCE_DIR}/control/disk.jsonl"
                    --expect "${CMAKE_CURRENT_SOURCE_DIR}/control/disk.expect"
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" )
  set_tests_properties( "control.disk" PROPERTIES FIXTURES_REQUIRED disk_image LABELS control )
endif()

# The regression suite: every script in regression/ run by petmcp-cli --batch,
# which checks it against the golden sums it holds (docs/spec/batch.md). Run
# beside the test disk; a script whose ROMs or files are not there exits 77,
# which CTest counts as skipped.
set( regression_roms )
if( EXISTS "${PET_TEST_ROMS}" )
  set( regression_roms --rom-dir "${PET_TEST_ROMS}" )
endif()
file( GLOB regression_scripts CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/regression/*.json" )
foreach( script IN LISTS regression_scripts )
  get_filename_component( name "${script}" NAME_WE )
  add_test( NAME "regression.${name}"
            COMMAND pet_cli --batch "${script}" ${regression_roms}
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" )
  set_tests_properties( "regression.${name}"
                        PROPERTIES LABELS regression SKIP_RETURN_CODE 77 FIXTURES_REQUIRED disk_image )
endforeach()
