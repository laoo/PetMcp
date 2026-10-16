// Copyright (C) 2007-2018 Ingo Korb <ingo@akana.de>, as NODISKEMU (a fork of sd2iec)
// carries it. GPL-2.0. Ported to C++ for PetMcp, 2026 (THIRD_PARTY.md).

// doscmd.c (NODISKEMU at commit e51a6df): the command channel parser, as
// configs/config-petSD+ builds it.
//
// Left out, each as if not configured:
// - the fast loaders: their tables, their detection by the CRC of what M-W
//   uploads, the partial capture of uploaded data, CONFIG_CAPTURE_LOADERS, and
//   their handlers. With no handler to find, M-E and the U commands that start
//   a program in a buffer answer 98 with the CRC of the M-W data, as the
//   firmware does for a loader it does not know.
// - the real-time clock: the drive behaves as a petSD+ whose RTC chip was not
//   found, so every T command answers 30 and T-R/T-W's bodies are left out.
// - the EEPROM configuration: XW stores nothing, and XR and XM change only the
//   setting in memory.
// - the disk swap lists: XS parses its path and does nothing more, as the
//   firmware does (its set_changelist call is commented out there).
// - the SD card's raw sectors: the D commands find their buffer as the
//   firmware does, and then answer as if the card were not ready.
// - the system reset (the hard reset U command, XM): dosInit stands for it.
// - the UART debug output, the display, the LEDs and the menu.
// - CONFIG_STACK_TRACKING, CONFIG_PARALLEL_DOLPHIN, the drive multiplexer
//   (NEED_DISKMUX: the petSD+ has an SD card only).

