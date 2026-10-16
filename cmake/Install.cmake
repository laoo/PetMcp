# What a release package holds: `cmake --install <build> --component petmcp`.
# The libraries fetched in Dependencies.cmake carry install rules of their own,
# for their headers and archives, and a package wants none of them; so every
# rule here is the `petmcp` component's, and a package installs that alone.
#
# The package holds the programs, the licence, this project's credits and the
# licence of every library linked into them (THIRD_PARTY.md). On macOS the
# application is an app bundle, assembled here rather than built as one, so
# that the build tree keeps the executable where the scripts and docs find it.

set( PET_COMPONENT petmcp )

install( TARGETS pet_cli RUNTIME DESTINATION . COMPONENT ${PET_COMPONENT} )

if( PET_BUILD_APP )
  if( APPLE )
    # A bundle's version is numbers alone: the tag's, or 0.0.0 for a build
    # that is not a release.
    set( PET_BUNDLE_VERSION "0.0.0" )
    if( PET_VERSION_STRING MATCHES "^([0-9]+(\\.[0-9]+)*)" )
      set( PET_BUNDLE_VERSION "${CMAKE_MATCH_1}" )
    endif( )
    set( PET_BUNDLE_MINIMUM_SYSTEM "${CMAKE_OSX_DEPLOYMENT_TARGET}" )
    if( NOT PET_BUNDLE_MINIMUM_SYSTEM )
      set( PET_BUNDLE_MINIMUM_SYSTEM "13.0" )
    endif( )
    configure_file( "${CMAKE_CURRENT_LIST_DIR}/Info.plist.in" "${CMAKE_BINARY_DIR}/Info.plist" @ONLY )

    install( TARGETS pet_app RUNTIME DESTINATION PetMcp.app/Contents/MacOS COMPONENT ${PET_COMPONENT} )
    install( FILES "${CMAKE_BINARY_DIR}/Info.plist" DESTINATION PetMcp.app/Contents COMPONENT ${PET_COMPONENT} )
  else( )
    install( TARGETS pet_app RUNTIME DESTINATION . COMPONENT ${PET_COMPONENT} )
  endif( )
endif( )

install( FILES LICENSE README.md THIRD_PARTY.md DESTINATION . COMPONENT ${PET_COMPONENT} )

# Each library's own licence, named after it.
set( licences
     "CLI11.txt=${cli11_SOURCE_DIR}/LICENSE"
     "spdlog.txt=${spdlog_SOURCE_DIR}/LICENSE"
     "nlohmann_json.txt=${nlohmann_json_SOURCE_DIR}/LICENSE.MIT"
     "miniz.txt=${miniz_SOURCE_DIR}/LICENSE"
     "cpp-httplib.txt=${httplib_SOURCE_DIR}/LICENSE" )
if( PET_BUILD_APP )
  list( APPEND licences "SDL3.txt=${sdl3_SOURCE_DIR}/LICENSE.txt" "imgui.txt=${imgui_SOURCE_DIR}/LICENSE.txt" )
endif( )
foreach( entry IN LISTS licences )
  string( REPLACE "=" ";" pair "${entry}" )
  list( GET pair 0 name )
  list( GET pair 1 file )
  install( FILES "${file}" DESTINATION licenses RENAME "${name}" COMPONENT ${PET_COMPONENT} )
endforeach( )
