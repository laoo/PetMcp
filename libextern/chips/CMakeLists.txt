# floooh/chips' 6502 and its disassembler, vendored. See ../README.md for
# provenance and the update rule.
#
# Both are single-header libraries: the implementation is compiled into the one
# translation unit of ours that defines CHIPS_IMPL (and CHIPS_UTIL_IMPL), so
# this is an interface target carrying the include directory, SYSTEM so that
# upstream's code is held to upstream's warnings, not ours.
add_library( chips_m6502 INTERFACE )

target_include_directories( chips_m6502 SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}" )
