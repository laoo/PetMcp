// Copyright (C) 2007-2018 Ingo Korb <ingo@akana.de>, as NODISKEMU (a fork of sd2iec)
// carries it. GPL-2.0. Ported to C++ for PetMcp, 2026 (THIRD_PARTY.md).

// Ports NODISKEMU's src/p00cache.c at commit e51a6df: the [PSUR]00 name
// cache, whole; the petSD+ is built with CONFIG_P00CACHE. Its statics are
// P00CacheState's.

#include "sd2pet/P00Cache.hpp"

#include "sd2pet/Dos.hpp"

#include <cstring>

namespace pet::sd2pet
{

static_assert( sizeof( P00Name ) == 20, "200 entries, as on the AVR" );

void p00cacheInvalidate( Dos& dos )
{
  dos.p00cache.cachePart = 0xff; // -1
  dos.p00cache.entries = 0;
}

std::uint8_t* p00cacheLookup( Dos& dos, std::uint8_t part, std::uint32_t cluster )
{
  P00CacheState& cache = dos.p00cache;

  /* fail if it's for a different partition */
  if ( part != cache.cachePart )
    return nullptr;

  /* linear search for the correct cluster number */
  /* (binary search was only 6-8% faster overall) */
  for ( unsigned int i = 0; i < cache.entries; i++ )
  {
    if ( cache.p00cache.at( i ).cluster == cluster )
      return cache.p00cache.at( i ).name.data();
  }

  /* noting found */
  return nullptr;
}

void p00cacheAdd( Dos& dos, std::uint8_t part, std::uint32_t cluster, std::uint8_t* name )
{
  P00CacheState& cache = dos.p00cache;

  /* don't add if the cache is full */
  if ( cache.entries == cache.p00cache.size() )
    return;

  /* invalidate if it's for a different partition */
  // FIXME: ignore instead and add invalidate to CP?
  if ( part != cache.cachePart )
  {
    p00cacheInvalidate( dos );
    cache.cachePart = part;
  }

  /* add entry at end of list */
  cache.p00cache.at( cache.entries ).cluster = cluster;
  std::memcpy( cache.p00cache.at( cache.entries ).name.data(), name, CBM_NAME_LENGTH );
  cache.entries++;
}

} // namespace pet::sd2pet
