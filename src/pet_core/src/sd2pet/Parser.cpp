// Copyright (C) 2007-2018 Ingo Korb <ingo@akana.de>, as NODISKEMU (a fork of sd2iec)
// carries it. GPL-2.0. Ported to C++ for PetMcp, 2026 (THIRD_PARTY.md).

// Ports NODISKEMU's src/parser.c at commit e51a6df. Left out: the display
// (update_current_dir behaves as with display_found false) and the EEPROM
// file system (parse_partition's "!:" alias for its partition).

#include "sd2pet/Parser.hpp"

#include "sd2pet/Dos.hpp"
#include "sd2pet/Utils.hpp"

#include <cstring>

namespace pet::sd2pet
{

namespace
{

/// isdigit in the C locale, avr-libc's.
bool isDigit( std::uint8_t c )
{
  return c >= '0' && c <= '9';
}

} // namespace

/* Updates current_dir in the partition array and sends */
/* the new dir to the display.                          */
void updateCurrentDir( Dos& dos, Path* path )
{
  dos.parser.partition.at( path->part ).currentDir = path->dir;
  dos.parser.dirChanged = 1;
}

/**
 * check_invalid_name - check for invalid characters in a file name
 * @name: pointer to the file name
 *
 * This function checks if the passed name contains any characters
 * that are not valid in a CBM file name. Returns 1 if invalid
 * characters are found, 0 if not.
 */
std::uint8_t checkInvalidName( Dos& /*dos*/, std::uint8_t* name )
{
  while ( *name != 0 )
  {
    if ( *name == '=' || *name == '"' || *name == '*' || *name == '?' || *name == ',' )
      return 1;
    name++;
  }
  return 0;
}

namespace
{

/* Convert a PETSCII character to lower-case */
std::uint8_t tolowerPet( std::uint8_t c )
{
  if ( c >= 0x61 && c <= 0x7a )
    c = static_cast<std::uint8_t>( c - 0x20 );
  else if ( c >= 0xc1 && c <= 0xda )
    c = static_cast<std::uint8_t>( c - 0x80 );
  return c;
}

} // namespace

/**
 * parse_partition - parse a partition number from a file name
 * @buf     : pointer to pointer to filename
 *
 * This function parses the partition number from the file name
 * specified in buf and advances buf to point at the first character
 * that isn't part of the number. Returns a 0-based partition number.
 */
std::uint8_t parsePartition( Dos& dos, std::uint8_t** buf )
{
  std::uint8_t part = 0;

  while ( isDigit( **buf ) || **buf == ' ' || **buf == '@' )
  {
    if ( isDigit( **buf ) )
      part = static_cast<std::uint8_t>( ( part * 10 ) + ( **buf - '0' ) );
    ( *buf )++;
  }
  if ( part == 0 )
    return dos.parser.currentPart;
  return static_cast<std::uint8_t>( part - 1 );
}

/**
 * match_name - Match a pattern against a file name
 * @matchstr  : pattern to be matched
 * @dent      : pointer to the directory entry to be matched against
 * @ignorecase: ignore the case of the file names
 *
 * This function tests if matchstr matches name in dent.
 * Returns 1 for a match, 0 otherwise.
 */
std::uint8_t matchName( Dos& dos, std::uint8_t* matchstr, CbmDirent* dent, std::uint8_t ignorecase )
{
  std::uint8_t* filename = dent->name.data();
  std::uint8_t* starpos = nullptr;
  std::uint8_t m = 0;
  std::uint8_t f = 0;
  std::uint8_t charsRemain = 16;

  while ( charsRemain != 0 && *filename != 0 )
  {
    if ( ignorecase != 0 )
    {
      m = tolowerPet( *matchstr );
      f = tolowerPet( *filename );
    }
    else
    {
      m = *matchstr;
      f = *filename;
    }
    switch ( m )
    {
    case '?':
      filename++;
      matchstr++;
      charsRemain--;
      break;

    case '*':
      if ( ( dos.commands.globalflags & POSTMATCH ) != 0 )
      {
        starpos = matchstr;
        matchstr += ustrlen( matchstr ) - 1;
        filename += ustrlen( filename ) - 1;
        while ( matchstr != starpos )
        {
          if ( ignorecase != 0 )
          {
            m = tolowerPet( *matchstr );
            f = tolowerPet( *filename );
          }
          else
          {
            m = *matchstr;
            f = *filename;
          }
          if ( m != f && m != '?' )
            return 0;
          filename--;
          matchstr--;
        }
      }
      return 1;

    default:
      if ( m != f )
        return 0;
      matchstr++;
      filename++;
      charsRemain--;
      break;
    }
  }
  if ( *matchstr != 0 && *matchstr != '*' && charsRemain != 0 )
    return 0;
  return 1;
}

/**
 * next_match - get next matching directory entry
 * @dh        : directory handle
 * @matchstr  : pattern to be matched
 * @start     : start date
 * @end       : end date
 * @type      : required file type (0 for any)
 * @dent      : pointer to a directory entry for returning the match
 *
 * This function looks for the next directory entry matching matchstr and
 * type (if != 0) and returns it in dent. Return values of the function are
 * -1 if no match could be found, 1 if an error occured or 0 if a match was
 * found.
 */
std::int8_t
nextMatch( Dos& dos, Dh* dh, std::uint8_t* matchstr, Date* start, Date* end, std::uint8_t type, CbmDirent* dent )
{
  std::int8_t res = 0;

  while ( true )
  {
    res = readdir( dos, dh, dent );
    if ( res == 0 )
    {
      /* Skip if the type doesn't match */
      if ( ( type & TYPE_MASK ) != 0 && ( dent->typeflags & TYPE_MASK ) != ( type & TYPE_MASK ) )
        continue;

      /* Skip hidden files */
      if ( ( dent->typeflags & FLAG_HIDDEN ) != 0 && ( type & FLAG_HIDDEN ) == 0 )
        continue;

      /* Skip if the name doesn't match */
      if ( matchstr != nullptr )
      {
        if ( dent->opstype == OpsType::FAT )
        {
          /* FAT: Ignore case */
          if ( matchName( dos, matchstr, dent, 1 ) == 0 )
            continue;
        }
        else
        {
          /* Honor case */
          if ( matchName( dos, matchstr, dent, 0 ) == 0 )
            continue;
        }
      }

      /* skip if earlier than start date */
      if ( start != nullptr && std::memcmp( &dent->date, start, sizeof( Date ) ) < 0 )
        continue;

      /* skip if later than end date */
      if ( end != nullptr && std::memcmp( &dent->date, end, sizeof( Date ) ) > 0 )
        continue;
    }

    return res;
  }
}

/**
 * first_match - get the first matching directory entry
 * @path    : pointer to a path object
 * @matchstr: pattern to be matched
 * @type    : required file type (0 for any)
 * @dent    : pointer to a directory entry for returning the match
 *
 * This function looks for the first directory entry matching matchstr and
 * type (if != 0) in path and returns it in dent. Uses matchdh for matching
 * and returns the same values as next_match. This function is just a
 * convenience wrapper around opendir+next_match, it is not required to call
 * it before using next_match.
 */
std::int8_t firstMatch( Dos& dos, Path* path, std::uint8_t* matchstr, std::uint8_t type, CbmDirent* dent )
{
  std::int8_t res = 0;

  if ( opendir( dos, &dos.buffers.matchdh, path ) != 0 )
    return 1;

  res = nextMatch( dos, &dos.buffers.matchdh, matchstr, nullptr, nullptr, type, dent );
  if ( res < 0 )
    setError( dos, ERROR_FILE_NOT_FOUND );
  return res;
}

/**
 * parse_path - parse CMD style directory specification
 * @in    : input buffer
 * @path  : pointer to path object
 * @name  : pointer to pointer to filename (may be NULL)
 * @for_cd: parse path from CD command
 *
 * This function parses a CMD style directory specification in the input
 * buffer. If successful, the path object will be set up for accessing
 * the path named in the input buffer. Returns 0 if successful or 1 if an
 * error occured.
 * If @for_cd is true, no colon is required in the input string and parsing
 * is ended successfully if the last component in the path is an image file.
 */
std::uint8_t parsePath( Dos& dos, std::uint8_t* in, Path* path, std::uint8_t** name, std::uint8_t forCd )
{
  CbmDirent dent{};
  std::uint8_t* end = nullptr;
  std::uint8_t saved = 0;
  std::uint8_t part = 0;

  if ( forCd != 0 || ustrchr( in, ':' ) != nullptr )
  {
    /* Skip partition number */
    part = parsePartition( dos, &in );
    if ( part >= dos.parser.maxPart )
    {
      setError( dos, ERROR_DRIVE_NOT_READY );
      return 1;
    }

    path->part = part;
    path->dir = dos.parser.partition.at( part ).currentDir;

    if ( *in != '/' )
    {
      *name = ustrchr( in, ':' );
      if ( *name == nullptr )
        *name = in;
      else
        *name += 1;
      return 0;
    }

    while ( *in != 0 )
    {
      switch ( *in++ )
      {
      case '/':
        switch ( *in )
        {
        case '/':
          /* Double slash -> root */
          dent.name[0] = 0;
          chdir( dos, path, &dent );
          break;

        case 0:
          /* End of path found, no name */
          *name = in;
          return 0;

        case ':':
          /* End of path found */
          *name = in + 1;
          return 0;

        default:
          /* Extract path component and match it */
          end = in;
          while ( *end != 0 && *end != '/' && *end != ':' )
            end++;
          saved = *end;
          *end = 0;
          if ( firstMatch( dos, path, in, FLAG_HIDDEN, &dent ) != 0 )
          {
            /* first_match has set an error already */
            if ( dos.errors.currentError == ERROR_FILE_NOT_FOUND )
              setError( dos, ERROR_FILE_NOT_FOUND_39 );
            return 1;
          }

          if ( ( dent.typeflags & TYPE_MASK ) != TYPE_DIR )
          {
            /* Not a directory */
            /* FIXME: Try to mount as image here so they can be accessed like a directory */
            if ( forCd != 0 && saved == 0 && checkImageext( in ) != ImgType::UNKNOWN )
            {
              /* no further path components, last one is an image file */
              *name = in;
              return 0;
            }
            setError( dos, ERROR_FILE_NOT_FOUND_39 );
            return 1;
          }

          /* Match found, move path */
          chdir( dos, path, &dent );
          *end = saved;
          in = end;
          break;
        }
        break;

      case 0:
        /* End of path found, no name */
        *name = in - 1; // -1 to fix the ++ in switch
        return 0;

      case ':':
        /* End of path found */
        *name = in;
        return 0;

      default:
        break;
      }
    }
  }
  else
  {
    /* No :, use current dir/path */
    path->part = dos.parser.currentPart;
    path->dir = dos.parser.partition.at( dos.parser.currentPart ).currentDir;
  }

  *name = in;
  return 0;
}

/* Parse a decimal number at str and return a pointer to the following char */
std::uint16_t parseNumber( Dos& /*dos*/, std::uint8_t** str )
{
  std::uint16_t res = 0;

  /* Skip leading spaces */
  while ( **str == ' ' )
    ( *str )++;

  /* Parse decimal number */
  while ( isDigit( **str ) )
  {
    res = static_cast<std::uint16_t>( res * 10 );
    res = static_cast<std::uint16_t>( res + ( *( *str )++ ) - '0' );
  }

  return res;
}

/* Parse a CMD-style date and return a pointer to the following char */
std::uint8_t parseDate( Dos& dos, Date* date, std::uint8_t** str )
{
  std::uint8_t ch = 0;

  date->month = static_cast<std::uint8_t>( parseNumber( dos, str ) );
  if ( date->month > 12 || *( *str )++ != '/' )
    return 1;

  date->day = static_cast<std::uint8_t>( parseNumber( dos, str ) );
  if ( date->day > 31 || *( *str )++ != '/' )
    return 1;

  date->year = static_cast<std::uint8_t>( parseNumber( dos, str ) );
  /* Y2K */
  if ( date->year < 80 )
    date->year = static_cast<std::uint8_t>( date->year + 100 );

  /* Shortcut: Just a date without time */
  if ( **str == 0 || *( *str ) == ',' )
  {
    date->hour = 0;
    date->minute = 0;
    date->second = 0;
    return 0;
  }
  if ( *( *str )++ != ' ' )
    return 1;

  date->hour = static_cast<std::uint8_t>( parseNumber( dos, str ) );
  ch = *( *str )++;
  if ( date->hour > 23 || ( ch != ':' && ch != '.' ) )
    return 1;

  date->minute = static_cast<std::uint8_t>( parseNumber( dos, str ) );
  ch = *( *str )++;
  if ( date->minute > 59 )
    return 1;

  switch ( ch )
  {
  case ':':
  case '.':
    date->second = static_cast<std::uint8_t>( parseNumber( dos, str ) );
    if ( date->second > 59 || *( *str )++ != ' ' )
      return 1;
    break;

  case ' ':
    date->second = 0;
    break;

  case ',':
  case 0:
    /* No AM/PM */
    ( *str )--;
    date->second = 0;
    return 0;

  default:
    return 1;
  }

  switch ( *( *str )++ )
  {
  case 'A':
    break;
  case 'P':
    date->hour = static_cast<std::uint8_t>( date->hour + 12 );
    break;
  default:
    return 1;
  }
  if ( date->hour > 23 )
    return 1;

  if ( *( *str )++ != 'M' )
    return 1; // JLB need to check to see if CMD is this anal
  return 0;
}

} // namespace pet::sd2pet
