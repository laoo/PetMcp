cmake_minimum_required( VERSION 3.25 )

project( PetMcp
  DESCRIPTION "Emulator of the Commodore PET 3032 with an SD2PET, driven by people and by agents alike"
  LANGUAGES   C CXX )

list( APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake" )

# No `VERSION` above: the version is the tag, and cmake/Version.cmake says why.
include( Version )
petResolveVersion( PET_VERSION_STRING )
message( STATUS "PetMcp version: ${PET_VERSION_STRING}" )

set( CMAKE_CXX_STANDARD          23 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )
set( CMAKE_CXX_EXTENSIONS        OFF )
set( CMAKE_EXPORT_COMPILE_COMMANDS ON )

option( PET_BUILD_TESTS "Build the test suite"                     ON )
option( PET_BUILD_APP   "Build the SDL3 desktop application"       ON )
option( PET_WERROR      "Treat compiler warnings as errors"        OFF )

include( Warnings )
include( Dependencies )

add_subdirectory( libextern )

add_subdirectory( src/pet_core )
add_subdirectory( src/pet_server )
add_subdirectory( src/pet_cli )

# The application is the one target that needs SDL and ImGui, so a headless
# build, the suite and CI can leave both out.
if( PET_BUILD_APP )
  add_subdirectory( src/pet_app )
endif( )

if( PET_BUILD_TESTS )
  enable_testing( )
  add_subdirectory( tests )
endif( )

include( Install )
