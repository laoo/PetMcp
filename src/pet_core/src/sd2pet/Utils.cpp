// Copyright (C) 2007-2018 Ingo Korb <ingo@akana.de>, as NODISKEMU (a fork of sd2iec)
// carries it. GPL-2.0. Ported to C++ for PetMcp, 2026 (THIRD_PARTY.md).

// Ports NODISKEMU's src/utils.c and src/ustring.h at commit e51a6df. Left
// out: utils.h's min and max macros, for which callers take std::min and
// std::max. ustring.h's macros are functions with the semantics of the C
// library functions they wrap; its _P variants are the overloads taking
// char const*, since nothing here lives in program memory.

#include "sd2pet/Utils.hpp"

namespace pet::sd2pet
{

namespace
{

/// tolower in the C locale, which is avr-libc's.
int asciiToLower( int c )
{
  return ( c >= 'A' && c <= 'Z' ) ? c - 'A' + 'a' : c;
}

} // namespace

/* Append a decimal number to a string */
std::uint8_t* appendnumber( std::uint8_t* msg, std::uint8_t value )
{
  if ( value >= 100 )
  {
    *msg++ = static_cast<std::uint8_t>( '0' + ( value / 100 ) );
    value = static_cast<std::uint8_t>( value % 100 );
  }

  *msg++ = static_cast<std::uint8_t>( '0' + ( value / 10 ) );
  *msg++ = static_cast<std::uint8_t>( '0' + ( value % 10 ) );

  return msg;
}

/* Convert a one-byte BCD value to a normal integer */
std::uint8_t bcd2int( std::uint8_t value )
{
  return static_cast<std::uint8_t>( ( value & 0x0fU ) + ( 10U * ( value >> 4U ) ) );
}

/* Convert a uint8_t into a BCD value */
std::uint8_t int2bcd( std::uint8_t value )
{
  return static_cast<std::uint8_t>( ( value % 10U ) + ( 16U * ( value / 10U ) ) );
}

/* Similiar to strtok_r, but only a single delimiting character  */
std::uint8_t* ustr1tok( std::uint8_t* str, std::uint8_t delim, std::uint8_t** saveptr )
{
  std::uint8_t* tmp = nullptr;

  if ( str == nullptr )
    str = *saveptr;

  /* Skip leading delimiters */
  while ( *str == delim )
    str++;

  /* If there is anything left... */
  if ( *str != 0 )
  {
    /* Search for the next delimiter */
    tmp = str;
    while ( *tmp != 0 && *tmp != delim )
      tmp++;

    /* Terminate the string there */
    if ( *tmp != 0 )
      *tmp++ = 0;

    *saveptr = tmp;

    return str;
  }
  return nullptr;
}

/**
 * asc2pet - convert string from ASCII to PETSCII
 * @buf: pointer to the string to be converted
 *
 * This function converts the string in the given buffer from ASCII to
 * PETSCII in-place.
 */
void asc2pet( std::uint8_t* buf )
{
  std::uint8_t ch = 0;
  while ( *buf != 0 )
  {
    ch = *buf;
    if ( ch > 64 && ch < 91 )
      ch = static_cast<std::uint8_t>( ch + 128 );
    else if ( ch > 96 && ch < 123 )
      ch = static_cast<std::uint8_t>( ch - 32 );
    else if ( ch > 192 && ch < 219 )
      ch = static_cast<std::uint8_t>( ch - 128 );
    else if ( ch == '~' )
      ch = 255;
    *buf = ch;
    buf++;
  }
}

// ustring.h

int ustrcasecmp( std::uint8_t const* s1, char const* s2 )
{
  auto const* p2 = reinterpret_cast<std::uint8_t const*>( s2 );
  while ( true )
  {
    int const c1 = asciiToLower( *s1++ );
    int const c2 = asciiToLower( *p2++ );
    if ( c1 != c2 || c1 == 0 )
      return c1 - c2;
  }
}

std::uint8_t* ustrchr( std::uint8_t* s, int c )
{
  auto const ch = static_cast<std::uint8_t>( c );
  while ( true )
  {
    if ( *s == ch )
      return s;
    if ( *s == 0 )
      return nullptr;
    s++;
  }
}

int ustrcmp( std::uint8_t const* s1, std::uint8_t const* s2 )
{
  while ( *s1 != 0 && *s1 == *s2 )
  {
    s1++;
    s2++;
  }
  return int{ *s1 } - int{ *s2 };
}

int ustrcmp( std::uint8_t const* s1, char const* s2 )
{
  return ustrcmp( s1, reinterpret_cast<std::uint8_t const*>( s2 ) );
}

std::uint8_t* ustrcpy( std::uint8_t* s1, std::uint8_t const* s2 )
{
  std::uint8_t* d = s1;
  while ( ( *d++ = *s2++ ) != 0 )
  {
  }
  return s1;
}

std::uint8_t* ustrcpy( std::uint8_t* s1, char const* s2 )
{
  return ustrcpy( s1, reinterpret_cast<std::uint8_t const*>( s2 ) );
}

std::uint8_t* ustrncpy( std::uint8_t* s1, std::uint8_t const* s2, std::size_t n )
{
  std::size_t i = 0;
  for ( ; i < n && s2[i] != 0; i++ )
    s1[i] = s2[i];
  for ( ; i < n; i++ )
    s1[i] = 0;
  return s1;
}

std::uint8_t* ustrncpy( std::uint8_t* s1, char const* s2, std::size_t n )
{
  return ustrncpy( s1, reinterpret_cast<std::uint8_t const*>( s2 ), n );
}

std::size_t ustrlen( std::uint8_t const* s )
{
  std::size_t n = 0;
  while ( s[n] != 0 )
    n++;
  return n;
}

std::uint8_t* ustrrchr( std::uint8_t* s, int c )
{
  auto const ch = static_cast<std::uint8_t>( c );
  std::uint8_t* found = nullptr;
  while ( true )
  {
    if ( *s == ch )
      found = s;
    if ( *s == 0 )
      return found;
    s++;
  }
}

} // namespace pet::sd2pet
