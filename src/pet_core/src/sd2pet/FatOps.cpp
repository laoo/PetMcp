// NODISKEMU's src/fatops.c and fatops.h at commit e51a6df: the FAT operations,
// on the card HostFs makes of a host directory, and the mounting of the images
// on it. fatMountHostImage is not NODISKEMU's: it is the emulator's "mount this
// image file", done as CD of its entry.
//
// Left out: the UART debug output (uart_putc), the LEDs (set_dirty_led,
// update_leds), the display (display_current_part, and the current directory
// image_unmount sends to it: no display is found), and the partitions of
// fatops_init's f_mount loop: the card is one partition, as HostFs mounts it.
// fatops_init's #ifndef HAVE_HOTPLUG branch is taken, hot-plugging being left
// out.

#include "sd2pet/FatOps.hpp"

#include "sd2pet/Dos.hpp"
#include "sd2pet/Utils.hpp"

#include <cstring>
#include <string>

namespace pet::sd2pet
{

namespace
{

constexpr std::uint8_t P00_HEADER_SIZE = 26;
constexpr std::uint8_t P00_CBMNAME_OFFSET = 8;
constexpr std::uint8_t P00_RECORDLEN_OFFSET = 25;

constexpr char const* BOOTSECTOR_FILE = "bootsect.128";

constexpr char const* P00MARKER = "C64File";
constexpr std::size_t P00MARKER_LENGTH = 7;

enum class ExtType : std::uint8_t
{
  UNKNOWN,
  IS_X00,
  IS_TYPE
};

/// The empty path l_getfree is given (NULLSTRING).
constexpr std::uint8_t NULLSTRING[] = { 0 }; // NOLINT(modernize-avoid-c-arrays): a C string, as FatFs takes it

bool isDigit( std::uint8_t c )
{
  return c >= '0' && c <= '9';
}

/// toupper as avr-libc has it: ASCII only.
std::uint8_t toUpper( std::uint8_t c )
{
  return ( c >= 'a' && c <= 'z' ) ? static_cast<std::uint8_t>( c - 0x20 ) : c;
}

Partition& partitionOf( Dos& dos, std::uint8_t part )
{
  return dos.parser.partition.at( part );
}

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

/**
 * check_extension - check for known file-type-based name extensions
 * @name: pointer to the file name
 * @ext : pointer to pointer to the file extension
 *
 * This function checks if the given file name has an extension that
 * indicates a specific file type like PRG/SEQ/P00/S00/... The ext
 * pointer will be set to the first character of the extension if
 * any is present or NULL if not. Returns EXT_IS_X00 for x00,
 * EXT_IS_TYPE for PRG/SEQ/... or EXT_UNKNOWN for an unknown file extension.
 */
ExtType checkExtension( std::uint8_t* name, std::uint8_t** ext )
{
  std::uint8_t f = 0;
  std::uint8_t s = 0;
  std::uint8_t t = 0;

  /* Search for the file extension */
  *ext = ustrrchr( name, '.' );
  if ( *ext != nullptr )
  {
    f = *( ++( *ext ) );
    s = *( *ext + 1 );
    t = *( *ext + 2 );
    if ( ( f == 'P' || f == 'S' || f == 'U' || f == 'R' ) && isDigit( s ) && isDigit( t ) )
      return ExtType::IS_X00;
    if ( ( f == 'P' && s == 'R' && t == 'G' ) || ( f == 'S' && s == 'E' && t == 'Q' ) ||
         ( f == 'R' && s == 'E' && t == 'L' ) || ( f == 'U' && s == 'S' && t == 'R' ) )
      return ExtType::IS_TYPE;
  }
  return ExtType::UNKNOWN;
}

/**
 * is_valid_fat_char - checks if a character is valid on FAT
 * @c: character to check
 *
 * This function checks if @c is a valid character for a FAT
 * file name. Returns true if it is, false if not.
 */
bool isValidFatChar( std::uint8_t const c )
{
  if ( c < 32 || c > 126 )
    return false;
  return std::strchr( "\"*,/\\:=?@", c ) == nullptr;
}

/**
 * is_valid_fat_name - checks if a file name is valid on FAT
 * @name: name to check
 *
 * This function checks if @name is a valid name for a FAT
 * file. Returns true if it is, false if not.
 */
bool isValidFatName( std::uint8_t const* name )
{
  std::uint8_t const* ptr = name;

  /* check the first character */
  if ( *name == ' ' || *name == '.' )
    return false;

  /* check all characters for validity */
  while ( *ptr != 0 )
  {
    if ( !isValidFatChar( *ptr++ ) )
      return false;
  }

  /* check the last character */
  // NODISKEMU checks the first one again here, so a name may end in a space
  // or a dot.
  ptr--;

  return *name != ' ' && *name != '.';
}

/**
 * build_name - convert PETSCII file name to valid FAT name
 * @name: pointer to a PETSCII file name to be converted
 * @type: file type
 *
 * This function converts a PETSCII file name to a suitable
 * FAT file name in-place. Returns a pointer to the last
 * character of the PC64 file extension if it was
 * created or NULL if not.
 */
std::uint8_t* buildName( Dos& dos, std::uint8_t* name, std::uint8_t type )
{
  auto const fileExtensionMode = dos.fat.fileExtensionMode;

  /* convert to PETSCII */
  pet2asc( name );

  /* known disk-image extensions are always without header or suffix */
  if ( type == TYPE_PRG && checkImageext( name ) != ImgType::UNKNOWN )
    return nullptr;

  /* PC64 mode or invalid FAT name? */
  if ( ( fileExtensionMode == 1 && type != TYPE_PRG ) || fileExtensionMode == 2 || !isValidFatName( name ) )
  {
    std::uint8_t* x00ext = nullptr;

    /* Append .[PSUR]00 suffix to the file name */
    while ( *name != 0 )
    {
      if ( isValidFatChar( *name ) )
      {
        name++;
      }
      else
      {
        *name++ = '_';
      }
    }
    *name++ = '.';
    *name++ = FILETYPES.at( std::size_t{ 3 } * type );
    *name++ = '0';
    x00ext = name;
    *name++ = '0';
    *name = 0;

    return x00ext;
  }

  /* type-suffix mode? */
  if ( ( fileExtensionMode == 3 && type != TYPE_PRG ) || ( fileExtensionMode == 4 ) )
  {
    /* Append type suffix to the file name */
    while ( *name != 0 )
      name++;
    *name++ = '.';
    std::memcpy( name, FILETYPES.data() + ( std::size_t{ TYPE_LENGTH } * ( type & EXT_TYPE_MASK ) ), TYPE_LENGTH );
    *( name + 3 ) = 0;

    return nullptr;
  }

  /* extension mode 0 and no special case */
  return nullptr;
}

/* ------------------------------------------------------------------------- */
/*  Callbacks                                                                */
/* ------------------------------------------------------------------------- */

/**
 * fat_file_read - read the next data block into the buffer
 * @buf: buffer to be worked on
 *
 * This function reads the next block of data from the associated file into
 * the given buffer. Used as a refill-callback when reading files
 */
std::uint8_t fatFileRead( Dos& dos, Buffer* buf )
{
  FResult res{};
  unsigned bytesread = 0;

  buf->fptr = buf->pvt.fat.fh.fptr - buf->pvt.fat.headersize;

  res = dos.card.fRead( &buf->pvt.fat.fh, buf->data + 2, ( buf->recordlen != 0 ? buf->recordlen : 254 ), &bytesread );
  if ( res != FResult::OK )
  {
    parseError( dos, res, 1 );
    freeBuffer( dos, buf );
    return 1;
  }

  /* The bus protocol can't handle 0-byte-files */
  if ( bytesread == 0 )
  {
    bytesread = 1;
    /* Experimental data suggests that this may be correct */
    buf->data[2] = ( buf->recordlen != 0 ? 255 : 13 );
  }

  buf->position = 2;
  buf->lastused = static_cast<std::uint8_t>( bytesread + 1 );
  if ( buf->recordlen != 0 ) // strip nulls from end of REL record.
    while ( buf->data[buf->lastused] == 0 )
    {
      --buf->lastused;
      if ( buf->lastused <= 1 )
        break;
    }
  buf->sendeoi = bytesread < 254 || ( buf->pvt.fat.fh.fsize - buf->pvt.fat.fh.fptr ) == 0 || buf->recordlen != 0;

  return 0;
}

/**
 * write_data - write the current buffer data
 * @buf: buffer to be worked on
 *
 * This function writes the current contents of the given buffer into its
 * associated file.
 */
std::uint8_t writeData( Dos& dos, Buffer* buf )
{
  FResult res{};
  unsigned byteswritten = 0;

  if ( !buf->mustflush )
    buf->lastused = static_cast<std::uint8_t>( buf->position - 1 );

  if ( buf->recordlen > buf->lastused - 1 )
    std::memset( buf->data + buf->lastused + 1, 0, static_cast<std::size_t>( buf->recordlen - ( buf->lastused - 1 ) ) );

  if ( buf->recordlen != 0 )
    buf->lastused = static_cast<std::uint8_t>( buf->recordlen + 1 );

  res = dos.card.fWrite( &buf->pvt.fat.fh, buf->data + 2, static_cast<unsigned>( buf->lastused - 1 ), &byteswritten );
  if ( res != FResult::OK )
  {
    parseError( dos, res, 1 );
    dos.card.fClose( &buf->pvt.fat.fh );
    freeBuffer( dos, buf );
    return 1;
  }

  if ( byteswritten != buf->lastused - 1U )
  {
    setError( dos, ERROR_DISK_FULL );
    dos.card.fClose( &buf->pvt.fat.fh );
    freeBuffer( dos, buf );
    return 1;
  }

  markBufferClean( dos, buf );
  buf->mustflush = false;
  buf->position = 2;
  buf->lastused = 2;
  buf->fptr = buf->pvt.fat.fh.fptr - buf->pvt.fat.headersize;

  return 0;
}

/**
 * fat_file_write - refill-callback for files opened for writing
 * @buf: target buffer
 *
 * This function writes the contents of buf to the associated file.
 */
std::uint8_t fatFileWrite( Dos& dos, Buffer* buf )
{
  FResult res = FResult::OK;
  std::uint32_t fptr = 0;
  std::uint32_t i = 0;

  fptr = buf->pvt.fat.fh.fsize - buf->pvt.fat.headersize;

  // on a REL file, the fptr will be be at the end of the record we just read.  Reposition.
  if ( buf->fptr != fptr )
  {
    res = dos.card.fLseek( &buf->pvt.fat.fh, buf->pvt.fat.headersize + buf->fptr );
    if ( res != FResult::OK )
    {
      parseError( dos, res, 1 );
      dos.card.fClose( &buf->pvt.fat.fh );
      freeBuffer( dos, buf );
      return 1;
    }
  }

  if ( buf->fptr > fptr )
    i = buf->fptr - fptr;

  if ( res == FResult::OK && writeData( dos, buf ) != 0 )
    return 1;

  if ( i != 0 )
  {
    // we need to fill bytes.
    // position to old end of file.
    res = dos.card.fLseek( &buf->pvt.fat.fh, buf->pvt.fat.headersize + fptr );
    buf->mustflush = false;
    buf->fptr = fptr;
    buf->data[2] = ( buf->recordlen != 0 ? 255 : 0 );
    std::memset( buf->data + 3, 0, 253 );
    while ( res == FResult::OK && i != 0 )
    {
      if ( buf->recordlen != 0 )
        buf->lastused = buf->recordlen;
      else
        buf->lastused = ( i > 254 ? 254 : static_cast<std::uint8_t>( i ) );

      i -= buf->lastused;
      buf->position = static_cast<std::uint8_t>( buf->lastused + 2 );

      if ( writeData( dos, buf ) != 0 )
        return 1;
    }
    res = dos.card.fLseek( &buf->pvt.fat.fh, buf->pvt.fat.fh.fsize );
    if ( res != FResult::OK )
    {
      parseError( dos, res, 1 );
      dos.card.fClose( &buf->pvt.fat.fh );
      freeBuffer( dos, buf );
      return 1;
    }
    buf->fptr = buf->pvt.fat.fh.fptr - buf->pvt.fat.headersize;
  }

  return 0;
}

/**
 * fat_file_sync - synchronize the current REL file.
 * @buf: buffer to be worked on
 *
 */
std::uint8_t fatFileSync( Dos& dos, Buffer* buf )
{
  return fatFileSeek( dos, buf, buf->fptr + buf->recordlen, 0 );
}

/**
 * fat_file_close - close the file associated with a buffer
 * @buf: buffer to be worked on
 *
 * This function closes the file associated with the given buffer. If the buffer
 * was opened for writing the data contents will be stored if required.
 * Additionally the buffer will be marked as free.
 * Used as a cleanup-callback for reading and writing.
 */
std::uint8_t fatFileClose( Dos& dos, Buffer* buf )
{
  FResult res{};

  if ( !buf->allocated )
    return 0;

  /* Write the remaining data using the callback */
  if ( buf->write && buf->refill( dos, buf ) != 0 )
    return 1;

  res = dos.card.fClose( &buf->pvt.fat.fh );
  parseError( dos, res, 1 );
  buf->cleanup = callbackDummy;

  if ( res != FResult::OK )
    return 1;
  return 0;
}

/**
 * fat_getvolumename - Get the volume label
 * @part : partition to request
 * @label: pointer to the buffer for the label (16 characters+zero-termination)
 *
 * This function reads the FAT volume label and stores it zero-terminated
 * in label. Returns 0 if successfull, != 0 if an error occured.
 */
std::uint8_t fatGetvolumename( Dos& dos, std::uint8_t part, std::uint8_t* label )
{
  FatDir dh{};
  FatFileInfo finfo{};
  FResult res{};
  std::uint8_t i = 0;
  std::uint8_t j = 0;

  finfo.lfn = nullptr;
  std::memset( label, 0, CBM_NAME_LENGTH + 1 );

  res = dos.card.lOpendir( &partitionOf( dos, part ).fatfs, 0, &dh );

  if ( res != FResult::OK )
  {
    parseError( dos, res, 0 );
    return 1;
  }

  while ( ( res = dos.card.fReaddir( &dh, &finfo ) ) == FResult::OK )
  {
    if ( finfo.fname[0] == 0 )
      break;
    if ( ( finfo.fattrib & ( AM_VOL | AM_SYS | AM_HID ) ) == AM_VOL )
    {
      i = 0;
      j = 0;
      while ( finfo.fname.at( i ) != 0 )
      {
        /* Skip dots */
        if ( finfo.fname.at( i ) == '.' )
        {
          i++;
          continue;
        }
        label[j++] = finfo.fname.at( i++ );
      }
      return 0;
    }
  }
  return 0;
}

/**
 * fat_readwrite_sector - simulate direct sector access
 * @buf   : target buffer
 * @part  : partition number
 * @track : track to read
 * @sector: sector to read
 * @rwflag: read/write flag
 *
 * This function allows access to a file called bootsect.128
 * as track 1 sector 0 to enable the auto-boot function of
 * the C128 on FAT directories. If rwflag is false (0),
 * the sector will be written; otherwise it will be read.
 */
void fatReadwriteSector(
    Dos& dos, Buffer* buf, std::uint8_t part, std::uint8_t track, std::uint8_t sector, std::uint8_t rwflag )
{
  FResult res{};
  unsigned bytes = 0;
  std::uint8_t mode = 0;

  if ( track != 1 || sector != 0 )
  {
    setErrorTs( dos, ERROR_READ_NOHEADER, track, sector );
    return;
  }

  if ( rwflag != 0 )
    mode = FA_OPEN_EXISTING | FA_READ;
  else
    mode = FA_OPEN_ALWAYS | FA_WRITE;

  /* since this is fatops, the imagehandle is currently unused       */
  /* (saves a noticable chunk of stack compared to a local variable) */
  res = dos.card.fOpen( &partitionOf( dos, part ).fatfs,
                        &partitionOf( dos, part ).imagehandle,
                        reinterpret_cast<std::uint8_t const*>( BOOTSECTOR_FILE ),
                        mode );
  if ( res != FResult::OK )
  {
    parseError( dos, res, rwflag );
    return;
  }

  if ( rwflag != 0 )
    res = dos.card.fRead( &partitionOf( dos, part ).imagehandle, buf->data, 256, &bytes );
  else
    res = dos.card.fWrite( &partitionOf( dos, part ).imagehandle, buf->data, 256, &bytes );

  if ( res != FResult::OK || bytes != 256 )
    parseError( dos, res, rwflag );

  res = dos.card.fClose( &partitionOf( dos, part ).imagehandle );
  if ( res != FResult::OK )
    parseError( dos, res, rwflag );
}

/**
 * fat_rename - rename a file
 * @path   : path object
 * @dent   : pointer to cbmdirent with old file name
 * @newname: new file name
 *
 * This function renames the file in dent in the directory referenced by
 * path to newname.
 */
void fatRename( Dos& dos, Path* path, CbmDirent* dent, std::uint8_t* newname )
{
  std::uint8_t* ext = nullptr;
  FResult res{};
  unsigned byteswritten = 0;
  auto& partition = partitionOf( dos, path->part );
  auto* opsScratch = dos.buffers.opsScratch.data();

  partition.fatfs.currDir = path->dir.fat;

  if ( dent->opstype == OpsType::FAT_X00 )
  {
    /* [PSUR]00 rename, just change the internal file name */
    p00cacheInvalidate( dos );

    res = dos.card.fOpen(
        &partition.fatfs, &partition.imagehandle, dent->pvt.fat.realname.data(), FA_WRITE | FA_OPEN_EXISTING );
    if ( res != FResult::OK )
    {
      parseError( dos, res, 0 );
      return;
    }

    res = dos.card.fLseek( &partition.imagehandle, P00_CBMNAME_OFFSET );
    if ( res != FResult::OK )
    {
      parseError( dos, res, 0 );
      return;
    }

    /* Copy the new name into dent->name so we can overwrite all 16 bytes */
    std::memset( dent->name.data(), 0, CBM_NAME_LENGTH );
    ustrcpy( dent->name.data(), newname );

    res = dos.card.fWrite( &partition.imagehandle, dent->name.data(), CBM_NAME_LENGTH, &byteswritten );
    if ( res != FResult::OK || byteswritten != CBM_NAME_LENGTH )
    {
      parseError( dos, res, 0 );
      return;
    }

    res = dos.card.fClose( &partition.imagehandle );
    if ( res != FResult::OK )
    {
      parseError( dos, res, 0 );
      return;
    }
  }
  else
  {
    if ( checkExtension( dent->pvt.fat.realname.data(), &ext ) == ExtType::IS_TYPE )
    {
      /* Keep type extension */
      ustrcpy( opsScratch, newname );
      buildName( dos, opsScratch, dent->typeflags & TYPE_MASK );
      res = dos.card.fRename( &partition.fatfs, dent->pvt.fat.realname.data(), opsScratch );
      if ( res != FResult::OK )
        parseError( dos, res, 0 );
    }
    else
    {
      /* Normal rename */
      pet2asc( dent->name.data() );
      pet2asc( newname );
      res = dos.card.fRename( &partition.fatfs, dent->name.data(), newname );
      if ( res != FResult::OK )
        parseError( dos, res, 0 );
    }
  }
}

} // namespace

/**
 * parse_error - translates a ff FRESULT into a commodore error message
 * @res     : FRESULT to be translated
 * @readflag: Flags if it was a read operation
 *
 * This function sets the error channel according to the problem given in
 * res. readflag specifies if a READ ERROR or WRITE ERROR should be used
 * if the error is FR_RW_ERROR.
 */
void parseError( Dos& dos, FResult res, std::uint8_t readflag )
{
  auto const code = static_cast<std::uint8_t>( res );
  switch ( res )
  {
  case FResult::OK:
    setError( dos, ERROR_OK );
    break;

  case FResult::NO_FILE:
    setErrorTs( dos, ERROR_FILE_NOT_FOUND, code, 0 );
    break;

  case FResult::NO_PATH:
  case FResult::NOT_DIRECTORY:
    setErrorTs( dos, ERROR_FILE_NOT_FOUND_39, code, 0 );
    break;

  case FResult::INVALID_NAME:
    setErrorTs( dos, ERROR_SYNTAX_JOKER, code, 0 );
    break;

  case FResult::NOT_READY:
  case FResult::INVALID_DRIVE:
  case FResult::NOT_ENABLED:
  case FResult::NO_FILESYSTEM:
    setErrorTs( dos, ERROR_DRIVE_NOT_READY, code, 0 );
    break;

  case FResult::RW_ERROR:
    /* Just a random READ ERROR */
    if ( readflag != 0 )
      setErrorTs( dos, ERROR_READ_NOHEADER, code, 0 );
    else
      setErrorTs( dos, ERROR_WRITE_VERIFY, code, 0 );
    break;

  case FResult::WRITE_PROTECTED:
    setErrorTs( dos, ERROR_WRITE_PROTECT, code, 0 );
    break;

  // NODISKEMU's cases are kept apart, though some answer alike.
  // NOLINTNEXTLINE(bugprone-branch-clone)
  case FResult::EXIST:
    setErrorTs( dos, ERROR_FILE_EXISTS, code, 0 );
    break;

  case FResult::DIR_NOT_EMPTY:
    // FIXME: What do the CMD drives return when removing a non-empty directory?
    setErrorTs( dos, ERROR_FILE_EXISTS, code, 0 );
    break;

  case FResult::DENIED:
    setErrorTs( dos, ERROR_DISK_FULL, code, 0 );
    break;

  case FResult::IS_READONLY:
  case FResult::IS_DIRECTORY:
    setErrorTs( dos, ERROR_FILE_EXISTS, code, 0 );
    break;

  case FResult::INVALID_OBJECT:
    setErrorTs( dos, ERROR_DRIVE_NOT_READY, code, 0 );
    break;

  default:
    setErrorTs( dos, ERROR_SYNTAX_UNABLE, code, 99 );
    break;
  }
}

/**
 * check_imageext - check for a known image file extension
 * @name: pointer to the file name
 *
 * This function checks if the given file name has an extension that
 * indicates a known image file type. Returns IMG_IS_DISK for
 * D64/D41/D71/D81 files or IMG_UNKNOWN for an unknown file extension.
 */
ImgType checkImageext( std::uint8_t* name )
{
  std::uint8_t f = 0;
  std::uint8_t s = 0;
  std::uint8_t t = 0;
  std::uint8_t* ext = ustrrchr( name, '.' );

  if ( ext == nullptr )
    return ImgType::UNKNOWN;

  f = toUpper( *++ext );
  s = toUpper( *++ext );
  t = toUpper( *++ext );

  if ( f == 'D' && ( ( s == '6' && t == '4' ) ||               // D64
                     ( s == 'N' && t == 'P' ) ||               // DNP
                     ( ( s == '4' || s == '7' || s == '8' ) && // D41, D71, D81
                       ( t == '1' ) ) ||
                     ( s == '8' && ( t == '0' || t == '2' ) ) ) ) // D80, D82
    return ImgType::IS_DISK;

  return ImgType::UNKNOWN;
}

/**
 * pet2asc - convert string from PETSCII to ASCII
 * @buf: pointer to the string to be converted
 *
 * This function converts the string in the given buffer from PETSCII to
 * ASCII in-place.
 */
void pet2asc( std::uint8_t* buf )
{
  std::uint8_t ch = 0;
  while ( *buf != 0 )
  {
    ch = *buf;
    if ( ch > ( 128 + 64 ) && ch < ( 128 + 91 ) )
      ch -= 128;
    else if ( ch > ( 96 - 32 ) && ch < ( 123 - 32 ) )
      ch += 32;
    else if ( ch > ( 192 - 128 ) && ch < ( 219 - 128 ) )
      ch += 128;
    else if ( ch == 255 )
      ch = '~';
    *buf = ch;
    buf++;
  }
}

/**
 * fat_file_seek - callback for seek
 * @buf     : buffer to be worked on
 * @position: offset to seek to
 * @index   : offset within the record to seek to
 *
 * This function seeks to the offset position in the file associated
 * with the given buffer and sets the read pointer to the byte given
 * in index, effectively seeking to (position+index) for normal files.
 * Returns 1 if an error occured, 0 otherwise.
 */
std::uint8_t fatFileSeek( Dos& dos, Buffer* buf, std::uint32_t position, std::uint8_t index )
{
  std::uint32_t const pos = position + buf->pvt.fat.headersize;

  if ( buf->dirty && fatFileWrite( dos, buf ) != 0 )
    return 1;

  if ( buf->pvt.fat.fh.fsize >= pos )
  {
    FResult const res = dos.card.fLseek( &buf->pvt.fat.fh, pos );
    if ( res != FResult::OK )
    {
      parseError( dos, res, 0 );
      dos.card.fClose( &buf->pvt.fat.fh );
      freeBuffer( dos, buf );
      return 1;
    }

    if ( fatFileRead( dos, buf ) != 0 )
      return 1;
  }
  else
  {
    buf->data[2] = ( buf->recordlen != 0 ? 255 : 13 );
    buf->lastused = 2;
    buf->fptr = position;
    setError( dos, ERROR_RECORD_MISSING );
  }

  buf->position = static_cast<std::uint8_t>( index + 2 );
  if ( index + 2 > buf->lastused )
    buf->position = buf->lastused;

  return 0;
}

/* ------------------------------------------------------------------------- */
/*  Internal handlers for the various operations                             */
/* ------------------------------------------------------------------------- */

/**
 * fat_open_read - opens a file for reading
 * @path: path of the file
 * @dent: pointer to cbmdirent with name of the file
 * @buf : buffer to be used
 *
 * This functions opens a file in the FAT filesystem for reading and sets up
 * buf to access it.
 */
void fatOpenRead( Dos& dos, Path* path, CbmDirent* filename, Buffer* buf )
{
  CbmDirent* const dent = filename;
  FResult res{};
  std::uint8_t* name = nullptr;
  auto& partition = partitionOf( dos, path->part );

  pet2asc( dent->name.data() );
  if ( dent->pvt.fat.realname[0] != 0 )
    name = dent->pvt.fat.realname.data();
  else
    name = dent->name.data();

  partition.fatfs.currDir = path->dir.fat;
  res = dos.card.fOpen( &partition.fatfs, &buf->pvt.fat.fh, name, FA_READ | FA_OPEN_EXISTING );
  if ( res != FResult::OK )
  {
    parseError( dos, res, 1 );
    return;
  }

  if ( dent->opstype == OpsType::FAT_X00 )
  {
    /* It's a [PSUR]00 file, skip the header */
    /* If anything goes wrong here, refill will notice too */
    dos.card.fLseek( &buf->pvt.fat.fh, P00_HEADER_SIZE );
    buf->pvt.fat.headersize = P00_HEADER_SIZE;
  }

  buf->read = true;
  buf->cleanup = fatFileClose;
  buf->refill = fatFileRead;
  buf->seek = fatFileSeek;

  stickBuffer( buf );

  /* Call the refill once for the first block of data */
  buf->refill( dos, buf );
}

/**
 * create_file - creates a file
 * @path     : path of the file
 * @dent     : name of the file
 * @type     : type of the file
 * @buf      : buffer to be used
 * @recordlen: length of record, if REL file.
 *
 * This function opens a file in the FAT filesystem for writing and sets up
 * buf to access it. type is ignored here because FAT has no equivalent of
 * file types.
 */
FResult createFile( Dos& dos, Path* path, CbmDirent* dent, std::uint8_t type, Buffer* buf, std::uint8_t recordlen )
{
  FResult res{};
  std::uint8_t* name = nullptr;
  std::uint8_t* x00ext = nullptr;
  auto* opsScratch = dos.buffers.opsScratch.data();
  auto& partition = partitionOf( dos, path->part );

  x00ext = nullptr;

  ustrcpy( opsScratch, dent->name.data() );
  x00ext = buildName( dos, opsScratch, type );
  name = opsScratch;

  partition.fatfs.currDir = path->dir.fat;
  do
  {
    res = dos.card.fOpen(
        &partition.fatfs, &buf->pvt.fat.fh, name, FA_WRITE | FA_CREATE_NEW | ( recordlen != 0 ? FA_READ : 0 ) );
    if ( res == FResult::EXIST && x00ext != nullptr )
    {
      /* File exists, increment extension */
      *x00ext += 1;
      if ( *x00ext == '9' + 1 )
      {
        *x00ext = '0';
        *( x00ext - 1 ) += 1;
        if ( *( x00ext - 1 ) == '9' + 1 )
          break;
      }
    }
  } while ( res == FResult::EXIST );

  if ( res != FResult::OK )
    return res;

  if ( x00ext != nullptr || recordlen != 0 )
  {
    unsigned byteswritten = 0;

    if ( x00ext != nullptr )
    {
      /* Write a [PSUR]00 header */

      std::memset( opsScratch, 0, P00_HEADER_SIZE );
      ustrcpy( opsScratch, P00MARKER );
      std::memcpy( opsScratch + P00_CBMNAME_OFFSET, dent->name.data(), CBM_NAME_LENGTH );
      if ( recordlen != 0 )
        opsScratch[P00_RECORDLEN_OFFSET] = recordlen;
      buf->pvt.fat.headersize = P00_HEADER_SIZE;
    }
    else if ( recordlen != 0 )
    {
      opsScratch[0] = recordlen;
      buf->pvt.fat.headersize = 1;
    }
    res = dos.card.fWrite( &buf->pvt.fat.fh, opsScratch, buf->pvt.fat.headersize, &byteswritten );
    if ( res != FResult::OK || byteswritten != buf->pvt.fat.headersize )
    {
      return res;
    }
  }

  return FResult::OK;
}

/**
 * fat_open_write - opens a file for writing
 * @path  : path of the file
 * @dent  : name of the file
 * @type  : type of the file
 * @buf   : buffer to be used
 * @append: Flags if the new data should be appended to the end of file
 *
 * This function opens a file in the FAT filesystem for writing and sets up
 * buf to access it. type is ignored here because FAT has no equivalent of
 * file types.
 */
void fatOpenWrite( Dos& dos, Path* path, CbmDirent* filename, std::uint8_t type, Buffer* buf, std::uint8_t append )
{
  CbmDirent* const dent = filename;
  FResult res{};

  if ( append != 0 )
  {
    auto& partition = partitionOf( dos, path->part );
    partition.fatfs.currDir = path->dir.fat;
    res = dos.card.fOpen(
        &partition.fatfs, &buf->pvt.fat.fh, dent->pvt.fat.realname.data(), FA_WRITE | FA_OPEN_EXISTING );
    if ( dent->opstype == OpsType::FAT_X00 )
      /* It's a [PSUR]00 file */
      buf->pvt.fat.headersize = P00_HEADER_SIZE;
    if ( res == FResult::OK )
      res = dos.card.fLseek( &buf->pvt.fat.fh, buf->pvt.fat.fh.fsize );
    buf->fptr = buf->pvt.fat.fh.fsize - buf->pvt.fat.headersize;
  }
  else
  {
    res = createFile( dos, path, dent, type, buf, 0 );
  }

  if ( res != FResult::OK )
  {
    parseError( dos, res, 0 );
    return;
  }

  markWriteBuffer( buf );
  buf->position = 2;
  buf->lastused = 2;
  buf->cleanup = fatFileClose;
  buf->refill = fatFileWrite;
  buf->seek = fatFileSeek;

  /* If no data is written the file should end up with a single 0x0d byte */
  buf->data[2] = 13;
}

/**
 * fat_open_rel - creates a rel file.
 * @path  : path of the file
 * @dent  : name of the file
 * @buf   : buffer to be used
 * @length: record length
 * @mode  : select between new or existing file
 *
 * This function opens a rel file and prepares it for access.
 * If the mode parameter is 0, create a new file. If it is != 0,
 * open an existing file.
 */
void fatOpenRel( Dos& dos, Path* path, CbmDirent* dent, Buffer* buf, std::uint8_t length, std::uint8_t mode )
{
  FResult res{};
  unsigned bytesread = 0;
  auto* opsScratch = dos.buffers.opsScratch.data();

  if ( mode == 0 )
  {
    res = createFile( dos, path, dent, TYPE_REL, buf, length );
    bytesread = 1;
    opsScratch[0] = length;
  }
  else
  {
    auto& partition = partitionOf( dos, path->part );
    partition.fatfs.currDir = path->dir.fat;
    res = dos.card.fOpen(
        &partition.fatfs, &buf->pvt.fat.fh, dent->pvt.fat.realname.data(), FA_WRITE | FA_READ | FA_OPEN_EXISTING );
    if ( res == FResult::OK )
    {
      if ( dent->opstype == OpsType::FAT_X00 )
      {
        res = dos.card.fLseek( &buf->pvt.fat.fh, P00_RECORDLEN_OFFSET );
      }
      if ( res == FResult::OK )
        /* read record length */
        res = dos.card.fRead( &buf->pvt.fat.fh, opsScratch, 1, &bytesread );
      if ( length == 0 )
        length = opsScratch[0];
    }
  }

  if ( res != FResult::OK || bytesread != 1 )
  {
    parseError( dos, res, 0 );
    return;
  }

  buf->pvt.fat.headersize = static_cast<std::uint8_t>( buf->pvt.fat.fh.fptr );
  buf->recordlen = length;
  markWriteBuffer( buf );
  buf->read = true;
  buf->cleanup = fatFileClose;
  buf->refill = fatFileSync;
  buf->seek = fatFileSeek;

  /* read the first record */
  if ( fatFileRead( dos, buf ) == 0 && length != opsScratch[0] )
    setError( dos, ERROR_RECORD_MISSING );
}

/* ------------------------------------------------------------------------- */
/*  External interface for the various operations                            */
/* ------------------------------------------------------------------------- */

std::uint8_t fatOpendir( Dos& dos, Dh* dh, Path* dir )
{
  Path* const path = dir;
  FResult res{};

  res = dos.card.lOpendir( &partitionOf( dos, path->part ).fatfs, path->dir.fat, &dh->dir.fat );
  dh->part = path->part;
  if ( res != FResult::OK )
  {
    parseError( dos, res, 1 );
    return 1;
  }
  return 0;
}

/**
 * fat_readdir - readdir wrapper for FAT
 * @dh  : directory handle as set up by opendir
 * @dent: CBM directory entry for returning data
 *
 * This function reads the next directory entry into dent.
 * Returns 1 if an error occured, -1 if there are no more
 * directory entries and 0 if successful.
 */
std::int8_t fatReaddir( Dos& dos, Dh* dh, CbmDirent* dent )
{
  FResult res{};
  FatFileInfo finfo{};
  std::uint8_t* ptr = nullptr;
  std::uint8_t* nameptr = nullptr;
  std::uint8_t typechar = 0;
  auto* opsScratch = dos.buffers.opsScratch.data();

  finfo.lfn = opsScratch;

  do
  {
    res = dos.card.fReaddir( &dh->dir.fat, &finfo );
    if ( res != FResult::OK )
    {
      if ( res == FResult::INVALID_OBJECT )
        setError( dos, ERROR_DIR_ERROR );
      else
        parseError( dos, res, 1 );
      return 1;
    }
  } while ( ( finfo.fname[0] != 0 && ( finfo.fattrib & AM_VOL ) != 0 ) ||
            ( finfo.fname[0] == '.' && finfo.fname[1] == 0 ) ||
            ( finfo.fname[0] == '.' && finfo.fname[1] == '.' && finfo.fname[2] == 0 ) );

  *dent = CbmDirent{};

  if ( finfo.fname[0] == 0 )
    return -1;

  dent->opstype = OpsType::FAT;

  /* Copy name */
  ustrcpy( dent->pvt.fat.realname.data(), finfo.fname.data() );

  if ( finfo.lfn[0] == 0 || ustrlen( finfo.lfn ) > CBM_NAME_LENGTH + 4 )
  {
    nameptr = finfo.fname.data();
  }
  else
  {
    /* Convert only LFNs to PETSCII, 8.3 are always upper-case */
    nameptr = finfo.lfn;
    asc2pet( nameptr );
  }

  /* File type */
  if ( ( finfo.fattrib & AM_DIR ) != 0 )
  {
    dent->typeflags = TYPE_DIR;
    /* Hide directories starting with . */
    if ( *nameptr == '.' )
      dent->typeflags |= FLAG_HIDDEN;
  }
  else
  {
    /* Search for the file extension */
    ExtType const ext = checkExtension( finfo.fname.data(), &ptr );
    if ( ext == ExtType::IS_X00 )
    {
      /* [PSRU]00 file - try to read the internal name */
      std::uint8_t* name = p00cacheLookup( dos, dh->part, finfo.clust );
      typechar = *ptr;

      // NODISKEMU leaves this branch by goto notp00 when the header cannot
      // be read; isX00 stands for having reached its end.
      bool isX00 = true;
      if ( name != nullptr )
      {
        /* lookup successful */
        std::memcpy( dent->name.data(), name, CBM_NAME_LENGTH );
      }
      else
      {
        /* read name from file */
        unsigned bytesread = 0;
        auto& partition = partitionOf( dos, dh->part );

        res = dos.card.lOpencluster( &partition.fatfs, &partition.imagehandle, finfo.clust );
        if ( res == FResult::OK )
          res = dos.card.fRead( &partition.imagehandle, opsScratch, P00_HEADER_SIZE, &bytesread );

        if ( res != FResult::OK || std::memcmp( opsScratch, P00MARKER, P00MARKER_LENGTH ) != 0 )
        {
          isX00 = false; /* goto notp00 */
        }
        else
        {
          /* Copy the internal name - dent->name is still zeroed */
          ustrcpy( dent->name.data(), opsScratch + P00_CBMNAME_OFFSET );

          /* Some programs pad the name with 0xa0 instead of 0 */
          ptr = dent->name.data();
          for ( std::uint8_t i = 0; i < 16; i++, ptr++ )
            if ( *ptr == 0xa0 )
              *ptr = 0;

          /* add name to cache */
          p00cacheAdd( dos, dh->part, finfo.clust, dent->name.data() );
        }
      }
      if ( isX00 )
      {
        finfo.fsize -= P00_HEADER_SIZE;
        dent->opstype = OpsType::FAT_X00;
      }
    }
    else if ( ext == ExtType::IS_TYPE && ( dos.commands.globalflags & EXTENSION_HIDING ) != 0 )
    {
      /* Type extension */
      typechar = *ptr;
      auto const i = static_cast<std::uint8_t>( ustrlen( nameptr ) - 4 );
      nameptr[i] = 0;
    }
    else
    { /* ext == EXT_UNKNOWN or EXT_IS_TYPE but hiding disabled */
      /* Unknown extension: PRG */
      typechar = 'P';
    }

    /* notp00: */
    /* Set the file type */
    switch ( typechar )
    {
    case 'P':
      dent->typeflags = TYPE_PRG;
      break;

    case 'S':
      dent->typeflags = TYPE_SEQ;
      break;

    case 'U':
      dent->typeflags = TYPE_USR;
      break;

    case 'R':
      dent->typeflags = TYPE_REL;
      break;

    default:
      break;
    }
  }

  /* Copy file name into dirent if it fits */
  if ( dent->opstype != OpsType::FAT_X00 )
  {
    if ( ustrlen( nameptr ) > CBM_NAME_LENGTH )
    {
      ustrcpy( dent->name.data(), finfo.fname.data() );
    }
    else
    {
      ustrcpy( dent->name.data(), nameptr );
    }

    ptr = dent->name.data();
    while ( *ptr != 0 )
    {
      if ( *ptr == '~' )
        *ptr = 0xff;
      ptr++;
    }
  }

  if ( finfo.fsize > 16255746 )
    /* File too large -> size 63999 blocks */
    dent->blocksize = 63999;
  else
    dent->blocksize = static_cast<std::uint16_t>( ( finfo.fsize + 253 ) / 254 );

  dent->remainder = static_cast<std::uint8_t>( finfo.fsize % 254 );

  /* Read-Only and hidden flags */
  if ( ( finfo.fattrib & AM_RDO ) != 0 )
    dent->typeflags |= FLAG_RO;

  if ( ( finfo.fattrib & ( AM_HID | AM_SYS ) ) != 0 )
    dent->typeflags |= FLAG_HIDDEN;

  /* Cluster number */
  dent->pvt.fat.cluster = finfo.clust;

  /* Date/Time */
  dent->date.year = static_cast<std::uint8_t>( ( finfo.fdate >> 9U ) + 80 );
  dent->date.month = static_cast<std::uint8_t>( ( finfo.fdate >> 5U ) & 0x0fU );
  dent->date.day = static_cast<std::uint8_t>( finfo.fdate & 0x1fU );

  dent->date.hour = static_cast<std::uint8_t>( finfo.ftime >> 11U );
  dent->date.minute = static_cast<std::uint8_t>( ( finfo.ftime >> 5U ) & 0x3fU );
  dent->date.second = static_cast<std::uint8_t>( ( finfo.ftime & 0x1fU ) << 1U );

  return 0;
}

/**
 * fat_delete - Delete a file/directory on FAT
 * @path: path to the file/directory
 * @dent: pointer to cbmdirent with name of the file/directory to be deleted
 *
 * This function deletes the file filename in path and returns
 * 0 if not found, 1 if deleted or 255 if an error occured.
 */
std::uint8_t fatDelete( Dos& dos, Path* path, CbmDirent* dent )
{
  FResult res{};
  std::uint8_t* name = nullptr;
  auto& partition = partitionOf( dos, path->part );

  if ( dent->pvt.fat.realname[0] != 0 )
  {
    name = dent->pvt.fat.realname.data();
    p00cacheInvalidate( dos );
  }
  else
  {
    name = dent->name.data();
    pet2asc( name );
  }
  partition.fatfs.currDir = path->dir.fat;
  res = dos.card.fUnlink( &partition.fatfs, name );

  parseError( dos, res, 0 );
  if ( res == FResult::OK )
    return 1;
  if ( res == FResult::NO_FILE )
    return 0;
  return 255;
}

/**
 * fat_chdir - change directory in FAT and/or mount image
 * @path: path object for the location of dirname
 * @dent: Name of the directory/image to be changed into
 *
 * This function changes the directory of the path object to dirname.
 * If dirname specifies a file with a known extension (e.g. D64), the
 * current(!) directory will be changed to the directory of the file and
 * it will be mounted as an image file. Returns 0 if successful,
 * 1 otherwise.
 */
std::uint8_t fatChdir( Dos& dos, Path* path, CbmDirent* dent )
{
  FResult res{};
  auto& partition = partitionOf( dos, path->part );
  auto* opsScratch = dos.buffers.opsScratch.data();

  partition.fatfs.currDir = path->dir.fat;

  /* Left arrow moves one directory up */
  if ( dent->name[0] == '_' && dent->name[1] == 0 )
  {
    FatFileInfo finfo{};

    opsScratch[0] = '.';
    opsScratch[1] = '.';
    opsScratch[2] = 0;

    res = dos.card.fStat( &partition.fatfs, opsScratch, &finfo );
    if ( res != FResult::OK )
    {
      parseError( dos, res, 1 );
      return 1;
    }

    dent->pvt.fat.cluster = finfo.clust;
    dent->typeflags = TYPE_DIR;
  }
  else if ( dent->name[0] == 0 )
  {
    /* Empty string moves to the root dir */
    path->dir.fat = 0;
    return 0;
  }

  if ( ( dent->typeflags & TYPE_MASK ) == TYPE_DIR )
  {
    /* It's a directory, change to its cluster */
    path->dir.fat = dent->pvt.fat.cluster;
  }
  else
  {
    /* Changing into a file, could be a mount request */
    if ( checkImageext( dent->pvt.fat.realname.data() ) != ImgType::UNKNOWN )
    {
      /* D64 mount request */
      freeMultipleBuffers( dos, FMB_USER_CLEAN );
      /* Open image file */
      res = dos.card.fOpen( &partition.fatfs,
                            &partition.imagehandle,
                            dent->pvt.fat.realname.data(),
                            FA_OPEN_EXISTING | FA_READ | FA_WRITE );

      /* Try to open read-only if medium or file is read-only */
      if ( res == FResult::DENIED || res == FResult::WRITE_PROTECTED )
        res = dos.card.fOpen(
            &partition.fatfs, &partition.imagehandle, dent->pvt.fat.realname.data(), FA_OPEN_EXISTING | FA_READ );

      if ( res != FResult::OK )
      {
        parseError( dos, res, 1 );
        return 1;
      }

      if ( d64Mount( dos, path, dent->pvt.fat.realname.data() ) != 0 )
        return 1;
      partition.fop = &D64OPS;

      return 0;
    }
  }
  return 0;
}

/* Create a new directory */
void fatMkdir( Dos& dos, Path* path, std::uint8_t* dirname )
{
  FResult res{};
  auto& partition = partitionOf( dos, path->part );

  partition.fatfs.currDir = path->dir.fat;
  pet2asc( dirname );
  res = dos.card.fMkdir( &partition.fatfs, dirname );
  parseError( dos, res, 0 );
}

/**
 * fat_getdirlabel - Get the directory label
 * @path : path object of the directory
 * @label: pointer to the buffer for the label (16 characters)
 *
 * This function reads the FAT volume label (if in root directory) or FAT
 * directory name (if not) and stored it space-padded
 * in the first 16 bytes of label.
 * Returns 0 if successfull, != 0 if an error occured.
 */
std::uint8_t fatGetdirlabel( Dos& dos, Path* path, std::uint8_t* label )
{
  FatDir dh{};
  FatFileInfo finfo{};
  FResult res{};
  auto* opsScratch = dos.buffers.opsScratch.data();
  std::uint8_t* name = opsScratch;
  auto& partition = partitionOf( dos, path->part );

  finfo.lfn = opsScratch;
  std::memset( label, ' ', CBM_NAME_LENGTH );

  res = dos.card.lOpendir( &partition.fatfs, path->dir.fat, &dh );
  if ( res == FResult::OK ) /* else goto gl_error */
  {
    while ( ( res = dos.card.fReaddir( &dh, &finfo ) ) == FResult::OK )
    {
      if ( finfo.fname[0] == '\0' || finfo.fname[0] != '.' )
      {
        // NODISKEMU stores fat_getvolumename's 0 or 1 in the FRESULT.
        res = static_cast<FResult>( fatGetvolumename( dos, path->part, name ) );
        break;
      }
      if ( finfo.fname[0] == '.' && finfo.fname[1] == '.' && finfo.fname[2] == 0 )
      {
        res = dos.card.lOpendir( &partition.fatfs, finfo.clust, &dh ); // open .. dir.
        if ( res != FResult::OK )
          break;
        while ( ( res = dos.card.fReaddir( &dh, &finfo ) ) == FResult::OK )
        {
          if ( finfo.fname[0] == '\0' )
            break;
          if ( finfo.clust == path->dir.fat )
          {
            if ( *name == 0 )
              name = finfo.fname.data();
            else
              asc2pet( name );
            break;
          }
        }
        break;
      }
    }

    if ( *name != 0 )
      std::memcpy( label, name, ustrlen( name ) );

    if ( res == FResult::OK )
      return 0;
  }

  /* gl_error: */
  parseError( dos, res, 0 );
  return 1;
}

/**
 * fat_getid - "Read" a disk id
 * @path: path object
 * @id  : pointer to the buffer for the id (5 characters)
 *
 * This function creates a disk ID from the FAT type (12/16/32)
 * and the usual " 2A" of a 1541 in the first 5 bytes of id.
 * Always returns 0 for success.
 */
std::uint8_t fatGetid( Dos& dos, Path* path, std::uint8_t* id )
{
  switch ( partitionOf( dos, path->part ).fatfs.fsType )
  {
  case FS_FAT12:
    *id++ = '1';
    *id++ = '2';
    break;

  case FS_FAT16:
    *id++ = '1';
    *id++ = '6';
    break;

  case FS_FAT32:
    *id++ = '3';
    *id++ = '2';
    break;

  default:
    break;
  }

  *id++ = ' ';
  *id++ = '2';
  *id++ = 'A';
  return 0;
}

/* Returns the number of free blocks */
std::uint16_t fatFreeblocks( Dos& dos, std::uint8_t part )
{
  FatVolume* fs = &partitionOf( dos, part ).fatfs;
  std::uint32_t clusters = 0;

  if ( dos.card.lGetfree( fs, NULLSTRING, &clusters, 65535 ) == FResult::OK )
  {
    if ( clusters > 65535 )
      return 65535;
    return static_cast<std::uint16_t>( clusters );
  }
  return 0;
}

/**
 * fat_read_sector - simulate direct sector reads
 * @buf   : target buffer
 * @part  : partition number
 * @track : track to read
 * @sector: sector to read
 *
 * Wrapper for fat_readwrite_sector in read mode
 */
void fatReadSector( Dos& dos, Buffer* buf, std::uint8_t part, std::uint8_t track, std::uint8_t sector )
{
  fatReadwriteSector( dos, buf, part, track, sector, 1 );
}

/**
 * fat_write_sector - simulate direct sector writes
 * @buf   : source buffer
 * @part  : partition number
 * @track : track to write
 * @sector: sector to write
 *
 * Wrapper for fat_readwrite_sector in write mode
 */
void fatWriteSector( Dos& dos, Buffer* buf, std::uint8_t part, std::uint8_t track, std::uint8_t sector )
{
  fatReadwriteSector( dos, buf, part, track, sector, 0 );
}

/**
 * fatops_init - Initialize fatops module
 * @preserve_path: Preserve the current directory if non-zero
 *
 * This function will initialize the fatops module and force
 * mounting of the card. It can safely be called again if re-mounting
 * is required.
 */
void fatopsInit( Dos& dos, std::uint8_t preserveDir )
{
  FResult res{};

  // NODISKEMU tries every partition of every drive here; the card is the one
  // partition HostFs mounts.
  dos.parser.maxPart = 0;
  while ( dos.parser.maxPart < MAX_PARTITIONS )
  {
    auto& partition = partitionOf( dos, dos.parser.maxPart );
    partition.fop = &FATOPS;

    res = dos.card.fMount( &partition.fatfs );

    if ( preserveDir == 0 )
      partition.currentDir.fat = 0;

    if ( res == FResult::OK )
      dos.parser.maxPart++;
    else
      break;
  }

  if ( preserveDir == 0 )
  {
    dos.parser.currentPart = 0;
    // FIXME: set_changelist(NULL, NULLSTRING);
    dos.files.previousFileDirent.name[0] = 0; // clear '*' file
  }

  /* Invalidate some caches */
  d64Invalidate( dos );
  p00cacheInvalidate( dos );

  /* #ifndef HAVE_HOTPLUG */
  if ( dos.parser.maxPart == 0 )
  {
    setErrorTs( dos, ERROR_DRIVE_NOT_READY, 0, 0 );
    return;
  }
}

/**
 * image_unmount - generic unmounting function for images
 * @part: partition number
 *
 * This function will clear all buffers, close the image file and
 * restore file operations to fatops. It can be used for unmounting
 * any image file types that don't require special cleanups.
 * Returns 0 if successful, 1 otherwise.
 */
std::uint8_t imageUnmount( Dos& dos, std::uint8_t part )
{
  FResult res{};
  auto& partition = partitionOf( dos, part );

  freeMultipleBuffers( dos, FMB_USER_CLEAN );

  /* call D64 unmount function to handle BAM refcounting etc. */
  // FIXME: ops entry?
  if ( partition.fop == &D64OPS )
    d64Unmount( dos, part );

  partition.fop = &FATOPS;
  res = dos.card.fClose( &partition.imagehandle );
  if ( res != FResult::OK )
  {
    parseError( dos, res, 0 );
    return 1;
  }
  return 0;
}

/**
 * image_chdir - generic chdir for image files
 * @path: path object of the location of dirname
 * @dent: directory to be changed into
 *
 * This function will ignore any names except _ (left arrow)
 * and unmount the image if that is found. It can be used as
 * chdir for all image types that don't support subdirectories
 * themselves. Returns 0 if successful, 1 otherwise.
 */
std::uint8_t imageChdir( Dos& dos, Path* path, CbmDirent* dent )
{
  if ( dent->name[0] == '_' && dent->name[1] == 0 )
  {
    /* Unmount request */
    return imageUnmount( dos, path->part );
  }
  return 1;
}

/**
 * image_mkdir - generic mkdir for image files
 * @path   : path of the directory
 * @dirname: name of the directory to be created
 *
 * This function does nothing.
 */
void imageMkdir( Dos& dos, Path* /*path*/, std::uint8_t* /*dirname*/ )
{
  setError( dos, ERROR_SYNTAX_UNABLE );
}

/**
 * image_read - Seek to a specified image offset and read data
 * @part  : partition number
 * @offset: offset to be seeked to
 * @buffer: pointer to where the data should be read to
 * @bytes : number of bytes to read from the image file
 *
 * This function seeks to offset in the image file and reads bytes
 * byte into buffer. It returns 0 on success, 1 if less than
 * bytes byte could be read and 2 on failure.
 */
std::uint8_t imageRead( Dos& dos, std::uint8_t part, std::uint32_t offset, void* buffer, std::uint16_t bytes )
{
  FResult res{};
  unsigned bytesread = 0;
  auto& partition = partitionOf( dos, part );

  if ( offset != 0xFFFFFFFFU ) /* offset != -1 */
  {
    res = dos.card.fLseek( &partition.imagehandle, offset );
    if ( res != FResult::OK )
    {
      parseError( dos, res, 1 );
      return 2;
    }
  }

  res = dos.card.fRead( &partition.imagehandle, buffer, bytes, &bytesread );
  if ( res != FResult::OK )
  {
    parseError( dos, res, 1 );
    return 2;
  }

  if ( bytesread != bytes )
    return 1;

  return 0;
}

/**
 * image_write - Seek to a specified image offset and write data
 * @part  : partition number
 * @offset: offset to be seeked to
 * @buffer: pointer to the data to be written
 * @bytes : number of bytes to read from the image file
 * @flush : Flags if written data should be flushed to disk immediately
 *
 * This function seeks to offset in the image file and writes bytes
 * byte into buffer. It returns 0 on success, 1 if less than
 * bytes byte could be written and 2 on failure.
 */
std::uint8_t
imageWrite( Dos& dos, std::uint8_t part, std::uint32_t offset, void* buffer, std::uint16_t bytes, std::uint8_t flush )
{
  FResult res{};
  unsigned byteswritten = 0;
  auto& partition = partitionOf( dos, part );

  if ( offset != 0xFFFFFFFFU ) /* offset != -1 */
  {
    res = dos.card.fLseek( &partition.imagehandle, offset );
    if ( res != FResult::OK )
    {
      parseError( dos, res, 0 );
      return 2;
    }
  }

  res = dos.card.fWrite( &partition.imagehandle, buffer, bytes, &byteswritten );
  if ( res != FResult::OK )
  {
    parseError( dos, res, 1 );
    return 2;
  }

  if ( byteswritten != bytes )
    return 1;

  if ( flush != 0 )
    dos.card.fSync( &partition.imagehandle );

  return 0;
}

/* Dummy function for format */
void formatDummy( Dos& dos, std::uint8_t /*drive*/, std::uint8_t* /*name*/, std::uint8_t* /*id*/ )
{
  setError( dos, ERROR_SYNTAX_UNKNOWN );
}

std::uint8_t fatMountHostImage( Dos& dos, std::filesystem::path const& image )
{
  // As a command does, from a clear error channel; then as do_chdir does,
  // with the image's directory as the path and its entry as the name matched.
  setError( dos, ERROR_OK );

  // An image mounted already is left first, as CD← would.
  if ( partitionOf( dos, 0 ).fop != &FATOPS && imageUnmount( dos, 0 ) != 0 )
    return 1;

  Path path{};
  path.part = 0;
  path.dir = partitionOf( dos, 0 ).currentDir;
  path.dir.fat = dos.card.clusterOf( image.parent_path() );

  /* clear '*' file */
  dos.files.previousFileDirent.name[0] = 0;

  auto const realname = dos.card.shortNameOf( image );
  Dh dh{};
  if ( fatOpendir( dos, &dh, &path ) != 0 )
    return 1;
  CbmDirent dent{};
  for ( ;; )
  {
    auto const res = fatReaddir( dos, &dh, &dent );
    if ( res < 0 )
    {
      setError( dos, ERROR_FILE_NOT_FOUND );
      return 1;
    }
    if ( res > 0 )
      return 1;
    if ( !realname.empty() && ustrcmp( dent.pvt.fat.realname.data(), realname.c_str() ) == 0 )
      break;
  }

  if ( fatChdir( dos, &path, &dent ) != 0 )
    return 1;

  updateCurrentDir( dos, &path );
  return 0;
}

FileOps const FATOPS = {
  // These should be at bottom, to be consistent with d64ops
  .openRead = &fatOpenRead,
  .openWrite = &fatOpenWrite,
  .openRel = &fatOpenRel,
  .fileDelete = &fatDelete,
  .diskLabel = &fatGetvolumename,
  .dirLabel = &fatGetdirlabel,
  .diskId = &fatGetid,
  .diskFree = &fatFreeblocks,
  .readSector = &fatReadSector,
  .writeSector = &fatWriteSector,
  .format = &formatDummy,
  .opendir = &fatOpendir,
  .readdir = &fatReaddir,
  .mkdir = &fatMkdir,
  .chdir = &fatChdir,
  .rename = &fatRename,
};

} // namespace pet::sd2pet
