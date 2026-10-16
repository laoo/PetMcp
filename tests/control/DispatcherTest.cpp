#include <catch2/catch_test_macros.hpp>

#include "pet/Version.hpp"
#include "pet/control/Dispatcher.hpp"

#include <algorithm>
#include <string>

using pet::control::Dispatcher;
using pet::control::Json;

namespace
{

std::string errorCode( Json const& response )
{
  return response.at( "error" ).at( "code" ).get<std::string>();
}

} // namespace

TEST_CASE( "a request is answered under its id with its result", "[control]" )
{
  pet::Emulator emulator{ pet::Settings{} };
  Dispatcher const dispatcher{ emulator };

  auto const response = dispatcher.handle( Json::parse( R"({"id":7,"method":"emu.status","params":{}})" ) );

  REQUIRE( response.at( "id" ) == 7 );
  REQUIRE( response.at( "ok" ) == true );
  REQUIRE( response.at( "result" ).at( "version" ) == std::string{ pet::versionString() } );
}

TEST_CASE( "a request built in C++ is answered as a parsed one is", "[control]" )
{
  pet::Emulator emulator{ pet::Settings{} };
  Dispatcher const dispatcher{ emulator };

  // `1` here is a signed int, which the JSON library stores differently from
  // the unsigned it parses `1` into.
  auto const response = dispatcher.handle( Json{ { "id", 1 }, { "method", "emu.status" } } );

  REQUIRE( response.at( "id" ) == 1 );
  REQUIRE( response.at( "ok" ) == true );
}

TEST_CASE( "params may be left out", "[control]" )
{
  pet::Emulator emulator{ pet::Settings{} };
  Dispatcher const dispatcher{ emulator };

  auto const response = dispatcher.handle( Json::parse( R"({"id":1,"method":"emu.status"})" ) );

  REQUIRE( response.at( "ok" ) == true );
}

TEST_CASE( "an unknown method is answered with unknown_method under the request's id", "[control]" )
{
  pet::Emulator emulator{ pet::Settings{} };
  Dispatcher const dispatcher{ emulator };

  auto const response = dispatcher.handle( Json::parse( R"({"id":3,"method":"emu.nonsense"})" ) );

  REQUIRE( response.at( "id" ) == 3 );
  REQUIRE( response.at( "ok" ) == false );
  REQUIRE( errorCode( response ) == "unknown_method" );
}

TEST_CASE( "a malformed request is answered with bad_request, not refused", "[control]" )
{
  pet::Emulator emulator{ pet::Settings{} };
  Dispatcher const dispatcher{ emulator };

  SECTION( "not an object: answered under id 0" )
  {
    auto const response = dispatcher.handle( Json::parse( "[1,2]" ) );
    REQUIRE( response.at( "id" ) == 0 );
    REQUIRE( errorCode( response ) == "bad_request" );
  }

  SECTION( "no id: answered under id 0" )
  {
    auto const response = dispatcher.handle( Json::parse( R"({"method":"emu.status"})" ) );
    REQUIRE( response.at( "id" ) == 0 );
    REQUIRE( errorCode( response ) == "bad_request" );
  }

  SECTION( "a negative id is not an id" )
  {
    auto const response = dispatcher.handle( Json::parse( R"({"id":-1,"method":"emu.status"})" ) );
    REQUIRE( response.at( "id" ) == 0 );
    REQUIRE( errorCode( response ) == "bad_request" );
  }

  SECTION( "a fractional id is not an id" )
  {
    auto const response = dispatcher.handle( Json::parse( R"({"id":1.5,"method":"emu.status"})" ) );
    REQUIRE( response.at( "id" ) == 0 );
    REQUIRE( errorCode( response ) == "bad_request" );
  }

  SECTION( "no method: answered under the id that was given" )
  {
    auto const response = dispatcher.handle( Json::parse( R"({"id":4})" ) );
    REQUIRE( response.at( "id" ) == 4 );
    REQUIRE( errorCode( response ) == "bad_request" );
  }

  SECTION( "params that are not an object" )
  {
    auto const response = dispatcher.handle( Json::parse( R"({"id":5,"method":"emu.status","params":[]})" ) );
    REQUIRE( response.at( "id" ) == 5 );
    REQUIRE( errorCode( response ) == "bad_request" );
  }
}

TEST_CASE( "the method names are listed in a stable order", "[control]" )
{
  pet::Emulator emulator{ pet::Settings{} };
  Dispatcher const dispatcher{ emulator };

  auto const names = dispatcher.methodNames();

  REQUIRE( std::ranges::is_sorted( names ) );
  REQUIRE( std::ranges::find( names, "emu.status" ) != names.end() );
}
