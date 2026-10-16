#pragma once

// utils.c and ustring.h (NODISKEMU at commit e51a6df): numbers in messages,
// BCD, tokens, and the C string functions on unsigned characters. Each
// ustr* function has the semantics of the C function its macro wraps
// (ustrcasecmp is strcasecmp_P's); a _P variant is the char const* overload.

#include <cstddef>
#include <cstdint>

namespace pet::sd2pet
{

std::uint8_t* appendnumber( std::uint8_t* msg, std::uint8_t value );
std::uint8_t bcd2int( std::uint8_t value );
std::uint8_t int2bcd( std::uint8_t value );
std::uint8_t* ustr1tok( std::uint8_t* str, std::uint8_t delim, std::uint8_t** saveptr );
void asc2pet( std::uint8_t* buf );

// ustring.h
int ustrcasecmp( std::uint8_t const* s1, char const* s2 );
std::uint8_t* ustrchr( std::uint8_t* s, int c );
int ustrcmp( std::uint8_t const* s1, std::uint8_t const* s2 );
int ustrcmp( std::uint8_t const* s1, char const* s2 );
std::uint8_t* ustrcpy( std::uint8_t* s1, std::uint8_t const* s2 );
std::uint8_t* ustrcpy( std::uint8_t* s1, char const* s2 );
std::uint8_t* ustrncpy( std::uint8_t* s1, std::uint8_t const* s2, std::size_t n );
std::uint8_t* ustrncpy( std::uint8_t* s1, char const* s2, std::size_t n );
std::size_t ustrlen( std::uint8_t const* s );
std::uint8_t* ustrrchr( std::uint8_t* s, int c );

} // namespace pet::sd2pet
