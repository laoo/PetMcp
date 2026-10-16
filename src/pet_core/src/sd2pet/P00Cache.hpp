#pragma once

// p00cache.c (NODISKEMU at commit e51a6df): the CBM names of the [PSUR]00
// files met, by their cluster.

#include "sd2pet/DosTypes.hpp"

#include <array>
#include <cstdint>

namespace pet::sd2pet
{

/// p00name_t
struct P00Name
{
  std::uint32_t cluster;
  std::array<std::uint8_t, CBM_NAME_LENGTH> name;
};

/// p00cache.c's statics. Zero at power-up as C's are: the cache is for
/// partition 0 and empty until p00cache_invalidate makes it no partition's.
struct P00CacheState
{
  std::array<P00Name, P00CACHE_SIZE / sizeof( P00Name )> p00cache{};
  std::uint8_t cachePart{};
  unsigned int entries{};
};

void p00cacheInvalidate( Dos& dos );
std::uint8_t* p00cacheLookup( Dos& dos, std::uint8_t part, std::uint32_t cluster );
void p00cacheAdd( Dos& dos, std::uint8_t part, std::uint32_t cluster, std::uint8_t* name );

} // namespace pet::sd2pet
