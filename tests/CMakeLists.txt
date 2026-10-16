add_executable( pet_tests
  VersionTest.cpp
  control/DispatcherTest.cpp
  server/JsonLinesServerTest.cpp
  server/McpServerTest.cpp
  server/NetworkServersTest.cpp
  support/Files.cpp )

# The core's private headers as well as its public ones: the devices of the
# machine are tested one by one, without the 6502 in front of them.
target_include_directories( pet_tests PRIVATE . "${PROJECT_SOURCE_DIR}/src/pet_core/src" )

# miniz's headers only: the suite writes zips and reads gzips with it, and its
# code arrives with pet_core, which links it. Linking it here as well puts the
# archive on the link line twice, which Apple's linker warns about.
target_include_directories( pet_tests SYSTEM PRIVATE $<TARGET_PROPERTY:miniz,INTERFACE_INCLUDE_DIRECTORIES> )

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