#include "sd2pet/Dos.hpp"
#include "sd2pet/Utils.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace pet::sd2pet
{

namespace
{

constexpr std::uint8_t CURSOR_RIGHT = 0x1d;

/* ---- Minimal drive rom emulation ---- */

struct MagicValue
{
  std::uint16_t address;
  std::array<std::uint8_t, 2> val;
};

/* These are address/value pairs used by some programs to detect a 1541. */
/* Currently we remember two bytes per address since that's the longest  */
/* block required. */
constexpr std::array<MagicValue, 4> C1541_MAGICS{ {
    { .address = 0xfea0, .val = { 0x0d, 0xed } }, /* used by DreamLoad and ULoad Model 3 */
    { .address = 0xe5c6, .val = { 0x34, 0xb1 } }, /* used by DreamLoad and ULoad Model 3 */
    { .address = 0xfffe, .val = { 0x00, 0x00 } }, /* Disable AR6 fastloader */
    { .address = 0, .val = { 0, 0 } }             /* end mark */
} };

/* System partition G-P answer */
constexpr std::array<std::uint8_t, 31> SYSTEM_PARTITION_INFO{ 0xff, 0xe2, 0x00, 0x53, 0x59, 0x53, 0x54, 0x45,
                                                              0x4d, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0, 0xa0,
                                                              0xa0, 0xa0, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00,
                                                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d };

/// diskio.h's DRESULT, for the D commands.
enum class DResult : std::uint8_t
{
  RES_OK = 0, /* 0: Successful */
  RES_ERROR,  /* 1: R/W Error */
  RES_WRPRT,  /* 2: Write Protected */
  RES_NOTRDY, /* 3: Not Ready */
  RES_PARERR  /* 4: Invalid Parameter */
};

// The card's raw sectors are left out: the card answers as not ready.
DResult diskGetinfo( std::uint8_t /*drv*/, std::uint8_t /*page*/, void* /*buffer*/ )
{
  return DResult::RES_NOTRDY;
}

DResult diskRead( std::uint8_t /*drv*/, std::uint8_t* /*buffer*/, std::uint32_t /*sector*/, std::uint8_t /*count*/ )
{
  return DResult::RES_NOTRDY;
}

DResult
diskWrite( std::uint8_t /*drv*/, std::uint8_t const* /*buffer*/, std::uint32_t /*sector*/, std::uint8_t /*count*/ )
{
  return DResult::RES_NOTRDY;
}

/// avr-libc's _crc16_update, which crc.h's crc16_update is on the AVR.
std::uint16_t crc16Update( std::uint16_t crc, std::uint8_t a )
{
  crc ^= a;
  for ( int i = 0; i < 8; ++i )
  {
    if ( ( crc & 1U ) != 0 )
      crc = static_cast<std::uint16_t>( ( crc >> 1U ) ^ 0xA001U );
    else
      crc = static_cast<std::uint16_t>( crc >> 1U );
  }
  return crc;
}

/* ------------------------------------------------------------------------- */
/*  Drive code exec helpers                                                  */
/* ------------------------------------------------------------------------- */

// With the fast loaders left out, no loader is ever detected and the handler
// table holds its end marker only: every code execution is unknown drive code.
void runLoader( Dos& dos, [[maybe_unused]] std::uint16_t address )
{
  setErrorTs( dos,
              ERROR_UNKNOWN_DRIVECODE,
              static_cast<std::uint8_t>( dos.commands.datacrc >> 8U ),
              static_cast<std::uint8_t>( dos.commands.datacrc & 0xffU ) );

  dos.commands.datacrc = 0xffff;
}

/* ------------------------------------------------------------------------- */
/*  Parsing helpers                                                          */
/* ------------------------------------------------------------------------- */

/* Fill the end of the command buffer with 0x00  */
/* so C string functions can work on file names. */

void cleanCmdbuffer( Dos& dos )
{
  auto& commandBuffer = dos.commands.commandBuffer;
  std::memset(
      commandBuffer.data() + dos.commands.commandLength, 0, commandBuffer.size() - dos.commands.commandLength );
}

/* Parse parameters of block commands in the command buffer */
/* Returns number of parameters (up to 4) or <0 on error    */
std::int8_t parseBlockparam( Dos& dos, std::array<std::uint8_t, 4>& values )
{
  std::uint8_t* const commandBuffer = dos.commands.commandBuffer.data();
  std::uint8_t paramcount = 0;
  std::uint8_t* str = nullptr;

  str = ustrchr( commandBuffer, ':' );
  if ( str == nullptr )
  {
    if ( ustrlen( commandBuffer ) < 3 )
      return -1;
    str = commandBuffer + 2;
  }

  str++;

  while ( *str != 0 && paramcount < 4 )
  {
    /* Skip all spaces, cursor-rights and commas - CC7C */
    while ( *str == ' ' || *str == CURSOR_RIGHT || *str == ',' )
      str++;
    if ( *str == 0 )
      break;

    values.at( paramcount++ ) = static_cast<std::uint8_t>( parseNumber( dos, &str ) );
  }

  return static_cast<std::int8_t>( paramcount );
}

std::uint8_t parseBool( Dos& dos )
{
  switch ( dos.commands.commandBuffer[2] )
  {
  case '+':
    return 1;

  case '-':
    return 0;

  default:
    setError( dos, ERROR_SYNTAX_UNKNOWN );
    return 255;
  }
}

/* ------------------------------------------------------------------------- */
/*  Command handlers                                                         */
/* ------------------------------------------------------------------------- */

/* ------------------- */
/*  CD/MD/RD commands  */
/* ------------------- */

/* --- MD --- */
void parseMkdir( Dos& dos )
{
  std::uint8_t* const commandBuffer = dos.commands.commandBuffer.data();
  Path path{};
  std::uint8_t* name = nullptr;

  /* MD requires a colon */
  if ( ustrchr( commandBuffer, ':' ) == nullptr )
  {
    setError( dos, ERROR_SYNTAX_NONAME );
    return;
  }
  if ( parsePath( dos, commandBuffer + 2, &path, &name, 0 ) != 0 )
    return;
  mkdir( dos, &path, name );
}

} // namespace

/* --- CD --- */

/* internal version of CD that is used by CD and diskchange.c */
void doChdir( Dos& dos, std::uint8_t* parsestr )
{
  Path path{};
  std::uint8_t* name = nullptr;
  CbmDirent dent{};

  if ( parsePath( dos, parsestr, &path, &name, 1 ) != 0 )
    return;

  /* clear '*' file */
  dos.files.previousFileDirent.name[0] = 0;

  if ( ustrlen( name ) != 0 )
  {
    /* Path component after the : */
    if ( name[0] == '_' )
    {
      /* Going up a level */
      ustrcpy( dent.name.data(), name );
      if ( chdir( dos, &path, &dent ) != 0 )
        return;
    }
    else
    {
      /* A directory name - try to match it */
      if ( firstMatch( dos, &path, name, FLAG_HIDDEN, &dent ) != 0 )
        return;

      if ( chdir( dos, &path, &dent ) != 0 )
        return;
    }
  }
  else
  {
    /* reject if there is no / in the string */
    if ( ustrchr( parsestr, '/' ) == nullptr )
    {
      setError( dos, ERROR_FILE_NOT_FOUND_39 );
      return;
    }
  }

  updateCurrentDir( dos, &path );
}

namespace
{

void parseChdir( Dos& dos )
{
  doChdir( dos, dos.commands.commandBuffer.data() + 2 );

  /* FIXME
  if (globalflags & AUTOSWAP_ACTIVE)
    set_changelist(NULL, NULLSTRING);
  */
}

/* --- RD --- */
void parseRmdir( Dos& dos )
{
  std::uint8_t* const commandBuffer = dos.commands.commandBuffer.data();
  std::uint8_t* str = nullptr;
  std::uint8_t res = 0;
  std::uint8_t part = 0;
  Path path{};
  CbmDirent dent{};
  CbmDirent chkdent{};
  Dh dh{};

  /* No deletion across subdirectories */
  if ( ustrchr( commandBuffer, '/' ) != nullptr )
  {
    setError( dos, ERROR_SYNTAX_NONAME );
    return;
  }

  /* Parse partition number */
  str = commandBuffer + 2;
  part = parsePartition( dos, &str );
  if ( *str != ':' )
  {
    setError( dos, ERROR_SYNTAX_NONAME );
  }
  else
  {
    // NODISKEMU indexes its partition table with the number unchecked; a
    // partition that does not exist answers as parse_path answers for one.
    if ( part >= dos.parser.maxPart )
    {
      setError( dos, ERROR_DRIVE_NOT_READY );
      return;
    }

    path.part = part;
    path.dir = dos.parser.partition[part].currentDir;

    if ( firstMatch( dos, &path, str + 1, TYPE_DIR, &dent ) != 0 )
      return;

    /* Check if there is anything in that directory */
    if ( chdir( dos, &path, &dent ) != 0 )
      return;

    if ( opendir( dos, &dh, &path ) != 0 )
      return;

    if ( readdir( dos, &dh, &chkdent ) != -1 )
    {
      if ( dos.errors.currentError == 0 )
        setError( dos, ERROR_FILE_EXISTS );
      return;
    }

    path.dir = dos.parser.partition[part].currentDir;

    res = fileDelete( dos, &path, &dent );
    if ( res != 255 )
      setErrorTs( dos, ERROR_SCRATCHED, res, 0 );
  }
}

/* --- CD/MD/RD subparser --- */
void parseDircommand( Dos& dos )
{
  cleanCmdbuffer( dos );

  switch ( dos.commands.commandBuffer[0] )
  {
  case 'M':
    parseMkdir( dos );
    break;

  case 'C':
    parseChdir( dos );
    break;

  case 'R':
    parseRmdir( dos );
    break;

  default:
    setError( dos, ERROR_SYNTAX_UNKNOWN );
    break;
  }
}

/* ------------ */
/*  B commands  */
/* ------------ */
void parseBlock( Dos& dos )
{
  std::uint8_t* const commandBuffer = dos.commands.commandBuffer.data();
  std::uint8_t* str = nullptr;
  Buffer* buf = nullptr;
  std::array<std::uint8_t, 4> params{};
  std::int8_t pcount = 0;

  cleanCmdbuffer( dos );

  str = ustrchr( commandBuffer, '-' );
  if ( str == nullptr )
  {
    setError( dos, ERROR_SYNTAX_UNABLE );
    return;
  }

  params.fill( 0 );
  pcount = parseBlockparam( dos, params );
  if ( pcount < 0 )
    return;

  str++;
  switch ( *str )
  {
  case 'R':
  case 'W':
    /* Block-Read  - CD56 */
    /* Block-Write - CD73 */
    buf = findBuffer( dos, params[0] );
    if ( buf == nullptr )
    {
      setError( dos, ERROR_NO_CHANNEL );
      return;
    }

    if ( *str == 'R' )
    {
      readSector( dos, buf, buf->pvt.buffer.part, params[2], params[3] );
      if ( commandBuffer[0] == 'B' )
      {
        buf->position = 1;
        buf->lastused = buf->data[0];
      }
      else
      {
        buf->position = 0;
        buf->lastused = 255;
      }
    }
    else
    {
      if ( commandBuffer[0] == 'B' )
        buf->data[0] = static_cast<std::uint8_t>( buf->position - 1 ); // FIXME: Untested, verify!
      writeSector( dos, buf, buf->pvt.buffer.part, params[2], params[3] );
    }
    break;

  case 'P':
    /* Buffer-Position - CDBD */
    buf = findBuffer( dos, params[0] );
    if ( buf == nullptr )
    {
      setError( dos, ERROR_NO_CHANNEL );
      return;
    }
    if ( buf->pvt.buffer.size != 1 )
    {
      /* Extended position for large buffers */
      std::uint8_t count = params[2];

      /* Walk the chain, wrap whenever necessary */
      buf->secondary = static_cast<std::uint8_t>( BUFFER_SEC_CHAIN - params[0] );
      buf = buf->pvt.buffer.first;
      while ( count-- != 0 )
      {
        if ( buf->pvt.buffer.next != nullptr )
          buf = buf->pvt.buffer.next;
        else
          buf = buf->pvt.buffer.first;
      }
      buf->secondary = params[0];
      buf->mustflush = false;
    }
    buf->position = params[1];
    break;

  default:
    setError( dos, ERROR_SYNTAX_UNABLE );
    return;
  }
}

/* ---------- */
/*  C - Copy  */
/* ---------- */
void parseCopy( Dos& dos )
{
  std::uint8_t* const commandBuffer = dos.commands.commandBuffer.data();
  Path srcpath{};
  Path dstpath{};
  std::uint8_t* srcname = nullptr;
  std::uint8_t* dstname = nullptr;
  std::uint8_t* tmp = nullptr;
  std::uint8_t savedtype = 0;
  std::int8_t res = 0;
  Buffer* srcbuf = nullptr;
  Buffer* dstbuf = nullptr;
  CbmDirent dent{};

  cleanCmdbuffer( dos );

  /* Find the = */
  srcname = ustrchr( commandBuffer, '=' );
  if ( srcname == nullptr )
  {
    setError( dos, ERROR_SYNTAX_UNKNOWN );
    return;
  }
  *srcname++ = 0;

  /* Parse the destination name */
  if ( parsePath( dos, commandBuffer + 1, &dstpath, &dstname, 0 ) != 0 )
    return;

  if ( ustrlen( dstname ) == 0 )
  {
    setError( dos, ERROR_SYNTAX_NONAME );
    return;
  }

  /* Check for invalid characters in the destination name */
  if ( checkInvalidName( dos, dstname ) != 0 )
  {
    setError( dos, ERROR_SYNTAX_UNKNOWN );
    return;
  }

  /* Check if the destination file exists */
  res = firstMatch( dos, &dstpath, dstname, FLAG_HIDDEN, &dent );
  if ( res == 0 )
  {
    setError( dos, ERROR_FILE_EXISTS );
    return;
  }

  if ( res > 0 )
    return;

  setError( dos, ERROR_OK );

  srcbuf = allocBuffer( dos );
  dstbuf = allocBuffer( dos );
  if ( srcbuf == nullptr || dstbuf == nullptr )
    return;

  savedtype = 0;
  srcname = ustr1tok( srcname, ',', &tmp );
  while ( srcname != nullptr )
  {
    /* Parse the source path */
    if ( parsePath( dos, srcname, &srcpath, &srcname, 0 ) != 0 )
      goto cleanup;

    /* Open the current source file */
    res = firstMatch( dos, &srcpath, srcname, FLAG_HIDDEN, &dent );
    if ( res != 0 )
      goto cleanup;

    /* Note: A 1541 can't copy REL files. We try to do better. */
    if ( ( dent.typeflags & TYPE_MASK ) == TYPE_REL )
    {
      if ( savedtype != 0 && savedtype != TYPE_REL )
      {
        setError( dos, ERROR_FILE_TYPE_MISMATCH );
        goto cleanup;
      }
      openRel( dos, &srcpath, &dent, srcbuf, 0, 1 );
    }
    else
    {
      if ( savedtype != 0 && savedtype == TYPE_REL )
      {
        setError( dos, ERROR_FILE_TYPE_MISMATCH );
        goto cleanup;
      }
      openRead( dos, &srcpath, &dent, srcbuf );
    }

    if ( dos.errors.currentError != 0 )
      goto cleanup;

    /* Open the destination file (first source only) */
    if ( savedtype == 0 )
    {
      savedtype = dent.typeflags & TYPE_MASK;
      dent = CbmDirent{};
      ustrncpy( dent.name.data(), dstname, CBM_NAME_LENGTH );
      if ( savedtype == TYPE_REL )
        openRel( dos, &dstpath, &dent, dstbuf, srcbuf->recordlen, 1 );
      else
        openWrite( dos, &dstpath, &dent, savedtype, dstbuf, 0 );
    }

    while ( true )
    {
      std::uint8_t tocopy = 0;

      // The uint8_t truncations are the firmware's: 256 - 0 is 0.
      if ( savedtype == TYPE_REL )
        tocopy = srcbuf->recordlen;
      else
        tocopy = static_cast<std::uint8_t>( 256 - dstbuf->position );

      if ( tocopy > ( srcbuf->lastused - srcbuf->position + 1 ) )
        tocopy = static_cast<std::uint8_t>( srcbuf->lastused - srcbuf->position + 1 );

      if ( tocopy > 256 - dstbuf->position )
        tocopy = static_cast<std::uint8_t>( 256 - dstbuf->position );

      std::memcpy( dstbuf->data + dstbuf->position, srcbuf->data + srcbuf->position, tocopy );
      markBufferDirty( dos, dstbuf );
      srcbuf->position =
          static_cast<std::uint8_t>( srcbuf->position + tocopy - 1 ); /* add 1 less, simplifies the test later */
      dstbuf->position = static_cast<std::uint8_t>( dstbuf->position + tocopy );
      dstbuf->lastused = static_cast<std::uint8_t>( dstbuf->position - 1 );

      /* End if we just copied the last data block */
      if ( srcbuf->sendeoi && srcbuf->position == srcbuf->lastused )
        break;

      /* Refill the buffers if required */
      if ( ( srcbuf->recordlen != 0 || srcbuf->position++ == srcbuf->lastused ) && srcbuf->refill( dos, srcbuf ) != 0 )
        goto cleanup;

      if ( ( dstbuf->recordlen != 0 || dstbuf->position == 0 ) && dstbuf->refill( dos, dstbuf ) != 0 )
        goto cleanup;
    }

    /* Close current source file */
    /* Free and reallocate the buffer. This is required because most of the  */
    /* file_open code assumes that it will get a "pristine" buffer with      */
    /* 0 is most of the fields. Allocation cannot fail at this point because */
    /* there is at least one free buffer.                                    */
    cleanupAndFreeBuffer( dos, srcbuf );
    srcbuf = allocBuffer( dos );

    /* Next file */
    srcname = ustr1tok( nullptr, ',', &tmp );
  }

cleanup:
  /* Close the buffers */
  srcbuf->cleanup( dos, srcbuf );
  cleanupAndFreeBuffer( dos, dstbuf );
}

/* ----------------------- */
/*  CP - Change Partition  */
/* ----------------------- */
void parseChangepart( Dos& dos )
{
  std::uint8_t* const commandBuffer = dos.commands.commandBuffer.data();
  std::uint8_t* str = nullptr;
  std::uint8_t part = 0;

  if ( commandBuffer[1] == 'P' )
  {
    cleanCmdbuffer( dos );
    str = commandBuffer + 2;
    part = parsePartition( dos, &str );
  }
  else
  {
    /* Shift-P - binary version */
    part = static_cast<std::uint8_t>( commandBuffer[2] - 1 );
  }

  if ( part >= dos.parser.maxPart )
  {
    setErrorTs( dos, ERROR_PARTITION_ILLEGAL, static_cast<std::uint8_t>( part + 1 ), 0 );
    return;
  }

  dos.parser.currentPart = part;
  /* FIXME
  if (globalflags & AUTOSWAP_ACTIVE)
    set_changelist(NULL, NULLSTRING);
  */

  setErrorTs( dos, ERROR_PARTITION_SELECTED, static_cast<std::uint8_t>( part + 1 ), 0 );
}

/* ------------ */
/*  D commands  */
/* ------------ */
void parseDirect( Dos& dos )
{
  std::uint8_t* const commandBuffer = dos.commands.commandBuffer.data();
  Buffer* buf = nullptr;
  std::uint8_t drive = 0;
  std::uint32_t sector = 0;
  DResult res{};

  /* This also guards against attempts to use the old Duplicate command. */
  /* Its syntax is D1=0, so buffer[2] would never have a value that is   */
  /* a valid secondary address.                                          */
  buf = findBuffer( dos, commandBuffer[2] );
  if ( buf == nullptr )
  {
    setError( dos, ERROR_NO_CHANNEL );
    return;
  }

  if ( buf->pvt.buffer.size > 1 )
  {
    std::uint8_t const oldsec = buf->secondary;
    buf->secondary = static_cast<std::uint8_t>( BUFFER_SEC_CHAIN - oldsec );
    buf = buf->pvt.buffer.first;
    buf->secondary = oldsec;
  }

  buf->position = 0;
  buf->lastused = 255;

  drive = commandBuffer[3];
  // The AVR is little-endian.
  sector = static_cast<std::uint32_t>( commandBuffer[4] ) | ( static_cast<std::uint32_t>( commandBuffer[5] ) << 8U ) |
           ( static_cast<std::uint32_t>( commandBuffer[6] ) << 16U ) |
           ( static_cast<std::uint32_t>( commandBuffer[7] ) << 24U );

  switch ( commandBuffer[1] )
  {
  case 'I':
    /* Get information */
    std::memset( buf->data, 0, 256 );
    if ( diskGetinfo( drive, commandBuffer[4], buf->data ) != DResult::RES_OK )
    {
      setError( dos, ERROR_DRIVE_NOT_READY );
      return;
    }
    break;

  case 'R':
    /* Read sector */
    if ( buf->pvt.buffer.size < 2 )
    { // FIXME: Assumes 512-byte sectors
      setError( dos, ERROR_BUFFER_TOO_SMALL );
      return;
    }
    res = diskRead( drive, buf->data, sector, 1 );
    switch ( res )
    {
    case DResult::RES_OK:
      return;

    case DResult::RES_ERROR:
      setError( dos, ERROR_READ_NOHEADER ); // Any random READ ERROR
      return;

    case DResult::RES_PARERR:
    case DResult::RES_NOTRDY:
    default:
      setErrorTs( dos, ERROR_DRIVE_NOT_READY, static_cast<std::uint8_t>( res ), 0 );
      return;
    }

  case 'W':
    /* Write sector */
    if ( buf->pvt.buffer.size < 2 )
    { // FIXME: Assumes 512-byte sectors
      setError( dos, ERROR_BUFFER_TOO_SMALL );
      return;
    }
    res = diskWrite( drive, buf->data, sector, 1 );
    switch ( res )
    {
    case DResult::RES_OK:
      return;

    case DResult::RES_WRPRT:
      setError( dos, ERROR_WRITE_PROTECT );
      return;

    case DResult::RES_ERROR:
      setError( dos, ERROR_WRITE_VERIFY );
      return;

    case DResult::RES_NOTRDY:
    case DResult::RES_PARERR:
    default:
      setErrorTs( dos, ERROR_DRIVE_NOT_READY, static_cast<std::uint8_t>( res ), 0 );
      return;
    }

  default:
    setError( dos, ERROR_SYNTAX_UNABLE );
    break;
  }
}

/* -------------------------- */
/*  G-P - Get Partition info  */
/* -------------------------- */
void parseGetpartition( Dos& dos )
{
  std::uint8_t* const commandBuffer = dos.commands.commandBuffer.data();
  std::uint8_t* const errorBuffer = dos.errors.errorBuffer.data();
  std::uint8_t* ptr = nullptr;
  std::uint8_t part = 0;

  if ( dos.commands.commandLength < 3 ) /* FIXME: should this set an error? */
    return;

  if ( commandBuffer[1] != '-' || commandBuffer[2] != 'P' )
  {
    setError( dos, ERROR_SYNTAX_UNKNOWN );
    return;
  }

  if ( dos.commands.commandLength == 3 || commandBuffer[3] == 0xff )
    part = static_cast<std::uint8_t>( dos.parser.currentPart + 1 );
  else
    part = commandBuffer[3];

  dos.buffers.buffers[ERRORBUFFER_IDX].position = 0;
  dos.buffers.buffers[ERRORBUFFER_IDX].lastused = 30;

  std::memset( errorBuffer, 0, 30 );
  errorBuffer[30] = 13;
  ptr = errorBuffer;

  if ( part > dos.parser.maxPart )
  {
    /* Nonexisting partition - return empty answer */
    ptr[30] = 13;
    return;
  }

  if ( part == 0 )
  {
    /* System partition - return static info */
    std::memcpy( ptr, SYSTEM_PARTITION_INFO.data(), SYSTEM_PARTITION_INFO.size() );
    return;
  }

  part = static_cast<std::uint8_t>( part - 1 );
  Partition const& thePartition = dos.parser.partition[part];

  /* Create partition info */
  if ( thePartition.fop == &D64OPS )
  {
    /* Use type of mounted image as partition type */
    *ptr++ = thePartition.imagetype & D64_TYPE_MASK;
  }
  else
  {
    /* Use native for anything else */
    *ptr++ = 1;
  }
  *ptr++ = 0xe2; // 1.6MB disk - "reserved" for HD

  *ptr++ = static_cast<std::uint8_t>( part + 1 );

  /* Read partition label */
  std::memset( ptr, 0xa0, 16 );
  if ( diskLabel( dos, part, dos.buffers.opsScratch.data() ) != 0 )
  {
    return;
  }

  std::uint8_t const* inptr = dos.buffers.opsScratch.data();
  std::uint8_t* outptr = ptr;
  while ( *inptr != 0 )
    *outptr++ = *inptr++;

  ptr += 16;
  *ptr++ = static_cast<std::uint8_t>( ( thePartition.fatfs.fatbase >> 16U ) & 0xffU );
  *ptr++ = static_cast<std::uint8_t>( ( thePartition.fatfs.fatbase >> 8U ) & 0xffU );
  *ptr++ = static_cast<std::uint8_t>( thePartition.fatfs.fatbase & 0xffU );
  ptr += 5; // reserved bytes

  std::uint32_t size = 0;

  if ( thePartition.fop == &D64OPS )
  {
    /* return the size of the disk image, rounded up */
    size = ( thePartition.imagehandle.fsize + 511 ) / 512;
  }
  else
  {
    /* return the size of the FAT partition */
    size = ( thePartition.fatfs.maxClust - 1 ) * thePartition.fatfs.csize;
  }

  size = std::min<std::uint32_t>( size, 0xffffff );

  *ptr++ = static_cast<std::uint8_t>( ( size >> 16U ) & 0xffU );
  *ptr++ = static_cast<std::uint8_t>( ( size >> 8U ) & 0xffU );
  *ptr = static_cast<std::uint8_t>( size & 0xffU );
}

/* ---------------- */
/*  I - Initialize  */
/* ---------------- */
void parseInitialize( Dos& dos )
{
  // disk_state is DISK_OK while a card is in and answering; HostFs's card
  // fails no access, so only its absence is not OK.
  if ( !dos.card.present() )
    setErrorTs( dos, ERROR_READ_NOSYNC, 18, 0 );
  else
    freeMultipleBuffers( dos, FMB_USER_CLEAN );
}

/* ------------ */
/*  M commands  */
/* ------------ */

/* --- M-E --- */
void handleMemexec( Dos& dos )
{
  std::uint8_t const* const commandBuffer = dos.commands.commandBuffer.data();
  std::uint16_t address = 0;

  if ( dos.commands.commandLength < 5 )
    return;

  address = static_cast<std::uint16_t>( commandBuffer[3] + ( commandBuffer[4] << 8U ) );
  runLoader( dos, address );
}

/* --- M-R --- */
void handleMemread( Dos& dos )
{
  std::uint8_t const* const commandBuffer = dos.commands.commandBuffer.data();
  auto& errorBuffer = dos.errors.errorBuffer;
  auto& romfile = dos.commands.romfile;
  std::uint8_t const* const romFilename = dos.commands.romFilename.data();
  FResult res{};
  std::uint16_t address = 0;
  bool useInternal = true;

  if ( dos.commands.commandLength < 6 )
    return;

  address = static_cast<std::uint16_t>( commandBuffer[3] + ( commandBuffer[4] << 8U ) );

  if ( address >= 0x8000 && romFilename[0] != 0 )
  {
    Partition& current = dos.parser.partition[dos.parser.currentPart];
    Partition& first = dos.parser.partition[0];

    /* Try to use the rom file as data source - look in the current dir first */
    current.fatfs.currDir = current.currentDir.fat;
    res = dos.card.fOpen( &current.fatfs, &romfile, romFilename, FA_READ | FA_OPEN_EXISTING );

    if ( res != FResult::OK )
    {
      /* Not successful, try root dir */
      current.fatfs.currDir = 0;
      res = dos.card.fOpen( &current.fatfs, &romfile, romFilename, FA_READ | FA_OPEN_EXISTING );

      if ( res != FResult::OK )
      {
        /* Not successful, try root of drive 0 */
        first.fatfs.currDir = 0;
        res = dos.card.fOpen( &first.fatfs, &romfile, romFilename, FA_READ | FA_OPEN_EXISTING );

        /* No file available - use internal table */
      }
    }

    if ( res == FResult::OK )
    {
      /* One of the f_open calls was successful */
      address = static_cast<std::uint16_t>( address - 0x8000 );
      if ( romfile.fsize < 32 * 1024U )
        /* Allow 16K 1541 roms */
        address = static_cast<std::uint16_t>( address & 0x3fffU );

      if ( ( romfile.fsize & 0x3fffU ) != 0 )
        /* Skip header bytes */
        address = static_cast<std::uint16_t>( address + ( romfile.fsize & 0x3fffU ) );

      res = dos.card.fLseek( &romfile, address );
      if ( res == FResult::OK )
      {
        /* Clamp maximum read length to buffer size */
        std::uint8_t bytes = commandBuffer[5];
        if ( bytes > ERROR_BUFFER_SIZE )
          bytes = static_cast<std::uint8_t>( ERROR_BUFFER_SIZE );

        unsigned bytesread = 0;
        res = dos.card.fRead( &romfile, errorBuffer.data(), bytes, &bytesread );
        if ( res == FResult::OK && bytesread == bytes )
          useInternal = false;
      }

      /* Note: f_close isn't neccessary in FatFs for read-only files */
      // It is for HostFs, which holds a host file open until then.
      dos.card.fClose( &romfile );
    }
  }

  if ( useInternal )
  {
    /* Check some special addresses used for drive detection. */
    // The address compared is the one the ROM file path may have changed, as
    // in the firmware's goto into this branch.
    for ( MagicValue const& p : C1541_MAGICS )
    {
      if ( p.address == 0 )
        break;
      if ( p.address == address )
      {
        errorBuffer[0] = p.val[0];
        errorBuffer[1] = p.val[1];
        break;
      }
    }
  }

  /* possibly the host wants to read more bytes than error_buffer size */
  /* we ignore this knowing that we return nonsense in this case       */
  dos.buffers.buffers[ERRORBUFFER_IDX].data = errorBuffer.data();
  dos.buffers.buffers[ERRORBUFFER_IDX].position = 0;
  dos.buffers.buffers[ERRORBUFFER_IDX].lastused = static_cast<std::uint8_t>( commandBuffer[5] - 1 );
}

/* --- M-W --- */
// The petSD+ is built with CONFIG_HAVE_IEC, so its M-W is the full one; of it,
// the fast loaders' detection and capture are left out, and the CRC that M-E
// reports is kept.
void handleMemwrite( Dos& dos )
{
  std::uint8_t const* const commandBuffer = dos.commands.commandBuffer.data();
  std::uint16_t address = 0;

  if ( dos.commands.commandLength < 6 )
    return;

  address = static_cast<std::uint16_t>( commandBuffer[3] + ( commandBuffer[4] << 8U ) );

  if ( address == 119 )
  {
    /* Change device address, 1541 style */
    dos.deviceAddress = commandBuffer[6] & 0x1fU;
    return;
  }

  if ( address == 0x1c06 || address == 0x1c07 || address == 0x1802 )
  {
    /* Ignored addresses:                                          */
    /* - VIA 2 timer (1c06, 1c07), determines IRQ frequency        */
    /* - VIA 1 DDRB, written by N0SD0S to fix Action Replay issues */
    return;
  }

  for ( unsigned i = 0; i < commandBuffer[5]; i++ )
  {
    // The firmware reads on past its command buffer for a length that does
    // not fit in it; what lies there is not the buffer's, and reads as 0 here.
    std::size_t const index = i + 6;
    std::uint8_t const byte = index < dos.commands.commandBuffer.size() ? commandBuffer[index] : 0;
    dos.commands.datacrc = crc16Update( dos.commands.datacrc, byte );
  }
}

/* --- M subparser --- */
void parseMemory( Dos& dos )
{
  std::uint8_t const command = dos.commands.commandBuffer[2];
  if ( command == 'W' )
    handleMemwrite( dos );
  else if ( command == 'E' )
    handleMemexec( dos );
  else if ( command == 'R' )
    handleMemread( dos );
  else
    setError( dos, ERROR_SYNTAX_UNKNOWN );
}

/* --------- */
/*  N - New  */
/* --------- */
void parseNew( Dos& dos )
{
  std::uint8_t* const commandBuffer = dos.commands.commandBuffer.data();
  std::uint8_t* name = nullptr;
  std::uint8_t* id = nullptr;
  std::uint8_t part = 0;

  cleanCmdbuffer( dos );

  name = commandBuffer + 1;
  part = parsePartition( dos, &name );
  name = ustrchr( commandBuffer, ':' );
  if ( name == nullptr )
  {
    setError( dos, ERROR_SYNTAX_NONAME );
    return;
  }
  name++;

  id = ustrchr( name, ',' );
  if ( id != nullptr )
  {
    *id = 0;
    id++;
  }

  // NODISKEMU calls the partition's format with the number unchecked; a
  // partition that does not exist answers as parse_path answers for one.
  if ( part >= dos.parser.maxPart )
  {
    setError( dos, ERROR_DRIVE_NOT_READY );
    return;
  }

  format( dos, part, name, id );
}

/* -------------- */
/*  P - Position  */
/* -------------- */
void parsePosition( Dos& dos )
{
  std::uint8_t const* const commandBuffer = dos.commands.commandBuffer.data();
  Buffer* buf = nullptr;

  dos.commands.commandLength = dos.commands.originalLength;
  cleanCmdbuffer( dos );

  std::uint8_t const commandLength = dos.commands.commandLength;
  if ( commandLength >= 2 )
    buf = findBuffer( dos, commandBuffer[1] & 0x0fU );
  if ( buf == nullptr )
  {
    setError( dos, ERROR_NO_CHANNEL );
    return;
  }

  if ( buf->seek == nullptr )
  {
    setError( dos, ERROR_SYNTAX_UNABLE );
    return;
  }

  if ( buf->recordlen != 0 )
  {
    /* REL file */
    std::uint16_t record = 1;
    std::uint8_t pos = 1;

    if ( commandLength > 1 )
      record = commandBuffer[2];
    if ( commandLength > 2 )
      record = static_cast<std::uint16_t>( record | ( commandBuffer[3] << 8U ) );
    if ( commandLength > 3 )
      pos = commandBuffer[4];

    if ( pos > buf->recordlen )
    {
      setError( dos, ERROR_RECORD_OVERFLOW );
      return;
    }

    if ( record != 0 )
      record--;

    if ( pos != 0 )
      pos--;

    buf->seek( dos, buf, record * static_cast<std::uint32_t>( buf->recordlen ), pos );
  }
  else
  {
    /* Non-REL seek uses a straight little-endian offset */
    std::uint32_t const offset = static_cast<std::uint32_t>( commandBuffer[2] ) |
                                 ( static_cast<std::uint32_t>( commandBuffer[3] ) << 8U ) |
                                 ( static_cast<std::uint32_t>( commandBuffer[4] ) << 16U ) |
                                 ( static_cast<std::uint32_t>( commandBuffer[5] ) << 24U );

    buf->seek( dos, buf, offset, 0 );
  }
}

/* ------------ */
/*  R - Rename  */
/* ------------ */
void parseRename( Dos& dos )
{
  std::uint8_t* const commandBuffer = dos.commands.commandBuffer.data();
  Path oldpath{};
  Path newpath{};
  std::uint8_t* oldname = nullptr;
  std::uint8_t* newname = nullptr;
  CbmDirent dent{};
  std::int8_t res = 0;

  cleanCmdbuffer( dos );

  /* Find the boundary between the names */
  oldname = ustrchr( commandBuffer, '=' );
  if ( oldname == nullptr )
  {
    setError( dos, ERROR_SYNTAX_UNKNOWN );
    return;
  }
  *oldname++ = 0;

  /* Parse both names */
  if ( parsePath( dos, commandBuffer + 1, &newpath, &newname, 0 ) != 0 )
    return;

  if ( parsePath( dos, oldname, &oldpath, &oldname, 0 ) != 0 )
    return;

  /* Rename can't move files across directories */
  // The firmware memcmps the two dir_t, which have no padding on the AVR.
  if ( oldpath.dir.fat != newpath.dir.fat || oldpath.dir.dxx.track != newpath.dir.dxx.track ||
       oldpath.dir.dxx.sector != newpath.dir.dxx.sector )
  {
    setError( dos, ERROR_FILE_NOT_FOUND );
    return;
  }

  /* Check for invalid characters in the new name */
  if ( checkInvalidName( dos, newname ) != 0 )
  {
    /* This isn't correct for all cases, but for most. */
    setError( dos, ERROR_SYNTAX_UNKNOWN );
    return;
  }

  /* Don't allow an empty new name */
  /* The 1541 renames the file to "=" in this case, but I consider that a bug. */
  if ( ustrlen( newname ) == 0 )
  {
    setError( dos, ERROR_SYNTAX_NONAME );
    return;
  }

  /* Check if the new name already exists */
  res = firstMatch( dos, &newpath, newname, FLAG_HIDDEN, &dent );
  if ( res == 0 )
  {
    setError( dos, ERROR_FILE_EXISTS );
    return;
  }

  if ( res > 0 )
    /* first_match generated an error other than File Not Found, abort */
    return;

  /* Clear the FNF */
  setError( dos, ERROR_OK );

  /* Check if the old name exists */
  if ( firstMatch( dos, &oldpath, oldname, FLAG_HIDDEN, &dent ) != 0 )
    return;

  rename( dos, &oldpath, &dent, newname );
}

/* ------------- */
/*  S - Scratch  */
/* ------------- */
void parseScratch( Dos& dos )
{
  std::uint8_t* const commandBuffer = dos.commands.commandBuffer.data();
  CbmDirent dent{};
  std::int8_t res = 0;
  std::uint8_t count = 0;
  std::uint8_t cnt = 0;
  std::uint8_t* filename = nullptr;
  std::uint8_t* tmp = nullptr;
  std::uint8_t* name = nullptr;
  Path path{};

  cleanCmdbuffer( dos );

  filename = ustr1tok( commandBuffer + 1, ',', &tmp );

  count = 0;
  /* Loop over all file names */
  while ( filename != nullptr )
  {
    // The firmware ignores parse_path's failure and goes on with a path and a
    // name it may not have set. That cannot be ported; the name is skipped,
    // as if what the firmware went on with had matched nothing.
    if ( parsePath( dos, filename, &path, &name, 0 ) != 0 )
    {
      filename = ustr1tok( nullptr, ',', &tmp );
      continue;
    }

    if ( opendir( dos, &dos.buffers.matchdh, &path ) != 0 )
      return;

    while ( true )
    {
      res = nextMatch( dos, &dos.buffers.matchdh, name, nullptr, nullptr, FLAG_HIDDEN, &dent );
      if ( res < 0 )
        break;
      if ( res > 0 )
        return;

      /* Skip directories */
      if ( ( dent.typeflags & TYPE_MASK ) == TYPE_DIR )
        continue;
      cnt = fileDelete( dos, &path, &dent );
      if ( cnt != 255 )
        count = static_cast<std::uint8_t>( count + cnt );
      else
        return;
    }

    filename = ustr1tok( nullptr, ',', &tmp );
  }

  setErrorTs( dos, ERROR_SCRATCHED, count, 0 );
}

/* ------------------ */
/*  T - Time commands */
/* ------------------ */

/* --- T subparser --- */
// rtc_state is RTC_NOT_FOUND: the RTC is left out, and with it T-R and T-W,
// which the firmware never reaches without one.
void parseTime( Dos& dos )
{
  setError( dos, ERROR_SYNTAX_UNKNOWN );
}

} // namespace

/* day of week calculation by M. Keith and T. Craver via
   https://en.wikipedia.org/wiki/Determination_of_the_day_of_the_week

   Please note that unlike struct tm time.tm_mon which stores months
   in a 0-11 range, this function requires months in a 1-12 range
*/
std::uint8_t dayOfWeek( std::uint16_t y, std::uint8_t m, std::uint8_t d )
{
  // The firmware's one expression, with its truncation of d to 8 bits kept:
  // (d+=m<3?y--:y-2,23*m/9+d+4+y/4-y/100+y/400)%7
  y = static_cast<std::uint16_t>( y + 1900 );
  if ( m < 3 )
  {
    d = static_cast<std::uint8_t>( d + y );
    y--;
  }
  else
  {
    d = static_cast<std::uint8_t>( d + y - 2 );
  }
  return static_cast<std::uint8_t>( ( ( 23 * m / 9 ) + d + 4 + ( y / 4 ) - ( y / 100 ) + ( y / 400 ) ) % 7 );
}

namespace
{

/* ------------ */
/*  U commands  */
/* ------------ */
void parseUser( Dos& dos )
{
  std::uint8_t* const commandBuffer = dos.commands.commandBuffer.data();

  if ( commandBuffer[1] == 202 )
  {
    /* The real hard reset command */
    // system_reset() restarts the AVR and does not return; dosInit stands for
    // it, and leaves the 73 the restarted drive shows.
    dosInit( dos );
    return;
  }

  std::uint8_t const c = commandBuffer[1] & 15U;
  switch ( c )
  {
  case 0:
    /* U0 - only device address changes for now */
    if ( ( commandBuffer[2] & 0x1fU ) == 0x1e && commandBuffer[3] >= 4 && commandBuffer[3] <= 30 )
    {
      dos.deviceAddress = commandBuffer[3];
      break;
    }
    setError( dos, ERROR_SYNTAX_UNKNOWN );
    break;

  case 1:
    /* Tiny little hack: Rewrite as (B)-R and call that                */
    /* This will always work because there is either a : in the string */
    /* or the drive will start parsing at buf[3].                      */
    commandBuffer[0] = '-';
    commandBuffer[1] = 'R';
    parseBlock( dos );
    break;

  case 2:
    /* Tiny little hack: see above case for rationale */
    commandBuffer[0] = '-';
    commandBuffer[1] = 'W';
    parseBlock( dos );
    break;

  // c is at most 15, so this case is never taken: UI (0x49) takes the
  // default below, as in the firmware.
  case '9':
    if ( dos.commands.commandLength == 2 )
    {
      /* Soft-reset - just return the dos version */
      setError( dos, ERROR_DOSVERSION );
      return;
    }
    switch ( commandBuffer[2] )
    {
    case '+':
      dos.commands.globalflags &= static_cast<std::uint8_t>( ~VC20MODE );
      break;

    case '-':
      dos.commands.globalflags |= VC20MODE;
      break;

    default:
      /* Soft-reset - just return the dos version */
      setError( dos, ERROR_DOSVERSION );
      break;
    }
    break;

  case 10:
    /* Reset - technically hard-reset */
    /* Faked because Ultima 5 sends UJ. */
    freeMultipleBuffers( dos, FMB_USER );
    setError( dos, ERROR_DOSVERSION );
    break;

  default:
    /* start program in buffer */
    runLoader( dos, static_cast<std::uint16_t>( 0x500 + ( 3 * ( commandBuffer[1] - '3' ) ) ) );
    break;
  }
}

/* ------------ */
/*  X commands  */
/* ------------ */
void parseXcommand( Dos& dos )
{
  std::uint8_t* const commandBuffer = dos.commands.commandBuffer.data();
  std::uint8_t num = 0;
  std::uint8_t* str = nullptr;
  Path path{};

  cleanCmdbuffer( dos );

  switch ( commandBuffer[1] )
  {
  case 'E':
    /* Change file extension mode */
    str = commandBuffer + 2;
    if ( *str == '+' )
    {
      dos.commands.globalflags |= EXTENSION_HIDING;
    }
    else if ( *str == '-' )
    {
      dos.commands.globalflags &= static_cast<std::uint8_t>( ~EXTENSION_HIDING );
    }
    else
    {
      num = static_cast<std::uint8_t>( parseNumber( dos, &str ) );
      if ( num > 4 )
      {
        setError( dos, ERROR_SYNTAX_UNKNOWN );
      }
      else
      {
        dos.fat.fileExtensionMode = num;
        if ( num >= 3 )
          dos.commands.globalflags |= EXTENSION_HIDING;
      }
    }
    setErrorTs( dos, ERROR_STATUS, dos.deviceAddress, 0 );
    break;

  case 'D':
    /* drive config */
    // return error for units without MUX support
    setError( dos, ERROR_SYNTAX_UNKNOWN );
    break;

  case 'I':
    /* image-as-directory mode */
    str = commandBuffer + 2;
    num = static_cast<std::uint8_t>( parseNumber( dos, &str ) );
    if ( num <= 2 )
    {
      dos.files.imageAsDir = num;
    }
    else
    {
      setError( dos, ERROR_SYNTAX_UNKNOWN );
    }
    break;

  case 'M':
    /* Enable or disable LCD menu system, used to switch
       between buttons for menu usage and DIP switches
       to set the device address */
    str = commandBuffer + 2;
    if ( *str == '+' )
    {
      dos.commands.menuSystemEnabled = true;
    }
    else if ( *str == '-' )
    {
      dos.commands.menuSystemEnabled = false;
    }
    else
    {
      setError( dos, ERROR_SYNTAX_UNKNOWN );
      break;
    }
    // write_configuration() is left out with the EEPROM; system_reset()
    // restarts the AVR, and dosInit stands for it.
    dosInit( dos );
    break;

  case 'W':
    /* Write configuration */
    // write_configuration() is left out with the EEPROM.
    setErrorTs( dos, ERROR_STATUS, dos.deviceAddress, 0 );
    break;

  case 'R':
    /* Set Rom-file */
    if ( commandBuffer[2] == ':' )
    {
      if ( dos.commands.commandLength > ROM_NAME_LENGTH + 3 )
      {
        setError( dos, ERROR_SYNTAX_TOOLONG );
      }
      else
      {
        ustrcpy( dos.commands.romFilename.data(), commandBuffer + 3 );
      }
    }
    else
    {
      /* Clear rom name */
      dos.commands.romFilename[0] = 0;
    }
    break;

  case 'S':
    /* Swaplist */
    if ( parsePath( dos, commandBuffer + 2, &path, &str, 0 ) != 0 )
      return;

    // FIXME set_changelist(&path, str);
    break;

  case '*':
    /* Post-* matching */
    num = parseBool( dos );
    if ( num != 255 )
    {
      if ( num != 0 )
        dos.commands.globalflags |= POSTMATCH;
      else
        dos.commands.globalflags &= static_cast<std::uint8_t>( ~POSTMATCH );

      setErrorTs( dos, ERROR_STATUS, dos.deviceAddress, 0 );
    }
    break;

  case '?':
    /* Output the long version string */
    setError( dos, ERROR_LONGVERSION );
    break;

  default:
    if ( dos.commands.commandLength != 1 )
      /* unknown command */
      setError( dos, ERROR_SYNTAX_UNKNOWN );
    else
      /* plain X by itself, show the extended status */
      setErrorTs( dos, ERROR_STATUS, dos.deviceAddress, 0 );

    break;
  }
}

} // namespace

/* ------------------------------------------------------------------------- */
/*  Main command parser function                                             */
/* ------------------------------------------------------------------------- */

void parseDoscommand( Dos& dos )
{
  auto& commands = dos.commands;
  std::uint8_t const* const commandBuffer = commands.commandBuffer.data();

  /* Set default message: Everything ok */
  setError( dos, ERROR_OK );

  /* Abort if the command is too long */
  if ( commands.commandLength == COMMAND_BUFFER_SIZE )
  {
    setError( dos, ERROR_SYNTAX_TOOLONG );
    return;
  }

  /* Remove one CR at end of command */
  commands.originalLength = commands.commandLength;
  if ( commands.commandLength > 0 && commandBuffer[commands.commandLength - 1] == 0x0d )
    commands.commandLength--;

  /* Abort if there is no command */
  if ( commands.commandLength == 0 )
  {
    setError( dos, ERROR_SYNTAX_UNABLE );
    return;
  }

  /* MD/CD/RD clash with other commands, so they're checked first */
  if ( commandBuffer[0] != 'X' && commandBuffer[1] == 'D' )
  {
    parseDircommand( dos );
    return;
  }

  switch ( commandBuffer[0] )
  {
  case 'B':
    /* Block-Something */
    parseBlock( dos );
    break;

  case 'C':
    /* Copy or Change Partition */
    if ( commandBuffer[1] == 'P' || commandBuffer[1] == 0xd0 )
      parseChangepart( dos );
    else
      /* Copy a file */
      parseCopy( dos );
    break;

  case 'D':
    /* Direct sector access (was duplicate in CBM drives) */
    parseDirect( dos );
    break;

  case 'G':
    /* Get-Partition */
    parseGetpartition( dos );
    break;

  case 'I':
    /* Initialize */
    parseInitialize( dos );
    break;

  case 'M':
    /* Memory-something */
    parseMemory( dos );
    break;

  case 'N':
    /* New */
    parseNew( dos );
    break;

  case 'P':
    /* Position */
    parsePosition( dos );
    break;

  case 'R':
    /* Rename */
    parseRename( dos );
    break;

  case 'S':
    if ( commands.commandLength == 3 && commandBuffer[1] == '-' )
    {
      /* Swap drive number */
      setError( dos, ERROR_SYNTAX_UNABLE );
      break;
    }
    /* Scratch */
    parseScratch( dos );
    break;

  case 'T':
    parseTime( dos );
    break;

  case 'U':
    parseUser( dos );
    break;

  case 'X':
    parseXcommand( dos );
    break;

  default:
    setError( dos, ERROR_SYNTAX_UNKNOWN );
    break;
  }
}

} // namespace pet::sd2pet
