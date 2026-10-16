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

Error badRequest( std::string message );

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
