#pragma once

// parser.c (NODISKEMU at commit e51a6df): partitions, paths and the
// matching of names.

#include "sd2pet/DosTypes.hpp"

#include <array>
#include <cstdint>

namespace pet::sd2pet
{

struct ParserState
{
  std::array<Partition, MAX_PARTITIONS> partition{};
  std::uint8_t currentPart{};
  std::uint8_t maxPart{};
  std::uint8_t dirChanged{};
};

void updateCurrentDir( Dos& dos, Path* path );
std::uint8_t parsePartition( Dos& dos, std::uint8_t** buf );
std::uint8_t matchName( Dos& dos, std::uint8_t* matchstr, CbmDirent* dent, std::uint8_t ignorecase );
std::int8_t
nextMatch( Dos& dos, Dh* dh, std::uint8_t* matchstr, Date* start, Date* end, std::uint8_t type, CbmDirent* dent );
std::int8_t firstMatch( Dos& dos, Path* path, std::uint8_t* matchstr, std::uint8_t type, CbmDirent* dent );
std::uint8_t parsePath( Dos& dos, std::uint8_t* in, Path* path, std::uint8_t** name, std::uint8_t forCd );
std::uint8_t checkInvalidName( Dos& dos, std::uint8_t* name );
std::uint16_t parseNumber( Dos& dos, std::uint8_t** str );
std::uint8_t parseDate( Dos& dos, Date* date, std::uint8_t** str );

} // namespace pet::sd2pet
