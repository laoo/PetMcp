#pragma once

// The methods of docs/spec/control-protocol.md, one group per file, and what
// they share for reading parameters. Private to the core: a transport sees only
// the Dispatcher.

#include "pet/Emulator.hpp"
#include "pet/control/Dispatcher.hpp"

#include <cstdint>
#include <expected>
#include <initializer_list>
#include <string>
#include <string_view>

namespace pet::control
{

void addEmuMethods( Dispatcher& dispatcher, Emulator& emulator );
void addRunMethods( Dispatcher& dispatcher, Emulator& emulator );
void addMemoryMethods( Dispatcher& dispatcher, Emulator& emulator );
void addCpuMethods( Dispatcher& dispatcher, Emulator& emulator );
void addVideoMethods( Dispatcher& dispatcher, Emulator& emulator );
void addInputMethods( Dispatcher& dispatcher, Emulator& emulator );
void addProgramMethods( Dispatcher& dispatcher, Emulator& emulator );
void addDebugMethods( Dispatcher& dispatcher, Emulator& emulator );
void addStateMethods( Dispatcher& dispatcher, Emulator& emulator );
void addAudioMethods( Dispatcher& dispatcher, Emulator& emulator );
void addDiskMethods( Dispatcher& dispatcher, Emulator& emulator );

Error badRequest( std::string message );

/// The `no_machine` error: why the PET is not running.
Error noMachine( Emulator const& emulator );

/// The emulator's machine, or the `no_machine` error.
std::expected<machine::Machine*, Error> machineOf( Emulator& emulator );

/// What a run answers: why it stopped, and the cycles and frames it took.
Json runJson( machine::RunResult const& result );

/// Where a run stopped, when a breakpoint or a watchpoint stopped it: the
/// `breakpoint` or `watchpoint` object of its answer. Empty otherwise.
Json stopJson( machine::RunResult const& result );

/// One parameter of a method, as its schema describes it: a JSON Schema type
/// ("string", "integer", "boolean", "object", "array"), and what it means.
struct Param
{
  std::string_view name;
  std::string_view type;
  std::string_view description;
  bool required{ true };
};

/// The JSON Schema of a method's `params`: an object of `params`.
Json paramsSchema( std::initializer_list<Param> params );

/// A method's description and the schema of its params.
MethodInfo info( std::string_view description, std::initializer_list<Param> params = {} );

/// The string `params[name]`, or the `bad_request` that says it is missing or
/// is not a string.
std::expected<std::string, Error> requireString( Json const& params, std::string_view name );

/// The non-negative integer `params[name]`, or the `bad_request` that says it
/// is missing or is not one. A string of hex digits after `0x` or `$` is one
/// too.
std::expected<std::uint64_t, Error> requireUnsigned( Json const& params, std::string_view name );

} // namespace pet::control
