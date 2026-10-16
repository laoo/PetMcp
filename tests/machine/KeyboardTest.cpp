#include <catch2/catch_test_macros.hpp>

#include "pet/machine/Keyboard.hpp"

using pet::machine::Keystroke;
using pet::machine::PetKey;

TEST_CASE( "keys are found by name where VICE's keymap has them", "[keyboard]" )
{
  CHECK( pet::machine::keyNamed( "A" ) == PetKey{ .row = 4, .column = 0 } );
  CHECK( pet::machine::keyNamed( "a" ) == PetKey{ .row = 4, .column = 0 } );
  CHECK( pet::machine::keyNamed( "RETURN" ) == PetKey{ .row = 6, .column = 5 } );
  CHECK( pet::machine::keyNamed( "SPACE" ) == PetKey{ .row = 9, .column = 2 } );
  CHECK( pet::machine::keyNamed( "RIGHT_SHIFT" ) == PetKey{ .row = 8, .column = 5 } );
  CHECK( pet::machine::keyNamed( "0" ) == PetKey{ .row = 8, .column = 6 } );
  CHECK_FALSE( pet::machine::keyNamed( "F1" ) );
  CHECK( pet::machine::nameOf( PetKey{ .row = 9, .column = 4 } ) == "STOP" );
}

TEST_CASE( "a text becomes keystrokes: letters unshifted, braces naming keys", "[keyboard]" )
{
  auto const strokes = pet::machine::keystrokesFor( "Ab\"{CLR}{SHIFT-A}π\n" );
  REQUIRE( strokes );
  REQUIRE( strokes->size() == 7 );
  CHECK( ( *strokes )[0] == Keystroke{ .key = { .row = 4, .column = 0 }, .shift = false } );
  CHECK( ( *strokes )[1] == Keystroke{ .key = { .row = 6, .column = 2 }, .shift = false } );
  CHECK( ( *strokes )[2] == Keystroke{ .key = { .row = 1, .column = 0 }, .shift = false } );
  CHECK( ( *strokes )[3] == Keystroke{ .key = { .row = 0, .column = 6 }, .shift = true } );
  CHECK( ( *strokes )[4] == Keystroke{ .key = { .row = 4, .column = 0 }, .shift = true } );
  CHECK( ( *strokes )[5] == Keystroke{ .key = { .row = 2, .column = 5 }, .shift = true } );
  CHECK( ( *strokes )[6] == Keystroke{ .key = { .row = 6, .column = 5 }, .shift = false } );
}

TEST_CASE( "what the PET cannot type is named", "[keyboard]" )
{
  CHECK_FALSE( pet::machine::keystrokesFor( "{F1}" ) );
  CHECK_FALSE( pet::machine::keystrokesFor( "{CLR" ) );
  CHECK_FALSE( pet::machine::keystrokesFor( "ä" ) );
}
