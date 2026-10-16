#pragma once

// d64ops.h and d64ops.c of NODISKEMU at commit e51a6df, ported: the Dxx
// images, D64, D71, D81, D80, D82 and DNP, mounted as directories. Nothing of
// the image support is left out; the real-time clock is (see updateTimestamp).

#include "sd2pet/DosTypes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pet::sd2pet
{

/* Offsets in a D64 directory entry, also needed for raw dirs */
inline constexpr std::uint8_t DIR_OFS_FILE_TYPE = 2;
inline constexpr std::uint8_t DIR_OFS_TRACK = 3;
inline constexpr std::uint8_t DIR_OFS_SECTOR = 4;
inline constexpr std::uint8_t DIR_OFS_FILE_NAME = 5;
inline constexpr std::uint8_t DIR_OFS_YEAR = 0x19;
inline constexpr std::uint8_t DIR_OFS_MONTH = 0x1a;
inline constexpr std::uint8_t DIR_OFS_DAY = 0x1b;
inline constexpr std::uint8_t DIR_OFS_HOUR = 0x1c;
inline constexpr std::uint8_t DIR_OFS_MINUTE = 0x1d;
inline constexpr std::uint8_t DIR_OFS_SIZE_LOW = 0x1e;
inline constexpr std::uint8_t DIR_OFS_SIZE_HI = 0x1f;

/* Disk image types - values must match G-P partition type byte */
inline constexpr std::uint8_t D64_TYPE_MASK = 0x7f;
inline constexpr std::uint8_t D64_TYPE_NONE = 0;
inline constexpr std::uint8_t D64_TYPE_DNP = 1;
inline constexpr std::uint8_t D64_TYPE_D41 = 2;
inline constexpr std::uint8_t D64_TYPE_D71 = 3;
inline constexpr std::uint8_t D64_TYPE_D81 = 4;
inline constexpr std::uint8_t D64_TYPE_D80 = 8;
inline constexpr std::uint8_t D64_TYPE_D82 = 16;
inline constexpr std::uint8_t D64_HAS_ERRORINFO = 128;

/* used for error info only */
inline constexpr std::size_t MAX_SECTORS_PER_TRACK = 40;

struct D64State
{
  /// d64ops.c's errorcache: the error info of the track read last.
  struct ErrorCache
  {
    std::uint8_t part;
    std::uint8_t track;
    std::array<std::uint8_t, MAX_SECTORS_PER_TRACK> errors;
  };

  ErrorCache errorcache{};
  /// recently-used buffer
  Buffer* bamBuffer{};
  /// secondary buffer
  Buffer* bamBuffer2{};
  std::uint8_t bamRefcount{};
};

extern FileOps const D64OPS;

std::uint8_t d64Mount( Dos& dos, Path* path, std::uint8_t* name );
void d64Unmount( Dos& dos, std::uint8_t part );

/* commit BAM buffer contents to storage medium */
std::uint8_t d64BamCommit( Dos& dos );

void d64RawDirectory( Dos& dos, Path* path, Buffer* buf );
void d64Invalidate( Dos& dos );

} // namespace pet::sd2pet
