#pragma once

// The types NODISKEMU's DOS passes between its parts: dirent.h, buffers.h and
// wrapops.h, ported (docs/decisions/0010-sd2pet.md). Each keeps its fields, in
// their order, and C's words in this tree's style, so that a grep across the
// two trees still meets: cbmdirent_t is CbmDirent, buffer_t is Buffer, d64fh_t
// is D64Fh. What was a global of the firmware is a member of Dos (Dos.hpp),
// and every function that used one takes the Dos it works on, first.
//
// The configuration is the petSD+'s (NODISKEMU's configs/config-petSD+), the
// board the SD2PET Future descends from, without the EEPROM file system.

#include "sd2pet/HostFs.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pet::sd2pet
{

struct Dos;
struct Buffer;

// config-petSD+
inline constexpr std::size_t BUFFER_COUNT = 32;         // CONFIG_BUFFER_COUNT
inline constexpr std::size_t COMMAND_BUFFER_SIZE = 120; // CONFIG_COMMAND_BUFFER_SIZE
inline constexpr std::size_t ERROR_BUFFER_SIZE = 100;   // CONFIG_ERROR_BUFFER_SIZE
inline constexpr std::size_t P00CACHE_SIZE = 4000;      // CONFIG_P00CACHE_SIZE
/// The card alone: the petSD+'s second partition, its EEPROM, is left out.
inline constexpr std::size_t MAX_PARTITIONS = 1; // CONFIG_MAX_PARTITIONS

// dirent.h
inline constexpr std::uint8_t CBM_NAME_LENGTH = 16;

inline constexpr std::uint8_t TYPE_LENGTH = 3;
inline constexpr std::uint8_t TYPE_MASK = 7;
inline constexpr std::uint8_t EXT_TYPE_MASK = 15;

/* Standard file types */
inline constexpr std::uint8_t TYPE_DEL = 0;
inline constexpr std::uint8_t TYPE_SEQ = 1;
inline constexpr std::uint8_t TYPE_PRG = 2;
inline constexpr std::uint8_t TYPE_USR = 3;
inline constexpr std::uint8_t TYPE_REL = 4;
inline constexpr std::uint8_t TYPE_CBM = 5;
inline constexpr std::uint8_t TYPE_DIR = 6;

/* Internal file types used for the partition directory */
inline constexpr std::uint8_t TYPE_NAT = 8;

/// Hidden is an unused bit on CBM
inline constexpr std::uint8_t FLAG_HIDDEN = 1U << 5U;
inline constexpr std::uint8_t FLAG_RO = 1U << 6U;
inline constexpr std::uint8_t FLAG_SPLAT = 1U << 7U;

/// A file's timestamp: year since 1900, month 1-12, day, hour, minute,
/// second. Compared with memcmp, so its fields run from the largest down and
/// it has no padding.
struct Date
{
  std::uint8_t year;
  std::uint8_t month;
  std::uint8_t day;
  std::uint8_t hour;
  std::uint8_t minute;
  std::uint8_t second;
};

static_assert( sizeof( Date ) == 6 );

/// dir_t: a directory, as the cluster it starts at on FAT (here the HostFs's
/// number for it) and as the track and sector of its first sector in a Dxx.
struct Dir
{
  std::uint32_t fat;

  struct Dxx
  {
    std::uint8_t track;
    std::uint8_t sector;
  } dxx;
};

/// path_t: a directory on a partition.
struct Path
{
  std::uint8_t part;
  Dir dir;
};

/// struct d64dh: an entry in a Dxx directory, by its sector and its place in
/// it (8 a sector).
struct D64Dh
{
  std::uint8_t track;
  std::uint8_t sector;
  std::uint8_t entry;
};

/// opstype_t: which part of CbmDirent's pvt holds.
enum class OpsType : std::uint8_t
{
  UNDEFINED = 0,
  FAT,
  /// X00 files can never be disk images and match case-sensitively.
  FAT_X00,
  DXX
};

/// cbmdirent_t: a directory entry under its CBM name. The name is padded with
/// zeros and always terminated; read from a Dxx it may hold characters past
/// the first zero, which a listing shows and matching ignores. typeflags is
/// the Dxx type byte with its splat bit inverted. realname is the file's 8.3
/// name on FAT when the CBM name was changed from it, or empty.
struct CbmDirent
{
  std::array<std::uint8_t, CBM_NAME_LENGTH + 1> name;
  std::uint8_t typeflags;
  std::uint16_t blocksize;
  std::uint8_t remainder;
  Date date;
  OpsType opstype;

  union Pvt
  {
    struct Fat
    {
      std::uint32_t cluster;
      std::array<std::uint8_t, 8 + 3 + 1 + 1> realname;
    } fat;

    struct Dxx
    {
      D64Dh dh;
    } dxx;
  } pvt;
};

/// d64fh_t: what writing a file in a Dxx needs to update its entry on close.
struct D64Fh
{
  D64Dh dh;
  std::uint8_t part;
  std::uint8_t track;
  std::uint8_t sector;
  std::uint16_t blocks;
};

/// dh_t: a directory being read, of either kind.
struct Dh
{
  std::uint8_t part;

  union Handle
  {
    FatDir fat;
    D64Dh d64;
  } dir;
};

/// param_t: the index of a field of Param, for get_param. Must match it.
enum class ParamIndex : std::uint8_t
{
  DIR_TRACK = 0,
  DIR_START_SECTOR,
  LAST_TRACK,
  LABEL_OFFSET,
  ID_OFFSET,
  FILE_INTERLEAVE,
  DIR_INTERLEAVE
};

using FormatFunction = void ( * )( Dos& dos, std::uint8_t part, Buffer* buf, std::uint8_t* name, std::uint8_t* idbuf );

/// struct param_s: what differs between the Dxx images.
struct Param
{
  std::uint8_t dirTrack;
  std::uint8_t dirStartSector;
  std::uint8_t lastTrack;
  std::uint8_t labelOffset;
  std::uint8_t idOffset;
  std::uint8_t fileInterleave;
  std::uint8_t dirInterleave;
  FormatFunction formatFunction;
};

struct FileOps;

/// partition_t
struct Partition
{
  FatVolume fatfs;
  Dir currentDir;
  FileOps const* fop;
  FatFile imagehandle;
  std::uint8_t imagetype;
  Param d64data;
};

// buffers.h
inline constexpr std::size_t ERRORBUFFER_IDX = BUFFER_COUNT;
inline constexpr std::uint8_t BUFFER_SEC_SYSTEM = 100;

/* Special-purpose buffer numbers */
inline constexpr std::uint8_t BUFFER_SYS_BAM = BUFFER_SEC_SYSTEM + 1;

/* chained buffers use (BUFFER_SEC_CHAIN-14)..BUFFER_SEC_CHAIN */
/* to distinguish secondary addresses */
inline constexpr std::uint8_t BUFFER_SEC_CHAIN = BUFFER_SEC_SYSTEM - 1;

/* Flags for free_multiple_buffers */
inline constexpr std::uint8_t FMB_CLEAN = 1U << 0U;
inline constexpr std::uint8_t FMB_FREE_SYSTEM = 1U << 1U;
inline constexpr std::uint8_t FMB_FREE_STICKY = 1U << 2U;
inline constexpr std::uint8_t FMB_ALL = FMB_FREE_STICKY | FMB_FREE_SYSTEM;
inline constexpr std::uint8_t FMB_ALL_CLEAN = FMB_FREE_STICKY | FMB_FREE_SYSTEM | FMB_CLEAN;
inline constexpr std::uint8_t FMB_USER = FMB_FREE_STICKY;
inline constexpr std::uint8_t FMB_USER_CLEAN = FMB_FREE_STICKY | FMB_CLEAN;
inline constexpr std::uint8_t FMB_UNSTICKY = FMB_FREE_SYSTEM;
inline constexpr std::uint8_t FMB_UNSTICKY_CLEAN = FMB_FREE_SYSTEM | FMB_CLEAN;

enum class DirFormat : std::uint8_t
{
  CBM,
  CMD_SHORT,
  CMD_LONG
};

/// A buffer's callbacks: refill or write it out, and clean up and save what
/// is left; each returns non-zero on an error.
using BufferCallback = std::uint8_t ( * )( Dos& dos, Buffer* buffer );
using SeekCallback = std::uint8_t ( * )( Dos& dos, Buffer* buffer, std::uint32_t position, std::uint8_t index );

/// buffer_t. Most point into Dos's buffer data; the error channel uses the
/// same structure for its own.
struct Buffer
{
  std::uint8_t* data;
  /// The index of the last byte used.
  std::uint8_t lastused;
  /// The index of the byte read or written next.
  std::uint8_t position;
  std::uint8_t secondary;
  /// The record length of a REL file.
  std::uint8_t recordlen;
  std::uint32_t fptr;
  bool allocated;
  /// Must be flushed before characters are added.
  bool mustflush;
  bool read;
  bool write;
  bool dirty;
  /// The last byte goes with EOI.
  bool sendeoi;
  /// Survives garbage collection.
  bool sticky;
  SeekCallback seek;
  BufferCallback refill;
  BufferCallback cleanup;

  /* private: */
  union Pvt
  {
    struct DirPvt
    {
      Dh dh;
      std::uint8_t filetype;
      DirFormat format;
      std::uint8_t* matchstr;
      Date* matchStart;
      Date* matchEnd;
      std::uint8_t counter;
    } dir;

    struct FatPvt
    {
      FatFile fh;
      std::uint8_t headersize;
    } fat;

    D64Fh d64;

    struct PdirPvt
    {
      std::uint8_t part;
      std::uint8_t* matchstr;
    } pdir;

    struct BamPvt
    {
      std::uint8_t part;
      std::uint8_t track;
      std::uint8_t sector;
    } bam;

    struct BufferPvt
    {
      std::uint8_t part;
      std::uint8_t size;
      Buffer* first;
      Buffer* next;
    } buffer;
  } pvt;
};

/// fileops_t: the operations on a partition, for FAT and for a Dxx image. No
/// field may be null.
struct FileOps
{
  void ( *openRead )( Dos& dos, Path* path, CbmDirent* name, Buffer* buf );
  void ( *openWrite )( Dos& dos, Path* path, CbmDirent* name, std::uint8_t type, Buffer* buf, std::uint8_t append );
  void ( *openRel )( Dos& dos, Path* path, CbmDirent* name, Buffer* buf, std::uint8_t recordlen, std::uint8_t mode );
  std::uint8_t ( *fileDelete )( Dos& dos, Path* path, CbmDirent* name );
  std::uint8_t ( *diskLabel )( Dos& dos, std::uint8_t part, std::uint8_t* label );
  std::uint8_t ( *dirLabel )( Dos& dos, Path* path, std::uint8_t* label );
  std::uint8_t ( *diskId )( Dos& dos, Path* path, std::uint8_t* id );
  std::uint16_t ( *diskFree )( Dos& dos, std::uint8_t part );
  void ( *readSector )( Dos& dos, Buffer* buf, std::uint8_t part, std::uint8_t track, std::uint8_t sector );
  void ( *writeSector )( Dos& dos, Buffer* buf, std::uint8_t part, std::uint8_t track, std::uint8_t sector );
  void ( *format )( Dos& dos, std::uint8_t drv, std::uint8_t* name, std::uint8_t* id );
  std::uint8_t ( *opendir )( Dos& dos, Dh* dh, Path* path );
  std::int8_t ( *readdir )( Dos& dos, Dh* dh, CbmDirent* dent );
  void ( *mkdir )( Dos& dos, Path* path, std::uint8_t* dirname );
  std::uint8_t ( *chdir )( Dos& dos, Path* path, CbmDirent* dent );
  void ( *rename )( Dos& dos, Path* path, CbmDirent* oldname, std::uint8_t* newname );
};

// flags.h
inline constexpr std::uint8_t VC20MODE = 1U << 0U;
inline constexpr std::uint8_t AUTOSWAP_ACTIVE = 1U << 2U;
inline constexpr std::uint8_t SWAPLIST_ASCII = 1U << 5U;
inline constexpr std::uint8_t EXTENSION_HIDING = 1U << 3U;
inline constexpr std::uint8_t POSTMATCH = 1U << 4U;

inline constexpr std::uint8_t IMAGE_DIR_NORMAL = 0;
inline constexpr std::uint8_t IMAGE_DIR_DIR = 1;
inline constexpr std::uint8_t IMAGE_DIR_BOTH = 2;

} // namespace pet::sd2pet
