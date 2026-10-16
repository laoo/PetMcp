// Copyright (C) 2007-2018 Ingo Korb <ingo@akana.de>, as NODISKEMU (a fork of sd2iec)
// carries it. GPL-2.0. Ported to C++ for PetMcp, 2026 (THIRD_PARTY.md).

// Ports NODISKEMU's src/errormsg.c at commit e51a6df. Left out: the LEDs
// (led_state's LED_ERROR, set_error_led), the display (display_errorchannel)
// and the LCD (lcd_update_disk_status), which only showed the message. The
// petSD+ has no disk mux, so diskio.h's map_drive(drv) is drv; rom_filename
// is DosCommandsState's.

#include "sd2pet/ErrorMessages.hpp"

#include "sd2pet/Dos.hpp"
#include "sd2pet/Utils.hpp"

#include <array>
#include <cstring>

namespace pet::sd2pet
{

namespace
{

constexpr std::uint8_t ec( int x )
{
  return static_cast<std::uint8_t>( x + 0x80 );
}

/// Abbreviations used in the main error strings
// clang-format off
constexpr auto ABBREVS = std::to_array<std::uint8_t>( {
  ec(0), 'F','I','L','E',
  ec(1), 'R','E','A','D',
  ec(2), 'W','R','I','T','E',
  ec(3), ' ','E','R','R','O','R',
  ec(4), ' ','N','O','T',' ',
  ec(5), 'D','I','S','K',' ',
  ec(6), 'O','P','E','N',
  ec(7), 'R','E','C','O','R','D',
  ec(8), 'P','A','R','T','I','T','I','O','N',' ',
  ec(9), 'S','E','L','E','C','T','E','D',
  ec(10), 'I','L','L','E','G','A','L',
  ec(11), ' ','T','O','O',' ',
  ec(12), 'N','O',' ',
  ec(127)
} );
// clang-format on

/// Error string table
// clang-format off
constexpr auto MESSAGES = std::to_array<std::uint8_t>( {
  ec(00),
    ' ','O','K',
  ec(01),
    0,'S',' ','S','C','R','A','T','C','H','E','D',
  ec(02),
    8,9,
  ec(20), ec(21), ec(22), ec(23), ec(24), ec(27),
    1,3,
  ec(25), ec(28),
    2,3,
  ec(26),
    2,' ','P','R','O','T','E','C','T',' ','O','N',
  ec(29),
    5,'I','D',' ','M','I','S','M','A','T','C','H',
  ec(30), ec(31), ec(32), ec(33), ec(34),
    'S','Y','N','T','A','X',3,
  ec(39), ec(62),
    0,4,'F','O','U','N','D',
  ec(50),
    7,4,'P','R','E','S','E','N','T',
  ec(51),
    'O','V','E','R','F','L','O','W',' ','I','N',' ',7,
  ec(52),
    0,11,'L','A','R','G','E',
  ec(60),
    2,' ',0,' ',6,
  ec(61),
    0,4,6,
  ec(63),
    0,' ','E','X','I','S','T','S',
  ec(64),
    0,' ','T','Y','P','E',' ','M','I','S','M','A','T','C','H',
  ec(65),
    12,'B','L','O','C','K',
  ec(66), ec(67),
    10,' ','T','R','A','C','K',' ','O','R',' ','S','E','C','T','O','R',
  ec(70),
    12,'C','H','A','N','N','E','L',
  ec(71),
    'D','I','R',3,
  ec(72),
    5,'F','U','L','L',
  ec(74),
    'D','R','I','V','E',4,1,'Y',
  ec(77),
    9,' ',8,10,
  ec(78),
    'B','U','F','F','E','R',11,'S','M','A','L','L',
  ec(79),
    'I','M','A','G','E',' ',0,' ','I','N','V','A','L','I','D',
  ec(98),
    'U','N','K','N','O','W','N',' ','D','R','I','V','E','C','O','D','E',
  ec(99),
    'C','L','O','C','K',' ','U','N','S','T','A','B','L','E',
  ec(127)
} );
// clang-format on

std::uint8_t* appendmsg( std::uint8_t* msg, std::uint8_t const* table, std::uint8_t const entry )
{
  std::uint8_t tmp = 0;
  std::uint16_t i = 0;

  // EC(entry) is an int in C: an entry above 127 matches nothing.
  int const wanted = entry + 0x80;
  do
  {
    tmp = table[i++];
    if ( int{ tmp } == wanted || tmp == ec( 127 ) )
      break;
  } while ( true );

  if ( tmp == ec( 127 ) )
  {
    /* Unknown error */
    *msg++ = '?';
  }
  else
  {
    /* Skip over remaining error numbers */
    while ( table[i] >= ec( 0 ) )
      i++;

    /* Copy error string to buffer */
    do
    {
      tmp = table[i++];

      if ( tmp < 32 )
      {
        /* Abbreviation found, handle by recursion */
        msg = appendmsg( msg, ABBREVS.data(), tmp );
        continue;
      }

      if ( tmp < ec( 0 ) )
        /* Don't copy error numbers */
        *msg++ = tmp;
    } while ( tmp < ec( 0 ) );
  }

  return msg;
}

std::uint8_t* appendbool( std::uint8_t* msg, std::uint8_t ch, std::uint8_t value )
{
  if ( ch != 0 )
    *msg++ = ch;

  if ( value != 0 )
    *msg++ = '+';
  else
    *msg++ = '-';

  *msg++ = ':';

  return msg;
}

/// toupper in the C locale, avr-libc's.
std::uint8_t toupperC( std::uint8_t c )
{
  return ( c >= 'a' && c <= 'z' ) ? static_cast<std::uint8_t>( c - 'a' + 'A' ) : c;
}

/// diskio.h's map_drive without NEED_DISKMUX, as on the petSD+.
std::uint8_t mapDrive( std::uint8_t drv )
{
  return drv;
}

} // namespace

void setError( Dos& dos, std::uint8_t errornum )
{
  setErrorTs( dos, errornum, 0, 0 );
}

void setErrorTs( Dos& dos, std::uint8_t errornum, std::uint8_t track, std::uint8_t sector )
{
  auto& errorBuffer = dos.errors.errorBuffer;
  Buffer& errorChannel = dos.buffers.buffers.at( ERRORBUFFER_IDX );
  std::uint8_t* msg = errorBuffer.data();
  std::uint8_t i = 0;

  dos.errors.currentError = errornum;
  errorChannel.data = errorBuffer.data();
  errorChannel.lastused = 0;
  errorChannel.position = 0;
  errorBuffer.fill( 0 );

  msg = appendnumber( msg, errornum );
  *msg++ = ',';

  if ( errornum == ERROR_STATUS )
  {
    switch ( sector )
    {
    case 0:
    default:
      *msg++ = 'E';
      msg = appendnumber( msg, dos.fat.fileExtensionMode );
      msg = appendbool( msg, 0, static_cast<std::uint8_t>( dos.commands.globalflags & EXTENSION_HIDING ) );

      msg = appendbool( msg, '*', static_cast<std::uint8_t>( dos.commands.globalflags & POSTMATCH ) );

      *msg++ = 'I';
      msg = appendnumber( msg, dos.files.imageAsDir );

      *msg++ = ':';
      *msg++ = 'R';
      ustrcpy( msg, dos.commands.romFilename.data() );
      msg += ustrlen( dos.commands.romFilename.data() );

      break;
    case 1: // Drive Config
      *msg++ = 'D';
      while ( i < 8 )
      {
        if ( mapDrive( i ) != 0x0f )
        {
          *msg++ = ':';
          msg = appendnumber( msg, i );
          *msg++ = '=';
          msg = appendnumber( msg, mapDrive( i ) );
        }
        i++;
      }
      break;
    }
  }
  else if ( errornum == ERROR_LONGVERSION || errornum == ERROR_DOSVERSION )
  {
    /* Start with the name and version number */
    while ( ( *msg++ = static_cast<std::uint8_t>( VERSIONSTR[i++] ) ) != 0 )
    {
    }

    /* Append the long version if requested */
    if ( errornum == ERROR_LONGVERSION )
    {
      i = 0;
      msg--;
      while ( ( *msg++ = toupperC( static_cast<std::uint8_t>( LONGVERSTR[i++] ) ) ) != 0 )
      {
      }
    }

    msg--;
  }
  else
  {
    msg = appendmsg( msg, MESSAGES.data(), errornum );
  }
  *msg++ = ',';

  msg = appendnumber( msg, track );
  *msg++ = ',';

  msg = appendnumber( msg, sector );
  *msg = 13;

  errorChannel.lastused = static_cast<std::uint8_t>( msg - errorBuffer.data() );
}

/* Callback for the error channel buffer */
std::uint8_t setOkMessage( Dos& dos, Buffer* /*buf*/ )
{
  setError( dos, 0 );
  return 0;
}

} // namespace pet::sd2pet
