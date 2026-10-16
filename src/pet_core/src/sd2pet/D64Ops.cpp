// d64ops.c of NODISKEMU at commit e51a6df, ported: D64 operations. Every
// image type it supports is here, recognised by its size as d64_mount does.
// Left out: the real-time clock. The petSD+ is ported as one with no RTC chip
// found, so a new or changed directory entry is stamped with the date the RTC
// drivers fall back to (updateTimestamp).

#include "sd2pet/D64Ops.hpp"

#include "sd2pet/Dos.hpp"
#include "sd2pet/Utils.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>

namespace pet::sd2pet
{

namespace
{

constexpr std::uint32_t D41_ERROR_OFFSET = 174848;
constexpr std::uint32_t D71_ERROR_OFFSET = 349696;
constexpr std::uint32_t D81_ERROR_OFFSET = 819200;
constexpr std::uint32_t D80_ERROR_OFFSET = 533248;
constexpr std::uint32_t D82_ERROR_OFFSET = 1066496;

constexpr std::uint8_t D41_BAM_TRACK = 18;
constexpr std::uint8_t D41_BAM_SECTOR = 0;
constexpr std::uint8_t D41_BAM_BYTES_PER_TRACK = 4;

constexpr std::uint8_t D81_BAM_TRACK = 40;
constexpr std::uint8_t D81_BAM_SECTOR1 = 1;
constexpr std::uint8_t D81_BAM_SECTOR2 = 2;
constexpr std::uint8_t D81_BAM_OFFSET = 10;
constexpr std::uint8_t D81_BAM_BYTES_PER_TRACK = 6;

constexpr std::uint8_t D71_BAM2_TRACK = 53;
constexpr std::uint8_t D71_BAM2_SECTOR = 0;
constexpr std::uint8_t D71_BAM2_BYTES_PER_TRACK = 3;
constexpr std::uint8_t D71_BAM_COUNTER2OFFSET = 0xdd;

constexpr std::uint8_t DNP_BAM_TRACK = 1;
constexpr std::uint8_t DNP_BAM_SECTOR = 2;
constexpr std::uint8_t DNP_BAM_LAST_TRACK_OFS = 8;
constexpr std::uint8_t DNP_ROOTDIR_SECTOR = 34; // only for formatting!
constexpr std::uint8_t DNP_BAM_BYTES_PER_TRACK = 32;
constexpr std::uint8_t DNP_DIRHEADER_ROOTHDR_TRACK = 32;
constexpr std::uint8_t DNP_DIRHEADER_ROOTHDR_SECTOR = 33;
constexpr std::uint8_t DNP_DIRHEADER_PARENTHDR_TRACK = 34;
constexpr std::uint8_t DNP_DIRHEADER_PARENTENTRY_TRACK = 36;
constexpr std::uint8_t DNP_LABEL_OFFSET = 4;
constexpr std::uint8_t DNP_LABEL_AREA_SIZE = 28 - 4 + 1;
constexpr std::uint8_t DNP_ID_OFFSET = 22;

constexpr std::uint8_t D80_BAM_TRACK = 38;
constexpr std::uint8_t D80_DIR_TRACK = 39;
constexpr std::uint8_t D80_BAM_SECTOR = 0;
constexpr std::uint8_t D80_BAM_INTERLEAVE = 3;
constexpr std::uint8_t D80_BAM_BYTES_PER_TRACK = 5;

enum class BamData : std::uint8_t
{
  BAM_BITFIELD,
  BAM_FREECOUNT
};

/* ------------------------------------------------------------------------- */
/*  Forward declarations                                                     */
/* ------------------------------------------------------------------------- */

std::uint8_t d64Opendir( Dos& dos, Dh* dh, Path* path );

void formatD41Image( Dos& dos, std::uint8_t part, Buffer* buf, std::uint8_t* name, std::uint8_t* idbuf );
void formatD71Image( Dos& dos, std::uint8_t part, Buffer* buf, std::uint8_t* name, std::uint8_t* idbuf );
void formatD81Image( Dos& dos, std::uint8_t part, Buffer* buf, std::uint8_t* name, std::uint8_t* idbuf );
void formatD80Image( Dos& dos, std::uint8_t part, Buffer* buf, std::uint8_t* name, std::uint8_t* idbuf );
void formatD82Image( Dos& dos, std::uint8_t part, Buffer* buf, std::uint8_t* name, std::uint8_t* idbuf );
void formatDnpImage( Dos& dos, std::uint8_t part, Buffer* buf, std::uint8_t* name, std::uint8_t* idbuf );

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

constexpr Param D41PARAM = { .dirTrack = 18,
                             .dirStartSector = 1,
                             .lastTrack = 35,
                             .labelOffset = 0x90,
                             .idOffset = 0xa2,
                             .fileInterleave = 10,
                             .dirInterleave = 3,
                             .formatFunction = formatD41Image };

constexpr Param D71PARAM = { .dirTrack = 18,
                             .dirStartSector = 1,
                             .lastTrack = 70,
                             .labelOffset = 0x90,
                             .idOffset = 0xa2,
                             .fileInterleave = 6,
                             .dirInterleave = 3,
                             .formatFunction = formatD71Image };

constexpr Param D81PARAM = { .dirTrack = 40,
                             .dirStartSector = 3,
                             .lastTrack = 80,
                             .labelOffset = 0x04,
                             .idOffset = 0x16,
                             .fileInterleave = 1,
                             .dirInterleave = 1,
                             .formatFunction = formatD81Image };

constexpr Param DNPPARAM = { .dirTrack = 1,
                             .dirStartSector = 1,
                             .lastTrack = 0,
                             .labelOffset = DNP_LABEL_OFFSET,
                             .idOffset = DNP_ID_OFFSET,
                             .fileInterleave = 1,
                             .dirInterleave = 1,
                             .formatFunction = formatDnpImage };

constexpr Param D80PARAM = { .dirTrack = 39,
                             .dirStartSector = 1,
                             .lastTrack = 77,
                             .labelOffset = 6,
                             .idOffset = 0x18,
                             .fileInterleave = 5,
                             .dirInterleave = 3,
                             .formatFunction = formatD80Image };

constexpr Param D82PARAM = { .dirTrack = 39,
                             .dirStartSector = 1,
                             .lastTrack = 154,
                             .labelOffset = 6,
                             .idOffset = 0x18,
                             .fileInterleave = 5,
                             .dirInterleave = 3,
                             .formatFunction = formatD82Image };

/// partition[part]
Partition& partitionAt( Dos& dos, std::uint8_t part )
{
  return dos.parser.partition.at( part );
}

/**
 * get_param - get a format-dependent parameter
 * @part : partition number
 * @param: parameter to retrieve
 *
 * Returns a parameter that is format-dependent for the image type mounted
 * on the specified partition.
 */
std::uint8_t getParam( Dos& dos, std::uint8_t part, ParamIndex param )
{
  // The C reads the byte at the offset param into struct param_s.
  Param const& d64data = partitionAt( dos, part ).d64data;
  switch ( param )
  {
  case ParamIndex::DIR_TRACK:
    return d64data.dirTrack;
  case ParamIndex::DIR_START_SECTOR:
    return d64data.dirStartSector;
  case ParamIndex::LAST_TRACK:
    return d64data.lastTrack;
  case ParamIndex::LABEL_OFFSET:
    return d64data.labelOffset;
  case ParamIndex::ID_OFFSET:
    return d64data.idOffset;
  case ParamIndex::FILE_INTERLEAVE:
    return d64data.fileInterleave;
  case ParamIndex::DIR_INTERLEAVE:
    return d64data.dirInterleave;
  }
  return 0;
}

/**
 * sector_lba - Transform a track/sector pair into a LBA sector number
 * @part  : partition number
 * @track : Track number
 * @sector: Sector number
 *
 * Calculates an LBA-style sector number for a given track/sector pair.
 */
/* This version used the least code of all tested variants. */
std::uint16_t sectorLba( Dos& dos, std::uint8_t part, std::uint8_t track, std::uint8_t const sector )
{
  std::uint16_t offset = 0;

  track--; /* Track numbers are 1-based */

  switch ( partitionAt( dos, part ).imagetype & D64_TYPE_MASK )
  {
  case D64_TYPE_D41:
  case D64_TYPE_D71:
  default:
    if ( track >= 35 )
    {
      offset = 683;
      track -= 35;
    }
    if ( track < 17 )
      return static_cast<std::uint16_t>( ( track * 21 ) + sector + offset );
    if ( track < 24 )
      return static_cast<std::uint16_t>( ( 17 * 21 ) + ( ( track - 17 ) * 19 ) + sector + offset );
    if ( track < 30 )
      return static_cast<std::uint16_t>( ( 17 * 21 ) + ( 7 * 19 ) + ( ( track - 24 ) * 18 ) + sector + offset );
    return static_cast<std::uint16_t>( ( 17 * 21 ) + ( 7 * 19 ) + ( 6 * 18 ) + ( ( track - 30 ) * 17 ) + sector +
                                       offset );

  case D64_TYPE_D81:
    return static_cast<std::uint16_t>( ( track * 40 ) + sector );

  case D64_TYPE_DNP:
    return static_cast<std::uint16_t>( ( track * 256 ) + sector );

  case D64_TYPE_D80:
  case D64_TYPE_D82:
    if ( track >= 78 )
    {
      offset = 2083;
      track -= 77;
    }
    if ( track < 39 )
      return static_cast<std::uint16_t>( ( track * 29 ) + sector + offset );
    if ( track < 53 )
      return static_cast<std::uint16_t>( ( 39 * 29 ) + ( ( track - 39 ) * 27 ) + sector + offset );
    if ( track < 64 )
      return static_cast<std::uint16_t>( ( 39 * 29 ) + ( 14 * 27 ) + ( ( track - 53 ) * 25 ) + sector + offset );
    return static_cast<std::uint16_t>( ( 39 * 29 ) + ( 14 * 27 ) + ( 11 * 25 ) + ( ( track - 64 ) * 23 ) + sector +
                                       offset );
  }
}

/**
 * sector_offset - Transform a track/sector pair into a D64 offset
 * @part  : partition number
 * @track : Track number
 * @sector: Sector number
 *
 * Calculates an offset into a D64 file from a track and sector number.
 */
std::uint32_t sectorOffset( Dos& dos, std::uint8_t part, std::uint8_t track, std::uint8_t const sector )
{
  return 256U * sectorLba( dos, part, track, sector );
}

/**
 * sectors_per_track - number of sectors on given track
 * @part : partition number
 * @track: Track number
 *
 * This function returns the number of sectors on the given track
 * of a 1541/71/81 disk. Invalid track numbers will return invalid results.
 */
std::uint16_t sectorsPerTrack( Dos& dos, std::uint8_t part, std::uint8_t track )
{
  switch ( partitionAt( dos, part ).imagetype & D64_TYPE_MASK )
  {
  case D64_TYPE_D41:
  case D64_TYPE_D71:
  default:
    if ( track > 35 )
      track -= 35;
    if ( track < 18 )
      return 21;
    if ( track < 25 )
      return 19;
    if ( track < 31 )
      return 18;
    return 17;

  case D64_TYPE_D81:
    return 40;

  case D64_TYPE_DNP:
    return 256;

  case D64_TYPE_D80:
  case D64_TYPE_D82:
    if ( track > 77 )
      track -= 77;
    if ( track < 40 )
      return 29;
    if ( track < 54 )
      return 27;
    if ( track < 65 )
      return 25;
    return 23;
  }
}

/**
 * checked_read - read a specified sector after range-checking
 * @part  : partition number
 * @track : track number to be read
 * @sector: sector number to be read
 * @buf   : pointer to where the data should be read to
 * @len   : number of bytes to be read
 * @error : error number to be flagged if the range check fails
 *
 * This function checks if the track and sector are within the
 * limits for the image format and calls image_read to read
 * the data if they are. Returns the result of image_read or
 * 2 if the range check failed.
 */
std::uint8_t checkedRead( Dos& dos,
                          std::uint8_t part,
                          std::uint8_t track,
                          std::uint8_t sector,
                          std::uint8_t* buf,
                          std::uint16_t len,
                          std::uint8_t error )
{
  if ( track < 1 || track > getParam( dos, part, ParamIndex::LAST_TRACK ) ||
       sector >= sectorsPerTrack( dos, part, track ) )
  {
    setErrorTs( dos, error, track, sector );
    return 2;
  }

  if ( ( partitionAt( dos, part ).imagetype & D64_HAS_ERRORINFO ) != 0 )
  {
    D64State::ErrorCache& errorcache = dos.d64.errorcache;

    /* Check if the sector is marked as bad */
    if ( errorcache.part != part || errorcache.track != track )
    {
      /* Read the error info for this track */
      errorcache.errors.fill( 1 );

      switch ( partitionAt( dos, part ).imagetype & D64_TYPE_MASK )
      {
      case D64_TYPE_D41:
        if ( imageRead( dos,
                        part,
                        D41_ERROR_OFFSET + sectorLba( dos, part, track, 0 ),
                        errorcache.errors.data(),
                        sectorsPerTrack( dos, part, track ) ) >= 2 )
          return 2;
        break;

      case D64_TYPE_D71:
        if ( imageRead( dos,
                        part,
                        D71_ERROR_OFFSET + sectorLba( dos, part, track, 0 ),
                        errorcache.errors.data(),
                        sectorsPerTrack( dos, part, track ) ) >= 2 )
          return 2;
        break;

      case D64_TYPE_D81:
        if ( imageRead( dos,
                        part,
                        D81_ERROR_OFFSET + sectorLba( dos, part, track, 0 ),
                        errorcache.errors.data(),
                        sectorsPerTrack( dos, part, track ) ) >= 2 )
          return 2;
        break;

      case D64_TYPE_D80:
        if ( imageRead( dos,
                        part,
                        D80_ERROR_OFFSET + sectorLba( dos, part, track, 0 ),
                        errorcache.errors.data(),
                        sectorsPerTrack( dos, part, track ) ) >= 2 )
          return 2;
        break;

      case D64_TYPE_D82:
        if ( imageRead( dos,
                        part,
                        D82_ERROR_OFFSET + sectorLba( dos, part, track, 0 ),
                        errorcache.errors.data(),
                        sectorsPerTrack( dos, part, track ) ) >= 2 )
          return 2;
        break;

      default:
        /* Should not happen unless someone enables error info
           for additional image formats */
        return 2;
      }
      errorcache.part = part;
      errorcache.track = track;
    }

    /* Calculate error message from the code */
    if ( errorcache.errors[sector] >= 2 && errorcache.errors[sector] <= 11 )
    {
      /* Most codes can be mapped directly */
      setErrorTs( dos, static_cast<std::uint8_t>( errorcache.errors[sector] - 2 + 20 ), track, sector );
      return 2;
    }
    if ( errorcache.errors[sector] == 15 )
    {
      /* Drive not ready */
      setError( dos, 74 );
      return 2;
    }
    /* 1 is OK, unknown values are accepted too */
  }

  return imageRead( dos, part, sectorOffset( dos, part, track, sector ), buf, len );
}

/**
 * update_timestamp - update timestamp of a directory entry
 * @buffer: pointer to the directory entry
 *
 * This function updates the time stamp of a directory entry
 * at the address pointed to by @buffer with the current
 * date/time if available or the default timestamp if not.
 */
void updateTimestamp( std::uint8_t* buffer )
{
  // The petSD+ has HAVE_RTC, and with no RTC chip found read_rtc returns
  // without writing its struct tm (rtc.c's RTC_NONE), so the C stamps what
  // the stack held. This port stamps the date each RTC driver falls back to
  // when its chip is absent (rtc.c's rtc_default_date), which is also what
  // the C writes without HAVE_RTC: 1982-08-31 00:00.
  buffer[DIR_OFS_YEAR] = 82;
  buffer[DIR_OFS_MONTH] = 8;
  buffer[DIR_OFS_DAY] = 31;
  buffer[DIR_OFS_HOUR] = 0;
  buffer[DIR_OFS_MINUTE] = 0;
}

/**
 * write_entry - write a single directory entry
 * @part : partition number
 * @dh   : pointer to d64dh pointing to the entry
 * @buf  : pointer to the buffer where the data should be read from
 * @flush: if true, data is flushed to disk immediately
 *
 * This function writes a single directory entry specified by @dh
 * from the buffer @buf.
 * Assumes that it is never called with an invalid track/sector.
 * Returns the same as image_write
 */
std::uint8_t writeEntry( Dos& dos, std::uint8_t part, D64Dh* dh, std::uint8_t* buf, std::uint8_t flush )
{
  return imageWrite(
      dos, part, sectorOffset( dos, part, dh->track, dh->sector ) + ( dh->entry * 32U ), buf, 32, flush );
}

/**
 * read_entry - read a single directory entry
 * @part: partition number
 * @dh  : pointer to d64dh pointing to the entry
 * @buf : pointer to the buffer where the data should be stored
 *
 * This function reads a single directory entry specified by dh
 * into the buffer buf which needs to be at least 32 bytes big.
 * Assumes that it is never called with an invalid track/sector.
 * Returns the same as image_read (0 success, 1 partial read, 2 failed)
 */
std::uint8_t readEntry( Dos& dos, std::uint8_t part, D64Dh* dh, std::uint8_t* buf )
{
  return imageRead( dos, part, sectorOffset( dos, part, dh->track, dh->sector ) + ( dh->entry * 32U ), buf, 32 );
}

/**
 * strnsubst - substitute one character with another in a buffer
 * @buffer : pointer to the buffer
 * @len    : length of buffer
 * @oldchar: character to be replaced
 * @newchar: character to be used as a replacement
 *
 * This functions changes all occurences of oldchar in the first len
 * byte of buffer with newchar. Although it is named str*, it doesn't
 * care about zero bytes in any way.
 */
void strnsubst( std::uint8_t* buffer, std::uint8_t len, std::uint8_t oldchar, std::uint8_t newchar )
{
  auto i = static_cast<std::uint8_t>( len - 1 );

  do
  {
    if ( buffer[i] == oldchar )
      buffer[i] = newchar;
  } while ( i-- != 0 );
}

/* fill a sector with 0 0xff 0* to generate a new empty directory sector */
/* needs a 256 byte work area in *data */
std::uint8_t clearDirSector( Dos& dos, std::uint8_t part, std::uint8_t t, std::uint8_t s, std::uint8_t* data )
{
  std::memset( data, 0, 256 );

  data[1] = 0xff;
  return imageWrite( dos, part, sectorOffset( dos, part, t, s ), data, 256, 0 );
}

/* ------------------------------------------------------------------------- */
/*  BAM buffer handling                                                      */
/* ------------------------------------------------------------------------- */

/**
 * bam_buffer_flush - write BAM buffer to disk
 * @buf: pointer to the BAM buffer
 *
 * This function writes the contents of the BAM buffer to the disk image.
 * Returns 0 if successful, != 0 otherwise.
 */
std::uint8_t bamBufferFlush( Dos& dos, Buffer* buf )
{
  if ( buf->mustflush && buf->pvt.bam.part < dos.parser.maxPart )
  {
    std::uint8_t const res =
        imageWrite( dos,
                    buf->pvt.bam.part,
                    sectorOffset( dos, buf->pvt.bam.part, buf->pvt.bam.track, buf->pvt.bam.sector ),
                    buf->data,
                    256,
                    1 );
    buf->mustflush = false;

    return res;
  }
  return 0;
}

} // namespace

/**
 * d64_bam_commit - write BAM buffers to disk
 *
 * This function is the exported interface to force the BAM buffer
 * contents to disk. Returns 0 if successful, != 0 otherwise.
 */
std::uint8_t d64BamCommit( Dos& dos )
{
  std::uint8_t res = 0;

  if ( dos.d64.bamBuffer != nullptr )
    res |= dos.d64.bamBuffer->cleanup( dos, dos.d64.bamBuffer );

  if ( dos.d64.bamBuffer2 != nullptr )
    res |= dos.d64.bamBuffer2->cleanup( dos, dos.d64.bamBuffer2 );

  // As in the C, the results are gathered and 0 is returned regardless.
  static_cast<void>( res );
  return 0;
}

namespace
{

/**
 * bam_buffer_alloc - allocates a buffer for the BAM
 * @buf: pointer to the BAM buffer pointer
 *
 * This function tries to allocate a buffer for holding the BAM,
 * using the pointer pointed to by @buf. Returns 0 if successful,
 * != 0 otherwise.
 */
std::uint8_t bamBufferAlloc( Dos& dos, Buffer** buf )
{
  *buf = allocSystemBuffer( dos );
  if ( *buf == nullptr )
    return 1;

  ( *buf )->secondary = BUFFER_SYS_BAM;
  ( *buf )->pvt.bam.part = 255;
  ( *buf )->cleanup = bamBufferFlush;
  stickBuffer( *buf );

  return 0;
}

/**
 * bam_buffer_swap - swap the two BAM buffers
 *
 * Thus function swaps the pointers to the two BAM buffers
 */
void bamBufferSwap( Dos& dos )
{
  std::swap( dos.d64.bamBuffer, dos.d64.bamBuffer2 );
}

/**
 * bam_buffer_match - checks if a BAM buffer contains a specific sector
 * @buf   : BAM buffer to check
 * @part  : partition
 * @track : track
 * @sector: sector
 *
 * This function checks if the BAM buffer @buf currently holds the data
 * for the given @part, @track and @sector. Returns 1 if it does,
 * 0 otherwise.
 */
std::uint8_t bamBufferMatch( Buffer const* buf, std::uint8_t part, std::uint8_t track, std::uint8_t sector )
{
  if ( buf->pvt.bam.part == part && buf->pvt.bam.track == track && buf->pvt.bam.sector == sector )
    return 1;
  return 0;
}

/**
 * move_bam_window - read correct BAM sector into window.
 * @part  : partition
 * @track : track number
 * @type  : type of pointer requested
 * @ptr   : pointer to track information in BAM sector
 *
 * This function reads the correct BAM sector into memory for the requested
 * track, flushing an existing BAM sector to disk if needed.  It also
 * calculates the correct pointer into the BAM sector for the appropriate
 * track.  Since the BAM contains both sector counts and sector allocation
 * bitmaps, type is used to signal which reference is desired.
 * This function may swap the BAM buffer pointers, after it returns
 * bam_buffer is always the buffer with the requested sector.
 * Returns 0 if successful, != 0 otherwise.
 */
std::uint8_t moveBamWindow( Dos& dos, std::uint8_t part, std::uint8_t track, BamData type, std::uint8_t** ptr )
{
  std::uint8_t res = 0;
  std::uint8_t t = 0;
  std::uint8_t s = 0;
  std::uint8_t pos = 0;
  std::uint8_t const bitfield = type == BamData::BAM_BITFIELD ? 1 : 0;

  switch ( partitionAt( dos, part ).imagetype & D64_TYPE_MASK )
  {
  case D64_TYPE_D41:
  default:
    t = D41_BAM_TRACK;
    s = D41_BAM_SECTOR;
    pos = static_cast<std::uint8_t>( ( D41_BAM_BYTES_PER_TRACK * track ) + bitfield );
    break;

  case D64_TYPE_D71:
    if ( track > 35 && type == BamData::BAM_BITFIELD )
    {
      t = D71_BAM2_TRACK;
      s = D71_BAM2_SECTOR;
      pos = static_cast<std::uint8_t>( ( track - 36 ) * D71_BAM2_BYTES_PER_TRACK );
    }
    else
    {
      t = D41_BAM_TRACK;
      s = D41_BAM_SECTOR;
      if ( track > 35 )
      {
        pos = static_cast<std::uint8_t>( ( track - 36 ) + D71_BAM_COUNTER2OFFSET );
      }
      else
      {
        pos = static_cast<std::uint8_t>( ( D41_BAM_BYTES_PER_TRACK * track ) + bitfield );
      }
    }
    break;

  case D64_TYPE_D81:
    t = D81_BAM_TRACK;
    s = ( track < 41 ? D81_BAM_SECTOR1 : D81_BAM_SECTOR2 );
    if ( track > 40 )
      track -= 40;
    pos = static_cast<std::uint8_t>( D81_BAM_OFFSET + ( track * D81_BAM_BYTES_PER_TRACK ) + bitfield );
    break;

  case D64_TYPE_DNP:
    t = DNP_BAM_TRACK;
    s = static_cast<std::uint8_t>( DNP_BAM_SECTOR + ( track >> 3U ) );
    pos = static_cast<std::uint8_t>( ( track & 0x07U ) * 32 );
    break;

  case D64_TYPE_D80:
  case D64_TYPE_D82:
    t = D80_BAM_TRACK;
    s = D80_BAM_SECTOR;
    while ( track > 50 )
    {
      s += D80_BAM_INTERLEAVE;
      track -= 50;
    }
    pos = static_cast<std::uint8_t>( 1 + ( D80_BAM_BYTES_PER_TRACK * track ) + bitfield );
    break;
  }

  if ( bamBufferMatch( dos.d64.bamBuffer, part, t, s ) == 0 )
  {
    /* check if the second BAM buffer exists */
    if ( dos.d64.bamBuffer2 != nullptr )
    {
      /* unconditionally swap to the other BAM buffer        */
      /* either it has the data or it was less-recently used */

      /* Note for future optimization:
       * If buf1 is invalid and buf2 valid but not matching this will swap and
       * use buf2 for the data instead of buf1 - using buf1
       * while keeping buf2 without swapping would be better.
       */
      bamBufferSwap( dos );

      /* check again, now in the other buffer */
      if ( bamBufferMatch( dos.d64.bamBuffer, part, t, s ) != 0 )
        goto found;
    }
    else
    {
      /* allocate and swap to the second BAM buffer if the first one is valid */
      if ( dos.d64.bamBuffer->pvt.bam.part != 255 )
      {
        if ( bamBufferAlloc( dos, &dos.d64.bamBuffer2 ) != 0 )
        {
          /* allocation failed, reset error and continue with just one buffer */
          setError( dos, ERROR_OK );
        }
        else
        {
          /* switch to the new buffer */
          bamBufferSwap( dos );
        }
      }
    }

    /* Need to read the BAM sector - flush only the target buffer */
    if ( dos.d64.bamBuffer->cleanup( dos, dos.d64.bamBuffer ) != 0 )
      return 1;

    res = imageRead( dos, part, sectorOffset( dos, part, t, s ), dos.d64.bamBuffer->data, 256 );
    if ( res != 0 )
      return res;

    dos.d64.bamBuffer->pvt.bam.part = part;
    dos.d64.bamBuffer->pvt.bam.track = t;
    dos.d64.bamBuffer->pvt.bam.sector = s;
  }

found:
  *ptr = dos.d64.bamBuffer->data + pos;
  return 0;
}

/**
 * is_free - checks if the given sector is marked as free
 * @part  : partition
 * @track : track number
 * @sector: sector number
 *
 * This function checks if the given sector is marked as free in
 * the BAM of drive "part". Returns 0 if allocated, >0 if free, <0 on error.
 */
std::int8_t isFree( Dos& dos, std::uint8_t part, std::uint8_t track, std::uint8_t sector )
{
  std::uint8_t res = 0;
  std::uint8_t* ptr = nullptr;

  res = moveBamWindow( dos, part, track, BamData::BAM_BITFIELD, &ptr );
  if ( res != 0 )
    return -1;

  if ( partitionAt( dos, part ).imagetype == D64_TYPE_DNP )
    return ( ptr[sector >> 3U] & ( 0x80U >> ( sector & 7U ) ) ) != 0 ? 1 : 0;
  return ( ptr[sector >> 3U] & ( 1U << ( sector & 7U ) ) ) != 0 ? 1 : 0;
}

/**
 * sectors_free - returns the number of free sectors on a given track
 * @part  : partition
 * @track : track number
 *
 * This function returns the number of free sectors on the given track
 * of partition part.
 */
std::uint16_t sectorsFree( Dos& dos, std::uint8_t part, std::uint8_t track )
{
  std::uint8_t* trackmap = nullptr;

  if ( track < 1 || track > getParam( dos, part, ParamIndex::LAST_TRACK ) )
    return 0;

  switch ( partitionAt( dos, part ).imagetype & D64_TYPE_MASK )
  {

  case D64_TYPE_DNP:
  {
    if ( moveBamWindow( dos, part, track, BamData::BAM_FREECOUNT, &trackmap ) != 0 )
      return 0;

    std::uint16_t blocks = 0;
    for ( std::uint8_t i = 0; i < DNP_BAM_BYTES_PER_TRACK; i++ )
    {
      // From http://everything2.com/title/counting%25201%2520bits
      auto b = static_cast<std::uint8_t>( ( trackmap[i] & 0x55U ) + ( ( trackmap[i] >> 1U ) & 0x55U ) );
      b = static_cast<std::uint8_t>( ( b & 0x33U ) + ( ( b >> 2U ) & 0x33U ) );
      b = static_cast<std::uint8_t>( ( b & 0x0fU ) + ( ( b >> 4U ) & 0x0fU ) );
      blocks = static_cast<std::uint16_t>( blocks + b );
    }
    return blocks;
  }

  case D64_TYPE_D71:
  case D64_TYPE_D81:
  case D64_TYPE_D41:
  case D64_TYPE_D80:
  case D64_TYPE_D82:
  default:
    if ( moveBamWindow( dos, part, track, BamData::BAM_FREECOUNT, &trackmap ) != 0 )
      return 0;
    return *trackmap;
  }
}

/**
 * allocate_sector - mark a sector as used
 * @part  : partitoin
 * @track : track number
 * @sector: sector number
 *
 * This function marks the given sector as used in the BAM of drive part.
 * If the sector was already marked as used nothing is changed.
 * Returns 0 if successful, 1 on error.
 */
std::uint8_t allocateSector( Dos& dos, std::uint8_t part, std::uint8_t track, std::uint8_t sector )
{
  std::uint8_t* trackmap = nullptr;
  std::int8_t const res = isFree( dos, part, track, sector );

  if ( res < 0 )
    return 1;

  if ( res != 0 )
  {
    /* do the bitfield first, since is_free already set it up for us. */
    if ( moveBamWindow( dos, part, track, BamData::BAM_BITFIELD, &trackmap ) != 0 )
      return 1;

    dos.d64.bamBuffer->mustflush = true;

    if ( partitionAt( dos, part ).imagetype == D64_TYPE_DNP )
    {
      /* For some reason DNP has its bitfield reversed */
      trackmap[sector >> 3U] &= static_cast<std::uint8_t>( ~( 0x80U >> ( sector & 7U ) ) );

      /* DNP has no counter in its BAM */
      return 0;
    }

    trackmap[sector >> 3U] &= static_cast<std::uint8_t>( ~( 1U << ( sector & 7U ) ) );

    if ( moveBamWindow( dos, part, track, BamData::BAM_FREECOUNT, &trackmap ) != 0 )
      return 1;

    if ( trackmap[0] > 0 )
    {
      trackmap[0]--;
      dos.d64.bamBuffer->mustflush = true;
    }
  }
  return 0;
}

/**
 * free_sector - mark a sector as free
 * @part  : partition
 * @track : track number
 * @sector: sector number
 *
 * This function marks the given sector as free in the BAM of partition
 * part. If the sector was already marked as free nothing is changed.
 * Returns 0 if successful, 1 on error.
 */
std::uint8_t freeSector( Dos& dos, std::uint8_t part, std::uint8_t track, std::uint8_t sector )
{
  std::uint8_t* trackmap = nullptr;
  std::int8_t const res = isFree( dos, part, track, sector );

  if ( res < 0 )
    return 1;

  if ( res == 0 )
  {
    /* do the bitfield first, since is_free already set it up for us. */
    if ( moveBamWindow( dos, part, track, BamData::BAM_BITFIELD, &trackmap ) != 0 )
      return 1;

    dos.d64.bamBuffer->mustflush = true;

    if ( partitionAt( dos, part ).imagetype == D64_TYPE_DNP )
    {
      /* For some reason DNP has its bitfield reversed */
      trackmap[sector >> 3U] |= static_cast<std::uint8_t>( 0x80U >> ( sector & 7U ) );

      /* DNP has no counter in its BAM */
      return 0;
    }

    trackmap[sector >> 3U] |= static_cast<std::uint8_t>( 1U << ( sector & 7U ) );

    if ( moveBamWindow( dos, part, track, BamData::BAM_FREECOUNT, &trackmap ) != 0 )
      return 1;

    if ( trackmap[0] < sectorsPerTrack( dos, part, track ) )
    {
      trackmap[0]++;
      dos.d64.bamBuffer->mustflush = true;
    }
  }
  return 0;
}

/**
 * get_first_sector - calculate the first sector for a new file
 * @part  : partition
 * @track : pointer to a variable holding the track
 * @sector: pointer to a variable holding the sector
 *
 * This function calculates the first sector to be allocated for a new
 * file. The algorithm is based on the description found at
 * http://ist.uwaterloo.ca/~schepers/formats/DISK.TXT
 * Returns 0 if successful or 1 if any error occured.
 *
 * This code will not skip track 53 of a D71 if there are any free
 * sectors on it - this behaviour is consistent with that of a 1571
 * with a revision 3.0 ROM.
 */
std::uint8_t getFirstSector( Dos& dos, std::uint8_t part, std::uint8_t* track, std::uint8_t* sector )
{
  std::int8_t distance = 1;

  /* DNP uses a simple "first free" allocation scheme, starting at track 2.  */
  /* It is not known if this is the same algorithm as used in the original   */
  /* CMD drives, but track 1 seems to be semi-reserved for directory sectors.*/
  if ( partitionAt( dos, part ).imagetype == D64_TYPE_DNP )
  {
    *track = 2;
    while ( sectorsFree( dos, part, *track ) == 0 )
    {
      ( *track )++;

      if ( *track == getParam( dos, part, ParamIndex::LAST_TRACK ) || *track == 0 )
      {
        /* Wrap to track 1 */
        *track = 1;
      }

      if ( *track == 2 )
        /* If we're at track 2 again there are no free sectors anywhere */
        return 1;
    }
  }
  else
  {
    /* Look for a track with free sectors close to the directory */
    // distance is an int8_t, as in the C: on a full D82 (154 tracks) it wraps
    // at 127 before reaching the last track, and the search never ends, as it
    // never ends on the AVR. Here the drive stops (DriveHung).
    while ( std::cmp_less( distance, getParam( dos, part, ParamIndex::LAST_TRACK ) ) )
    {
      if ( distance == std::numeric_limits<std::int8_t>::min() )
      {
        throw DriveHung{};
      }
      if ( sectorsFree(
               dos, part, static_cast<std::uint8_t>( getParam( dos, part, ParamIndex::DIR_TRACK ) - distance ) ) != 0 )
        break;

      /* Invert sign */
      distance = static_cast<std::int8_t>( -distance );

      /* Increase distance every second try */
      if ( distance > 0 )
        distance++;
    }

    if ( std::cmp_equal( distance, getParam( dos, part, ParamIndex::LAST_TRACK ) ) )
    {
      if ( dos.errors.currentError == ERROR_OK )
        setError( dos, ERROR_DISK_FULL );
      return 1;
    }

    *track = static_cast<std::uint8_t>( getParam( dos, part, ParamIndex::DIR_TRACK ) - distance );
  }

  /* Search for the first free sector on this track */
  for ( *sector = 0; *sector < sectorsPerTrack( dos, part, *track ); *sector += 1 )
    if ( isFree( dos, part, *track, *sector ) > 0 )
      return 0;

  /* If we're here the BAM is invalid or couldn't be read */
  if ( dos.errors.currentError == ERROR_OK )
    setError( dos, ERROR_DISK_FULL );
  return 1;
}

/**
 * get_next_sector - calculate the next sector for a file
 * @part  : partition
 * @track : pointer to a variable holding the track
 * @sector: pointer to a variable holding the sector
 *
 * This function calculates the next sector to be allocated for a file
 * based on the current sector in the variables pointed to by track/sector.
 * The algorithm is based on the description found at
 * http://ist.uwaterloo.ca/~schepers/formats/DISK.TXT
 * Returns 0 if successful or 1 if any error occured.
 */
std::uint8_t getNextSector( Dos& dos, std::uint8_t part, std::uint8_t* track, std::uint8_t* sector )
{
  if ( partitionAt( dos, part ).imagetype == D64_TYPE_DNP )
  {
    std::uint8_t newtrack = *track;

    /* Find a track with free sectors */
    while ( sectorsFree( dos, part, newtrack ) == 0 )
    {
      newtrack++;

      if ( newtrack == getParam( dos, part, ParamIndex::LAST_TRACK ) || newtrack == 0 )
      {
        /* Wrap to track 1 */
        newtrack = 1;
      }

      if ( newtrack == *track )
        /* Returned to the start track -> no free sectors anywhere */
        return 1;
    }

    std::uint8_t newsector = 0;

    if ( newtrack == *track )
    {
      /* Same track: start at the previous sector */
      newsector = *sector;
    }
    else
    {
      /* New track: start at sector 0 */
      newsector = 0;
    }

    while ( isFree( dos, part, newtrack, newsector ) == 0 )
      newsector++; // will automatically wrap from 255->0

    *track = newtrack;
    *sector = newsector;

    return 0;
  }

  std::uint8_t interleave = 0;
  std::uint8_t tries = 0;

  if ( *track == getParam( dos, part, ParamIndex::DIR_TRACK ) )
  {
    if ( sectorsFree( dos, part, getParam( dos, part, ParamIndex::DIR_TRACK ) ) == 0 )
    {
      if ( dos.errors.currentError == ERROR_OK )
        setError( dos, ERROR_DISK_FULL );
      return 1;
    }
    interleave = getParam( dos, part, ParamIndex::DIR_INTERLEAVE );
  }
  else
  {
    interleave = getParam( dos, part, ParamIndex::FILE_INTERLEAVE );
  }

  /* Look for a track with free sectors */
  tries = 0;
  while ( tries < 3 && sectorsFree( dos, part, *track ) == 0 )
  {
    /* No more space on current track, try another */
    if ( *track < getParam( dos, part, ParamIndex::DIR_TRACK ) )
    {
      *track -= 1;
    }
    else
    {
      *track += 1;
      /* Skip track 53 on D71 images */
      if ( ( partitionAt( dos, part ).imagetype & D64_TYPE_MASK ) == D64_TYPE_D71 && *track == D71_BAM2_TRACK )
        *track += 1;
    }

    if ( *track < 1 )
    {
      *track = static_cast<std::uint8_t>( getParam( dos, part, ParamIndex::DIR_TRACK ) + 1 );
      *sector = 0;
      tries++;
    }
    if ( *track > getParam( dos, part, ParamIndex::LAST_TRACK ) )
    {
      *track = static_cast<std::uint8_t>( getParam( dos, part, ParamIndex::DIR_TRACK ) - 1 );
      *sector = 0;
      tries++;
    }
  }

  if ( tries == 3 )
  {
    if ( dos.errors.currentError == ERROR_OK )
      setError( dos, ERROR_DISK_FULL );
    return 1;
  }

  /* Look for a sector at interleave distance */
  *sector += interleave;
  if ( *sector >= sectorsPerTrack( dos, part, *track ) )
  {
    *sector = static_cast<std::uint8_t>( *sector - sectorsPerTrack( dos, part, *track ) );
    if ( *sector != 0 )
      *sector -= 1;
  }

  /* Increase distance until an empty sector is found */
  // tries is a uint8_t: when the 99 run out it wraps to 255, so the test
  // below passes, as it does in the C.
  tries = 99;
  while ( isFree( dos, part, *track, *sector ) <= 0 && tries-- != 0 )
  {
    *sector += 1;
    if ( *sector >= sectorsPerTrack( dos, part, *track ) )
      *sector = 0;
  }

  if ( tries != 0 )
    return 0;

  if ( dos.errors.currentError == ERROR_OK )
    setError( dos, ERROR_DISK_FULL );
  return 1;
}

/**
 * nextdirentry - read the next dir entry to ops_scratch
 * @dh: directory handle
 *
 * This function reads the next directory entry from the disk
 * into ops_scratch. Returns 1 if an error occured, -1 if there
 * are no more directory entries and 0 if successful. This
 * function will return every entry, even deleted ones.
 */
std::int8_t nextdirentry( Dos& dos, Dh* dh )
{
  std::uint8_t* opsScratch = dos.buffers.opsScratch.data();

  /* End of directory entries in this sector? */
  if ( dh->dir.d64.entry == 8 )
  {
    /* Read link pointer */
    if ( checkedRead( dos, dh->part, dh->dir.d64.track, dh->dir.d64.sector, opsScratch, 2, ERROR_ILLEGAL_TS_LINK ) !=
         0 )
      return 1;

    /* Final directory sector? */
    if ( opsScratch[0] == 0 )
      return -1;

    dh->dir.d64.track = opsScratch[0];
    dh->dir.d64.sector = opsScratch[1];
    dh->dir.d64.entry = 0;
  }

  if ( dh->dir.d64.track < 1 || dh->dir.d64.track > getParam( dos, dh->part, ParamIndex::LAST_TRACK ) ||
       dh->dir.d64.sector >= sectorsPerTrack( dos, dh->part, dh->dir.d64.track ) )
  {
    setErrorTs( dos, ERROR_ILLEGAL_TS_LINK, dh->dir.d64.track, dh->dir.d64.sector );
    return 1;
  }

  if ( readEntry( dos, dh->part, &dh->dir.d64, opsScratch ) != 0 )
    return 1;

  dh->dir.d64.entry++;

  return 0;
}

/**
 * find_empty_entry - find an empty directory entry
 * @path: path of the directory
 * @dh: directory handle
 *
 * This funtion finds an empty entry in the directory, creating
 * a new directory sector if required.
 * Returns 0 if successful or 1 if not.
 */
std::uint8_t findEmptyEntry( Dos& dos, Path* path, Dh* dh )
{
  std::uint8_t* opsScratch = dos.buffers.opsScratch.data();
  std::int8_t res = 0;

  d64Opendir( dos, dh, path );
  do
  {
    res = nextdirentry( dos, dh );
    if ( res > 0 )
      return 1;
  } while ( res == 0 && opsScratch[DIR_OFS_FILE_TYPE] != 0 );

  /* Allocate a new directory sector if no empty entries were found */
  if ( res < 0 )
  {
    std::uint8_t const t = dh->dir.d64.track;
    std::uint8_t const s = dh->dir.d64.sector;

    if ( getNextSector( dos, path->part, &dh->dir.d64.track, &dh->dir.d64.sector ) != 0 )
      return 1;

    /* Link the old sector to the new */
    opsScratch[0] = dh->dir.d64.track;
    opsScratch[1] = dh->dir.d64.sector;
    if ( imageWrite( dos, path->part, sectorOffset( dos, path->part, t, s ), opsScratch, 2, 0 ) != 0 )
      return 1;

    if ( allocateSector( dos, path->part, dh->dir.d64.track, dh->dir.d64.sector ) != 0 )
      return 1;

    /* DNP only: Increment the block count in the parent directory */
    if ( partitionAt( dos, path->part ).imagetype == D64_TYPE_DNP )
    {
      if ( imageRead( dos,
                      path->part,
                      DNP_DIRHEADER_PARENTENTRY_TRACK +
                          sectorOffset( dos, path->part, path->dir.dxx.track, path->dir.dxx.sector ),
                      opsScratch,
                      3 ) != 0 )
        return 1;

      if ( opsScratch[0] != 0 )
      {
        /* Read block count of entry in parent directory */
        if ( imageRead( dos,
                        path->part,
                        sectorOffset( dos, path->part, opsScratch[0], opsScratch[1] ) + opsScratch[2] +
                            DIR_OFS_SIZE_LOW - 2,
                        opsScratch + 3,
                        2 ) != 0 )
          return 1;

        // The C increments it in place as the AVR's uint16_t, little-endian.
        auto blocks = static_cast<std::uint16_t>( opsScratch[3] | ( opsScratch[4] << 8U ) );
        blocks++;
        opsScratch[3] = static_cast<std::uint8_t>( blocks & 0xffU );
        opsScratch[4] = static_cast<std::uint8_t>( blocks >> 8U );

        /* Write new block count */
        if ( imageWrite( dos,
                         path->part,
                         sectorOffset( dos, path->part, opsScratch[0], opsScratch[1] ) + opsScratch[2] +
                             DIR_OFS_SIZE_LOW - 2,
                         opsScratch + 3,
                         2,
                         1 ) != 0 )
          return 1;
      }
    }

    /* Clear the new directory sector */
    std::memset( opsScratch, 0, 32 );
    opsScratch[1] = 0xff;
    for ( std::uint8_t i = 0; i < 256 / 32; i++ )
    {
      dh->dir.d64.entry = i;
      if ( writeEntry( dos, path->part, &dh->dir.d64, opsScratch, 0 ) != 0 )
        return 1;

      opsScratch[1] = 0;
    }

    /* Mark full sector as used */
    opsScratch[1] = 0xff;
    dh->dir.d64.entry = 0;
  }
  else
  {
    /* nextdirentry has already incremented this variable, undo it */
    dh->dir.d64.entry--;
  }

  return 0;
}

/**
 * d64_read - refill-callback used for reading
 * @buf: target buffer
 *
 * This is the callback used as refill for files opened for reading.
 */
std::uint8_t d64Read( Dos& dos, Buffer* buf )
{
  /* Store the current sector, used for append */
  buf->pvt.d64.track = buf->data[0];
  buf->pvt.d64.sector = buf->data[1];

  if ( checkedRead( dos, buf->pvt.d64.part, buf->data[0], buf->data[1], buf->data, 256, ERROR_ILLEGAL_TS_LINK ) != 0 )
  {
    freeBuffer( dos, buf );
    return 1;
  }

  buf->position = 2;

  if ( buf->data[0] == 0 )
  {
    /* Final sector of the file */
    buf->lastused = buf->data[1];
    buf->sendeoi = true;
  }
  else
  {
    buf->lastused = 255;
    buf->sendeoi = false;
  }

  return 0;
}

/**
 * d64_seek - seek-callback
 * @buf     : target buffer
 * @position: offset to seek to
 * @index   : offset within the record to seek to
 *
 * This is the function used as the seek callback. Since seeking
 * isn't supported for D64 files it just sets an error message
 * and returns 1.
 */
std::uint8_t d64Seek( Dos& dos, Buffer* /*buf*/, std::uint32_t /*position*/, std::uint8_t /*index*/ )
{
  setError( dos, ERROR_SYNTAX_UNABLE );
  return 1;
}

/**
 * d64_write - refill-callback used for writing
 * @buf: target buffer
 *
 * This is the callback used as refill for files opened for writing.
 */
std::uint8_t d64Write( Dos& dos, Buffer* buf )
{
  std::uint8_t t = 0;
  std::uint8_t s = 0;
  std::uint8_t savederror = 0;

  savederror = 0;
  t = buf->pvt.d64.track;
  s = buf->pvt.d64.sector;

  buf->pvt.d64.blocks++;

  /* Mark as last sector in case something below fails */
  buf->data[0] = 0;
  buf->data[1] = buf->lastused;

  /* Find another free sector */
  if ( getNextSector( dos, buf->pvt.d64.part, &t, &s ) != 0 )
  {
    t = 0;
    savederror = dos.errors.currentError;
    goto storedata;
  }

  buf->data[0] = t;
  buf->data[1] = s;

  /* Allocate it */
  if ( allocateSector( dos, buf->pvt.d64.part, t, s ) != 0 )
  {
    freeBuffer( dos, buf );
    return 1;
  }

storedata:
  /* Store data in the already-reserved sector */
  if ( imageWrite( dos,
                   buf->pvt.d64.part,
                   sectorOffset( dos, buf->pvt.d64.part, buf->pvt.d64.track, buf->pvt.d64.sector ),
                   buf->data,
                   256,
                   1 ) != 0 )
  {
    freeBuffer( dos, buf );
    return 1;
  }

  buf->pvt.d64.track = t;
  buf->pvt.d64.sector = s;
  buf->position = 2;
  buf->lastused = 1;
  buf->mustflush = false;
  markBufferClean( dos, buf );

  if ( savederror != 0 )
  {
    setError( dos, savederror );
    freeBuffer( dos, buf );
    return 1;
  }
  return 0;
}

std::uint8_t d64WriteCleanup( Dos& dos, Buffer* buf )
{
  std::uint8_t* opsScratch = dos.buffers.opsScratch.data();

  buf->data[0] = 0;
  buf->data[1] = buf->lastused;

  std::uint8_t const t = buf->pvt.d64.track;
  std::uint8_t const s = buf->pvt.d64.sector;
  buf->pvt.d64.blocks++;

  /* Track=0 means that there was an error somewhere earlier */
  if ( t == 0 )
    return 1;

  /* Store data */
  if ( imageWrite( dos, buf->pvt.d64.part, sectorOffset( dos, buf->pvt.d64.part, t, s ), buf->data, 256, 1 ) != 0 )
    return 1;

  /* Update directory entry */
  if ( readEntry( dos, buf->pvt.d64.part, &buf->pvt.d64.dh, opsScratch ) != 0 )
    return 1;

  opsScratch[DIR_OFS_FILE_TYPE] |= FLAG_SPLAT;
  opsScratch[DIR_OFS_SIZE_LOW] = static_cast<std::uint8_t>( buf->pvt.d64.blocks & 0xffU );
  opsScratch[DIR_OFS_SIZE_HI] = static_cast<std::uint8_t>( buf->pvt.d64.blocks >> 8U );
  updateTimestamp( opsScratch );

  if ( writeEntry( dos, buf->pvt.d64.part, &buf->pvt.d64.dh, opsScratch, 1 ) != 0 )
    return 1;

  buf->cleanup = callbackDummy;
  freeBuffer( dos, buf );

  return 0;
}

} // namespace

/* ------------------------------------------------------------------------- */
/*  fileops-API                                                              */
/* ------------------------------------------------------------------------- */

std::uint8_t d64Mount( Dos& dos, Path* path, std::uint8_t* name )
{
  std::uint8_t imagetype = 0;
  std::uint8_t const part = path->part;
  std::uint32_t const fsize = partitionAt( dos, part ).imagehandle.fsize;

  switch ( fsize )
  {
  case 174848:
    imagetype = D64_TYPE_D41;
    partitionAt( dos, part ).d64data = D41PARAM;
    break;

  case 175531:
    imagetype = D64_TYPE_D41 | D64_HAS_ERRORINFO;
    partitionAt( dos, part ).d64data = D41PARAM;
    break;

  case 349696:
    imagetype = D64_TYPE_D71;
    partitionAt( dos, part ).d64data = D71PARAM;
    break;

  case 351062:
    imagetype = D64_TYPE_D71 | D64_HAS_ERRORINFO;
    partitionAt( dos, part ).d64data = D71PARAM;
    break;

  case 819200:
    imagetype = D64_TYPE_D81;
    partitionAt( dos, part ).d64data = D81PARAM;
    break;

  case 822400:
    imagetype = D64_TYPE_D81 | D64_HAS_ERRORINFO;
    partitionAt( dos, part ).d64data = D81PARAM;
    break;

  case 533248:
    imagetype = D64_TYPE_D80;
    partitionAt( dos, part ).d64data = D80PARAM;
    break;

  case 535331:
    imagetype = D64_TYPE_D80 | D64_HAS_ERRORINFO;
    partitionAt( dos, part ).d64data = D80PARAM;
    break;

  case 1066496:
    imagetype = D64_TYPE_D82;
    partitionAt( dos, part ).d64data = D82PARAM;
    break;

  case 1070662:
    imagetype = D64_TYPE_D82 | D64_HAS_ERRORINFO;
    partitionAt( dos, part ).d64data = D82PARAM;
    break;

  default:
    if ( ( fsize % ( 256 * 256U ) ) != 0 )
    {
      setError( dos, ERROR_IMAGE_INVALID );
      return 1;
    }

    /* sanity check: ignore 40-track D64 images */
    if ( fsize == 196608 )
    {
      std::uint8_t const* ptr = ustrrchr( name, '.' );

      // A name without a dot has the AVR read the bytes at addresses 2 and
      // 3, its registers; here it is not taken for a D64.
      if ( ptr != nullptr && ptr[2] == '6' && ptr[3] == '4' )
      {
        setError( dos, ERROR_IMAGE_INVALID );
        return 1;
      }
    }

    imagetype = D64_TYPE_DNP;
    partitionAt( dos, part ).d64data = DNPPARAM;
    partitionAt( dos, part ).d64data.lastTrack = static_cast<std::uint8_t>( fsize / ( 256 * 256U ) );
  }

  /* allocate the first BAM buffer if required */
  if ( dos.d64.bamBuffer == nullptr && bamBufferAlloc( dos, &dos.d64.bamBuffer ) != 0 )
    return 1;

  partitionAt( dos, part ).imagetype = imagetype;
  path->dir.dxx.track = getParam( dos, part, ParamIndex::DIR_TRACK );
  path->dir.dxx.sector = getParam( dos, part, ParamIndex::DIR_START_SECTOR );

  dos.d64.bamRefcount++;

  if ( ( imagetype & D64_HAS_ERRORINFO ) != 0 )
    /* Invalidate error cache */
    dos.d64.errorcache.part = 255;

  return 0;
}

namespace
{

std::uint8_t d64Opendir( Dos& dos, Dh* dh, Path* path )
{
  dh->part = path->part;
  dh->dir.d64.track = path->dir.dxx.track;
  dh->dir.d64.sector = path->dir.dxx.sector;
  dh->dir.d64.entry = 0;

  if ( partitionAt( dos, path->part ).imagetype == D64_TYPE_DNP )
  {
    /* Read the real first directory sector from the header sector */
    std::array<std::uint8_t, 2> tmp{};
    if ( imageRead(
             dos, path->part, sectorOffset( dos, path->part, dh->dir.d64.track, dh->dir.d64.sector ), tmp.data(), 2 ) !=
         0 )
      return 1;

    dh->dir.d64.track = tmp[0];
    dh->dir.d64.sector = tmp[1];
  }
  return 0;
}

std::int8_t d64Readdir( Dos& dos, Dh* dh, CbmDirent* dent )
{
  std::uint8_t const* opsScratch = dos.buffers.opsScratch.data();
  std::int8_t res = 0;

  do
  {
    res = nextdirentry( dos, dh );
    if ( res != 0 )
      return res;

    if ( opsScratch[DIR_OFS_FILE_TYPE] != 0 )
      break;
  } while ( true );

  *dent = CbmDirent{};

  dent->opstype = OpsType::DXX;
  dent->typeflags = static_cast<std::uint8_t>( opsScratch[DIR_OFS_FILE_TYPE] ^ FLAG_SPLAT );

  if ( ( dent->typeflags & TYPE_MASK ) > TYPE_DIR )
    /* Change invalid types to DEL */
    /* FIXME: Should exclude DIR on non-DNP */
    dent->typeflags &= static_cast<std::uint8_t>( ~TYPE_MASK );

  dent->pvt.dxx.dh = dh->dir.d64;
  dent->pvt.dxx.dh.entry -= 1; /* undo increment in nextdirentry */
  dent->blocksize = static_cast<std::uint16_t>( opsScratch[DIR_OFS_SIZE_LOW] + ( 256 * opsScratch[DIR_OFS_SIZE_HI] ) );
  dent->remainder = 0xff;
  std::memcpy( dent->name.data(), opsScratch + DIR_OFS_FILE_NAME, CBM_NAME_LENGTH );
  strnsubst( dent->name.data(), 16, 0xa0, 0 );

  /* sanitize values in case they were not stored */
  dent->date.minute = static_cast<std::uint8_t>( opsScratch[DIR_OFS_MINUTE] % 60 );
  dent->date.hour = static_cast<std::uint8_t>( opsScratch[DIR_OFS_HOUR] % 24 );
  dent->date.day = static_cast<std::uint8_t>( ( ( opsScratch[DIR_OFS_DAY] - 1 ) % 31 ) + 1 );
  dent->date.month = static_cast<std::uint8_t>( ( ( opsScratch[DIR_OFS_MONTH] - 1 ) % 12 ) + 1 );
  dent->date.year = static_cast<std::uint8_t>( opsScratch[DIR_OFS_YEAR] % 100 );
  /* adjust year into 1980..2079 range */
  if ( dent->date.year < 80 )
    dent->date.year += 100;

  return 0;
}

/* Reads and converts a string from the dir header sector (BAM for D41/D71) to the buffer */
/* Used by d64_get(disk|dir)label and d64_getid */
std::uint8_t readStringFromDirheader( Dos& dos, Path* path, std::uint8_t* buffer, ParamIndex what, std::uint8_t size )
{
  std::uint8_t sector = 0;

  if ( partitionAt( dos, path->part ).imagetype == D64_TYPE_DNP )
    sector = path->dir.dxx.sector;
  else
    sector = 0;

  if ( imageRead( dos,
                  path->part,
                  sectorOffset( dos, path->part, path->dir.dxx.track, sector ) + getParam( dos, path->part, what ),
                  buffer,
                  size ) != 0 )
    return 1;

  strnsubst( buffer, size, 0xa0, 0x20 );
  return 0;
}

std::uint8_t d64Getdirlabel( Dos& dos, Path* path, std::uint8_t* label )
{
  return readStringFromDirheader( dos, path, label, ParamIndex::LABEL_OFFSET, 16 );
}

std::uint8_t d64Getdisklabel( Dos& dos, std::uint8_t part, std::uint8_t* label )
{
  if ( partitionAt( dos, part ).imagetype == D64_TYPE_DNP )
  {
    /* Read directly from root dir header */
    if ( imageRead( dos, part, 256 + DNP_LABEL_OFFSET, label, 16 ) != 0 )
      return 1;
  }
  else
  {
    /* Use getdirlabel instead */
    Path curpath{};

    curpath.part = part;
    curpath.dir = partitionAt( dos, part ).currentDir;
    if ( d64Getdirlabel( dos, &curpath, label ) != 0 )
      return 1;
  }

  /* Zero-terminate label */
  std::uint8_t* ptr = label + 16;
  *ptr-- = 0;
  while ( ptr != label && *ptr == ' ' )
    *ptr-- = 0;

  return 0;
}

std::uint8_t d64Getid( Dos& dos, Path* path, std::uint8_t* id )
{
  return readStringFromDirheader( dos, path, id, ParamIndex::ID_OFFSET, 5 );
}

std::uint16_t d64Freeblocks( Dos& dos, std::uint8_t part )
{
  std::uint16_t blocks = 0;

  for ( std::uint8_t i = 1; i != 0 && i <= getParam( dos, part, ParamIndex::LAST_TRACK ); i++ )
  {
    /* Skip directory track */
    switch ( partitionAt( dos, part ).imagetype & D64_TYPE_MASK )
    {
    case D64_TYPE_D81:
      if ( i == D81_BAM_TRACK )
        continue;
      break;

    case D64_TYPE_DNP:
      /* DNP reserves a partial track, handled below */
      break;

    case D64_TYPE_D41:
    case D64_TYPE_D71:
    default:
      if ( i == D41_BAM_TRACK || i == D71_BAM2_TRACK )
        continue; // continue the for loop
      break;      // break out of the switch

    case D64_TYPE_D80:
    case D64_TYPE_D82:
      if ( i == D80_DIR_TRACK )
        continue; // continue the for loop
      break;      // break out of the switch
    }

    if ( ( partitionAt( dos, part ).imagetype & D64_TYPE_MASK ) == D64_TYPE_DNP && i == 1 )
    {
      /* DNP: ignore sectors 0-63 on track 1 */
      for ( std::uint16_t j = 64; j < 256; j++ )
      {
        if ( isFree( dos, part, 1, static_cast<std::uint8_t>( j ) ) > 0 )
          blocks++;
      }
    }
    else
    {
      blocks = static_cast<std::uint16_t>( blocks + sectorsFree( dos, part, i ) );
    }
  }

  return blocks;
}

void d64OpenRead( Dos& dos, Path* path, CbmDirent* dent, Buffer* buf )
{
  std::uint8_t* opsScratch = dos.buffers.opsScratch.data();

  /* Read the directory entry of the file */
  if ( readEntry( dos, path->part, &dent->pvt.dxx.dh, opsScratch ) != 0 )
    return;

  buf->data[0] = opsScratch[DIR_OFS_TRACK];
  buf->data[1] = opsScratch[DIR_OFS_SECTOR];

  buf->pvt.d64.part = path->part;

  buf->read = true;
  buf->refill = d64Read;
  buf->seek = d64Seek;
  stickBuffer( buf );

  buf->refill( dos, buf );
}

void d64OpenWrite( Dos& dos, Path* path, CbmDirent* dent, std::uint8_t type, Buffer* buf, std::uint8_t append )
{
  std::uint8_t* opsScratch = dos.buffers.opsScratch.data();
  Dh dh{};
  std::uint8_t* ptr = nullptr;

  /* Check for read-only image file */
  if ( ( partitionAt( dos, path->part ).imagehandle.flag & FA_WRITE ) == 0 )
  {
    setError( dos, ERROR_WRITE_PROTECT );
    return;
  }

  if ( append != 0 )
  {
    /* Append case: Open the file and read the last sector */
    d64OpenRead( dos, path, dent, buf );
    while ( dos.errors.currentError == 0 && buf->data[0] != 0 )
      buf->refill( dos, buf );

    if ( dos.errors.currentError != 0 )
      return;

    /* Modify the buffer for writing */
    buf->pvt.d64.dh = dent->pvt.dxx.dh;
    buf->pvt.d64.blocks =
        static_cast<std::uint16_t>( opsScratch[DIR_OFS_SIZE_LOW] + ( 256 * opsScratch[DIR_OFS_SIZE_HI] ) - 1 );
    buf->read = false;
    buf->position = static_cast<std::uint8_t>( buf->lastused + 1 );
    buf->mustflush = buf->position == 0;
    buf->refill = d64Write;
    buf->cleanup = d64WriteCleanup;
    buf->seek = d64Seek;
    markWriteBuffer( buf );

    updateTimestamp( opsScratch );
    writeEntry( dos, buf->pvt.d64.part, &buf->pvt.d64.dh, opsScratch, 1 );

    return;
  }

  /* Non-append case */

  /* Search for an empty directory entry */
  if ( findEmptyEntry( dos, path, &dh ) != 0 )
    return;

  /* Create directory entry in ops_scratch */
  std::uint8_t const* name = dent->name.data();
  std::memset( opsScratch + 2, 0, dos.buffers.opsScratch.size() - 2 ); /* Don't overwrite the link pointer! */
  std::memset( opsScratch + DIR_OFS_FILE_NAME, 0xa0, CBM_NAME_LENGTH );
  ptr = opsScratch + DIR_OFS_FILE_NAME;
  while ( *name != 0 )
    *ptr++ = *name++;
  opsScratch[DIR_OFS_FILE_TYPE] = type;

  /* Find a free sector and allocate it */
  std::uint8_t t = 0;
  std::uint8_t s = 0;

  if ( getFirstSector( dos, path->part, &t, &s ) != 0 )
    return;

  opsScratch[DIR_OFS_TRACK] = t;
  opsScratch[DIR_OFS_SECTOR] = s;

  if ( allocateSector( dos, path->part, t, s ) != 0 )
    return;

  /* Write the directory entry */
  updateTimestamp( opsScratch );
  if ( writeEntry( dos, path->part, &dh.dir.d64, opsScratch, 1 ) != 0 )
    return;

  /* Prepare the data buffer */
  markWriteBuffer( buf );
  buf->position = 2;
  buf->lastused = 2;
  buf->cleanup = d64WriteCleanup;
  buf->refill = d64Write;
  buf->seek = d64Seek;
  buf->data[2] = 13; /* Verified on VICE */
  buf->pvt.d64.dh = dh.dir.d64;
  buf->pvt.d64.part = path->part;
  buf->pvt.d64.track = t;
  buf->pvt.d64.sector = s;
}

void d64OpenRel(
    Dos& dos, Path* /*path*/, CbmDirent* /*dent*/, Buffer* /*buf*/, std::uint8_t /*length*/, std::uint8_t /*mode*/ )
{
  setError( dos, ERROR_SYNTAX_UNABLE );
}

std::uint8_t d64Delete( Dos& dos, Path* path, CbmDirent* dent )
{
  std::uint8_t* opsScratch = dos.buffers.opsScratch.data();
  std::array<std::uint8_t, 2> linkbuf{};

  /* Read the directory entry of the file */
  if ( readEntry( dos, path->part, &dent->pvt.dxx.dh, opsScratch ) != 0 )
    return 255;

  /* Free the sector chain in the BAM */
  linkbuf[0] = opsScratch[DIR_OFS_TRACK];
  linkbuf[1] = opsScratch[DIR_OFS_SECTOR];

  do
  {
    freeSector( dos, path->part, linkbuf[0], linkbuf[1] );

    if ( checkedRead( dos, path->part, linkbuf[0], linkbuf[1], linkbuf.data(), 2, ERROR_ILLEGAL_TS_LINK ) != 0 )
      return 255;
  } while ( linkbuf[0] != 0 );

  /* Clear directory entry */
  opsScratch[DIR_OFS_FILE_TYPE] = 0;
  if ( writeEntry( dos, path->part, &dent->pvt.dxx.dh, opsScratch, 1 ) != 0 )
    return 255;

  return 1;
}

void d64ReadSector( Dos& dos, Buffer* buf, std::uint8_t part, std::uint8_t track, std::uint8_t sector )
{
  checkedRead( dos, part, track, sector, buf->data, 256, ERROR_ILLEGAL_TS_COMMAND );
}

void d64WriteSector( Dos& dos, Buffer* buf, std::uint8_t part, std::uint8_t track, std::uint8_t sector )
{
  if ( track < 1 || track > getParam( dos, part, ParamIndex::LAST_TRACK ) ||
       sector >= sectorsPerTrack( dos, part, track ) )
  {
    setErrorTs( dos, ERROR_ILLEGAL_TS_COMMAND, track, sector );
  }
  else
  {
    imageWrite( dos, part, sectorOffset( dos, part, track, sector ), buf->data, 256, 1 );
  }
}

void d64Rename( Dos& dos, Path* path, CbmDirent* dent, std::uint8_t* newname )
{
  std::uint8_t* opsScratch = dos.buffers.opsScratch.data();
  std::uint8_t* ptr = nullptr;

  /* Read the directory entry of the file */
  if ( readEntry( dos, path->part, &dent->pvt.dxx.dh, opsScratch ) != 0 )
    return;

  std::memset( opsScratch + DIR_OFS_FILE_NAME, 0xa0, CBM_NAME_LENGTH );
  ptr = opsScratch + DIR_OFS_FILE_NAME;
  while ( *newname != 0 )
    *ptr++ = *newname++;

  writeEntry( dos, path->part, &dent->pvt.dxx.dh, opsScratch, 1 );
}

} // namespace

/**
 * d64_raw_directory - open directory track as file
 * @buf: target buffer
 *
 * This function opens the directory track as a file on the
 * buffer passed in buf. Used when $ is opened on a secondary
 * address > 0.
 */
void d64RawDirectory( Dos& dos, Path* path, Buffer* buf )
{
  /* Copy&Waste from d64_open_read */
  buf->data[0] = path->dir.dxx.track;
  if ( partitionAt( dos, path->part ).imagetype == D64_TYPE_DNP )
    buf->data[1] = path->dir.dxx.sector;
  else
    buf->data[1] = 0;

  buf->pvt.d64.part = path->part;

  buf->read = true;
  buf->refill = d64Read;
  buf->seek = d64Seek;
  stickBuffer( buf );

  buf->refill( dos, buf );
}

namespace
{

/**
 * d64_chdir - chdir for Dxx files
 * @path   : path object of the location of dirname
 * @dirname: directory to be changed into
 *
 * Changes the directory in the path object for DNP files,
 * returns an error for everything else.
 * Returns 0 if successful, 1 otherwise.
 */
std::uint8_t d64Chdir( Dos& dos, Path* path, CbmDirent* dirname )
{
  std::uint8_t* opsScratch = dos.buffers.opsScratch.data();

  if ( partitionAt( dos, path->part ).imagetype != D64_TYPE_DNP )
    return imageChdir( dos, path, dirname );

  if ( dirname->name[0] == 0 )
  {
    /* Empty string: root directory */
    path->dir.dxx.track = 1;
    path->dir.dxx.sector = 1;
    return 0;
  }

  if ( dirname->name[0] == '_' && dirname->name[1] == 0 )
  {
    /* Move up a directory, unmount if at the root */
    std::array<std::uint8_t, 2> parent{};

    if ( imageRead( dos,
                    path->part,
                    sectorOffset( dos, path->part, path->dir.dxx.track, path->dir.dxx.sector ) +
                        DNP_DIRHEADER_PARENTHDR_TRACK,
                    parent.data(),
                    2 ) != 0 )
      return 1;

    if ( parent[0] == 0 )
      /* Already at the root directory */
      return imageUnmount( dos, path->part );

    path->dir.dxx.track = parent[0];
    path->dir.dxx.sector = parent[1];
    return 0;
  }

  if ( readEntry( dos, path->part, &dirname->pvt.dxx.dh, opsScratch ) != 0 )
    return 1;

  path->dir.dxx.track = opsScratch[DIR_OFS_TRACK];
  path->dir.dxx.sector = opsScratch[DIR_OFS_SECTOR];

  return 0;
}

/**
 * d64_mkdir - mkdir for Dxx files
 * @path   : path object of the location of dirname
 * @dirname: directory to be created
 *
 * Creates a subdirectory for DNP files, returns an error
 * for everything else.
 */
void d64Mkdir( Dos& dos, Path* path, std::uint8_t* dirname )
{
  std::uint8_t* opsScratch = dos.buffers.opsScratch.data();
  Dh dh{};
  Buffer* buf = nullptr;
  std::uint8_t hTrack = 0;
  std::uint8_t hSector = 0;
  std::uint8_t dTrack = 0;
  std::uint8_t dSector = 0;
  std::uint8_t* ptr = nullptr;
  std::uint8_t const* name = nullptr;

  if ( partitionAt( dos, path->part ).imagetype != D64_TYPE_DNP )
  {
    setError( dos, ERROR_SYNTAX_UNABLE );
    return;
  }

  buf = allocBuffer( dos );
  if ( buf == nullptr )
    return;

  /* Find an empty directory entry */
  if ( findEmptyEntry( dos, path, &dh ) != 0 )
    return;

  /* Find a free track/sector for the dir header */
  hTrack = path->dir.dxx.track;
  hSector = path->dir.dxx.sector;

  if ( getNextSector( dos, path->part, &hTrack, &hSector ) != 0 )
  {
    setError( dos, ERROR_DISK_FULL );
    return;
  }

  /* Allocate it now so the next call to get_next doesn't return it again */
  if ( allocateSector( dos, path->part, hTrack, hSector ) != 0 )
    return;

  /* Find a free track/sector for the first directory sector */
  dTrack = hTrack;
  dSector = hSector;

  if ( getNextSector( dos, path->part, &dTrack, &dSector ) != 0 )
  {
    setError( dos, ERROR_DISK_FULL );
    freeSector( dos, path->part, hTrack, hSector );
    return;
  }

  if ( allocateSector( dos, path->part, dTrack, dSector ) != 0 )
    return;

  /* Build a directory header */
  std::memset( buf->data, 0, 256 );
  std::memset( buf->data + DNP_LABEL_OFFSET, 0xa0, DNP_LABEL_AREA_SIZE );

  /* Unfortunately this generates much smaller code on AVR */
  ptr = buf->data;
  *ptr++ = dTrack;
  *ptr++ = dSector;
  *ptr++ = 'H';

  /* Note: The CMD FD manual says these bytes point to the root header, */
  /*       but all observed DNP files actually store the t/s of the     */
  /*       sector itself here.                                          */
  ptr = buf->data + DNP_DIRHEADER_ROOTHDR_TRACK;
  *ptr++ = hTrack;
  *ptr++ = hSector;

  *ptr++ = path->dir.dxx.track;
  *ptr++ = path->dir.dxx.sector;

  *ptr++ = dh.dir.d64.track;
  *ptr++ = dh.dir.d64.sector;
  *ptr++ = static_cast<std::uint8_t>( ( dh.dir.d64.entry * 32 ) + 2 );

  /* Fill name and ID */
  ptr = buf->data + DNP_LABEL_OFFSET;
  name = dirname;
  while ( *name != 0 )
    *ptr++ = *name++;

  ptr = buf->data + DNP_ID_OFFSET;
  *ptr++ = dirname[0];
  *ptr++ = dirname[1];
  ptr++;
  *ptr++ = '1';
  *ptr++ = 'H';

  /* Write directory header sector */
  if ( imageWrite( dos, path->part, sectorOffset( dos, path->part, hTrack, hSector ), buf->data, 256, 0 ) != 0 )
    return;

  /* Create empty directory sector */
  std::memset( buf->data, 0, 256 );
  buf->data[1] = 0xff;
  if ( imageWrite( dos, path->part, sectorOffset( dos, path->part, dTrack, dSector ), buf->data, 256, 0 ) != 0 )
    return;

  /* Create the directory entry */
  /* FIXME: Isn't similiar code duplicated in open_write and rename? */
  std::memset( opsScratch + 2, 0, dos.buffers.opsScratch.size() - 2 );
  std::memset( opsScratch + DIR_OFS_FILE_NAME, 0xa0, CBM_NAME_LENGTH );

  ptr = opsScratch + DIR_OFS_FILE_NAME;
  while ( *dirname != 0 )
    *ptr++ = *dirname++;

  opsScratch[DIR_OFS_FILE_TYPE] = TYPE_DIR | FLAG_SPLAT;
  opsScratch[DIR_OFS_TRACK] = hTrack;
  opsScratch[DIR_OFS_SECTOR] = hSector;
  opsScratch[DIR_OFS_SIZE_LOW] = 2;
  updateTimestamp( opsScratch );

  imageWrite( dos,
              path->part,
              sectorOffset( dos, path->part, dh.dir.d64.track, dh.dir.d64.sector ) + ( dh.dir.d64.entry * 32U ) + 2,
              opsScratch + 2,
              30,
              1 );
}

} // namespace

/**
 * d64_invalidate - invalidate internal state
 *
 * This function invalidates all cached state when
 * a card change is detected.
 */
void d64Invalidate( Dos& dos )
{
  freeBuffer( dos, dos.d64.bamBuffer );
  dos.d64.bamBuffer = nullptr;
  freeBuffer( dos, dos.d64.bamBuffer2 );
  dos.d64.bamBuffer2 = nullptr;
  dos.d64.bamRefcount = 0;
}

/**
 * d64_unmount - unmount disk image
 * @part: partition number
 *
 * This function in called on image unmount and handles
 * refcounting for the BAM buffers.
 */
void d64Unmount( Dos& dos, std::uint8_t part )
{
  /* invalidate BAM buffers that point to the current partition */
  if ( dos.d64.bamBuffer != nullptr )
  {
    dos.d64.bamBuffer->cleanup( dos, dos.d64.bamBuffer );
    if ( dos.d64.bamBuffer->pvt.bam.part == part )
      dos.d64.bamBuffer->pvt.bam.part = 255;
  }

  if ( dos.d64.bamBuffer2 != nullptr )
  {
    dos.d64.bamBuffer2->cleanup( dos, dos.d64.bamBuffer2 );
    if ( dos.d64.bamBuffer2->pvt.bam.part == part )
      dos.d64.bamBuffer2->pvt.bam.part = 255;
  }

  /* decrease BAM buffer refcounter - it can never be zero while a Dxx is mounted*/
  // As in the C, the buffers are freed while the count is not zero, and
  // kept when it reaches zero.
  if ( --dos.d64.bamRefcount != 0 )
  {
    freeBuffer( dos, dos.d64.bamBuffer );
    freeBuffer( dos, dos.d64.bamBuffer2 );
    dos.d64.bamBuffer = nullptr;
    dos.d64.bamBuffer2 = nullptr;
  }
}

namespace
{

/* ------------------------------------------------------------------------- */
/*  Formatting disk images                                                   */
/* ------------------------------------------------------------------------- */

/* create a 1581/DNP BAM signature */
void formatAddBamSignature( Dos& dos, std::uint8_t doschar, std::uint8_t const* idbuf )
{
  std::uint8_t* ptr = dos.d64.bamBuffer->data + 2;

  *ptr++ = doschar;
  *ptr++ = static_cast<std::uint8_t>( doschar ^ 0xffU );
  *ptr++ = idbuf[0];
  *ptr++ = idbuf[1];
  *ptr++ = 0xc0;
}

/* copy name and id into some buffer */
void formatCopyLabel(
    Dos& dos, std::uint8_t part, std::uint8_t* data, std::uint8_t const* name, std::uint8_t const* idbuf )
{
  std::uint8_t* ptr = nullptr;
  std::uint8_t count = 0;

  ptr = data + getParam( dos, part, ParamIndex::LABEL_OFFSET );
  std::memset( ptr, 0xa0, 25 ); /* 25=name+id+dos+spaces+padding */

  count = 16;
  while ( *name != 0 && count-- != 0 )
    *ptr++ = *name++;

  std::memcpy( data + getParam( dos, part, ParamIndex::ID_OFFSET ), idbuf, 5 );
}

void formatD41Image( Dos& dos, std::uint8_t part, Buffer* buf, std::uint8_t* name, std::uint8_t* idbuf )
{
  /* allocate BAM and first directory sector */
  for ( std::uint8_t s = 0; s < 2; s++ )
    allocateSector( dos, part, D41_BAM_TRACK, s );

  /* 18/0 is now available via bam_buffer */
  std::uint8_t* ptr = dos.d64.bamBuffer->data;
  *ptr++ = 18;
  *ptr++ = 1;
  *ptr++ = 0x41;

  /* copy disk label and ID */
  idbuf[3] = '2';
  idbuf[4] = 'A';
  formatCopyLabel( dos, part, dos.d64.bamBuffer->data, name, idbuf );
  /* two additional 0xa0 characters on 5.25" disks */
  dos.d64.bamBuffer->data[0xa9] = 0xa0;
  dos.d64.bamBuffer->data[0xaa] = 0xa0;

  clearDirSector( dos, part, D41_BAM_TRACK, 1, buf->data );
}

void formatD71Image( Dos& dos, std::uint8_t part, Buffer* buf, std::uint8_t* name, std::uint8_t* idbuf )
{
  /* almost everything is the same as D41 */
  formatD41Image( dos, part, buf, name, idbuf );

  /* add double-sided marker in 18/0 (still in bam_buffer) */
  dos.d64.bamBuffer->data[3] = 0x80;

  /* allocate all of track 53 */
  for ( std::uint8_t s = 0; s < 19; s++ )
    allocateSector( dos, part, D71_BAM2_TRACK, s );
}

void formatD81Image( Dos& dos, std::uint8_t part, Buffer* buf, std::uint8_t* name, std::uint8_t* idbuf )
{
  /* allocate 40/0 to 40/3 */
  for ( std::uint8_t s = 0; s < 4; s++ )
    allocateSector( dos, part, D81_BAM_TRACK, s );

  /* bam_buffer buffer now holds 40/1 */
  dos.d64.bamBuffer->data[0] = 40;
  dos.d64.bamBuffer->data[1] = 2;
  formatAddBamSignature( dos, 'D', idbuf );
  // mustflush was already set by allocate_sector

  /* switch bam_buffer to 40/2 */
  static_cast<void>( sectorsFree( dos, part, 41 ) );
  dos.d64.bamBuffer->data[0] = 0;
  dos.d64.bamBuffer->data[1] = 0xff;
  formatAddBamSignature( dos, 'D', idbuf );
  dos.d64.bamBuffer->mustflush = true;

  /* build contents of 40/0 */
  std::uint8_t* ptr = buf->data;
  *ptr++ = 40;
  *ptr++ = 3;
  *ptr++ = 'D';

  /* copy disk label and ID to buffer */
  idbuf[3] = '3';
  idbuf[4] = 'D';
  formatCopyLabel( dos, part, buf->data, name, idbuf );

  /* write 40/0 */
  if ( imageWrite( dos, part, /*sector_offset(part, 40, 0)*/ ( 40 - 1 ) * 40 * 256U, buf->data, 256, 0 ) != 0 )
    return;

  clearDirSector( dos, part, D81_BAM_TRACK, 3, buf->data );
}

void formatDnpImage( Dos& dos, std::uint8_t part, Buffer* buf, std::uint8_t* name, std::uint8_t* idbuf )
{
  /* Note: Formatting is only accepted while in the root dir, so the
           d64param struct doesn't need to be changed */
  for ( std::uint8_t s = 0; s < 35; s++ )
    allocateSector( dos, part, DNP_BAM_TRACK, s );

  /* add BAM signature - first BAM sector is in bam_buffer because of allocate_sector */
  formatAddBamSignature( dos, 'H', idbuf );
  dos.d64.bamBuffer->data[DNP_BAM_LAST_TRACK_OFS] = getParam( dos, part, ParamIndex::LAST_TRACK );

  /* build root dirheader */
  std::uint8_t* ptr = buf->data;
  std::memset( ptr, 0, 256 );
  *ptr++ = 1;
  *ptr++ = 34;
  *ptr++ = 'H';
  idbuf[3] = '1';
  idbuf[4] = 'H';
  formatCopyLabel( dos, part, buf->data, name, idbuf );
  buf->data[DNP_DIRHEADER_ROOTHDR_TRACK] = 1;
  buf->data[DNP_DIRHEADER_ROOTHDR_SECTOR] = 1;

  /* write 1/1 */
  if ( imageWrite( dos, part, /*sector_offset(part, 1, 1)*/ 256, buf->data, 256, 0 ) != 0 )
    return;

  clearDirSector( dos, part, 1, DNP_ROOTDIR_SECTOR, buf->data );
}

void formatD80Image(
    Dos& /*dos*/, std::uint8_t /*part*/, Buffer* /*buf*/, std::uint8_t* /*name*/, std::uint8_t* /*idbuf*/ )
{
  // TODO: implement D80 format
}

void formatD82Image(
    Dos& /*dos*/, std::uint8_t /*part*/, Buffer* /*buf*/, std::uint8_t* /*name*/, std::uint8_t* /*idbuf*/ )
{
}

// TODO: implement D82 format

// NOLINTNEXTLINE(readability-non-const-parameter): its type is FileOps::format's
void d64Format( Dos& dos, std::uint8_t part, std::uint8_t* name, std::uint8_t* id )
{
  Buffer* buf = nullptr;
  std::array<std::uint8_t, 5> idbuf{};
  std::uint16_t t = 0;
  std::uint16_t s = 0;

  /* allow format on DNP only when in the root directory */
  if ( partitionAt( dos, part ).imagetype == D64_TYPE_DNP &&
       ( partitionAt( dos, part ).currentDir.dxx.track != 1 || partitionAt( dos, part ).currentDir.dxx.sector != 1 ) )
  {
    /* just ignore, CMD HD doesn't return an error either */
    return;
  }

  /* grab a buffer as work area */
  buf = allocBuffer( dos );
  if ( buf == nullptr )
    return;

  markWriteBuffer( buf );
  unstickBuffer( buf );
  markBufferDirty( dos, buf );
  std::memset( buf->data, 0, 256 );

  /* Flush BAM buffers and mark their contents as invalid */
  d64BamCommit( dos );
  dos.d64.bamBuffer->pvt.bam.part = 0xff;
  if ( dos.d64.bamBuffer2 != nullptr )
    dos.d64.bamBuffer2->pvt.bam.part = 0xff;

  if ( id != nullptr )
  {
    /* Clear the data area of the disk image */
    for ( t = 1; t <= getParam( dos, part, ParamIndex::LAST_TRACK ); t++ )
    {
      for ( s = 0; s < sectorsPerTrack( dos, part, static_cast<std::uint8_t>( t ) ); s++ )
      {
        if ( imageWrite( dos,
                         part,
                         sectorOffset( dos, part, static_cast<std::uint8_t>( t ), static_cast<std::uint8_t>( s ) ),
                         buf->data,
                         256,
                         0 ) != 0 )
          return;
      }
    }

    /* Copy the new ID into the buffer */
    idbuf[0] = id[0];
    idbuf[1] = id[1];
  }
  else
  {
    /* Read the old ID into the buffer */
    Path path{};
    path.part = part;
    path.dir.dxx.track = getParam( dos, part, ParamIndex::DIR_TRACK );
    path.dir.dxx.sector = 1; // only relevant for DNP
    if ( d64Getid( dos, &path, idbuf.data() ) != 0 )
      return;

    /* clear the entire directory track */
    /* This is not accurate, but I do not care. */
    t = getParam( dos, part, ParamIndex::DIR_TRACK );
    for ( s = 0; s < sectorsPerTrack( dos, part, static_cast<std::uint8_t>( t ) ); s++ )
    {
      if ( imageWrite( dos,
                       part,
                       sectorOffset( dos, part, static_cast<std::uint8_t>( t ), static_cast<std::uint8_t>( s ) ),
                       buf->data,
                       256,
                       0 ) != 0 )
        return;
    }
  }
  idbuf[2] = 0xa0;

  /* Mark all sectors as free */
  for ( t = 1; t <= getParam( dos, part, ParamIndex::LAST_TRACK ); t++ )
  {
    for ( s = 0; s < sectorsPerTrack( dos, part, static_cast<std::uint8_t>( t ) ); s++ )
      freeSector( dos, part, static_cast<std::uint8_t>( t ), static_cast<std::uint8_t>( s ) );
  }

  /* call imagetype-specific format function */
  partitionAt( dos, part ).d64data.formatFunction( dos, part, buf, name, idbuf.data() );

  /* FIXME: Clear the error info block */
}

} // namespace

/* ------------------------------------------------------------------------- */
/*  ops struct                                                               */
/* ------------------------------------------------------------------------- */

FileOps const D64OPS = { .openRead = d64OpenRead,
                         .openWrite = d64OpenWrite,
                         .openRel = d64OpenRel,
                         .fileDelete = d64Delete,
                         .diskLabel = d64Getdisklabel,
                         .dirLabel = d64Getdirlabel,
                         .diskId = d64Getid,
                         .diskFree = d64Freeblocks,
                         .readSector = d64ReadSector,
                         .writeSector = d64WriteSector,
                         .format = d64Format,
                         .opendir = d64Opendir,
                         .readdir = d64Readdir,
                         .mkdir = d64Mkdir,
                         .chdir = d64Chdir,
                         .rename = d64Rename };

} // namespace pet::sd2pet
