// Copyright (C) 2007-2018 Ingo Korb <ingo@akana.de>, as NODISKEMU (a fork of sd2iec)
// carries it. GPL-2.0. Ported to C++ for PetMcp, 2026 (THIRD_PARTY.md).

// Ports NODISKEMU's src/fileops.c at commit e51a6df: the OPEN of a name, the
// directory listings ($, $=T, $=P and the raw one of a secondary other than
// 0), the # buffers and the * previous file. Left out: the UART trace and the
// display's file names, and the EEPROM file system's drive `!` in a listing's
// name (CONFIG_HAVE_EEPROMFS is off; MAX_PARTITIONS is 1, so the partition
// directory lists the card alone). The bus is IEEE-488, so the short listing
// takes the #else branch of CONFIG_HAVE_IEC: 40 columns for the PET.
//
// fileops.c's globals and statics are FileOpsState's.

#include "sd2pet/FileOps.hpp"

#include "sd2pet/Dos.hpp"
#include "sd2pet/Utils.hpp"

#include <cstddef>
#include <cstring>

namespace pet::sd2pet
{

/* TODO: The output of $=t depends on the available busses:
   - CMD compatible for devices with IEC bus
   - 40 column friendly for CBM/PET
   What should be done if a device offers both?
*/

std::array<std::uint8_t, 36> const FILETYPES = {
  'D', 'E', 'L', // 0
  'S', 'E', 'Q', // 1
  'P', 'R', 'G', // 2
  'U', 'S', 'R', // 3
  'R', 'E', 'L', // 4
  'C', 'B', 'M', // 5
  'D', 'I', 'R', // 6
  '?', '?', '?', // 7
  'N', 'A', 'T', // 8
  '4', '1', ' ', // 9
  '7', '1', ' ', // 10
  '8', '1', ' ', // 11
};

namespace
{

/* ------------------------------------------------------------------------- */
/*  Some constants used for directory generation                             */
/* ------------------------------------------------------------------------- */

constexpr std::uint8_t HEADER_OFFSET_DRIVE = 4;
constexpr std::uint8_t HEADER_OFFSET_NAME = 8;
constexpr std::uint8_t HEADER_OFFSET_ID = 26;

/* offsets within a D64 BAM sector for raw directory emulation */
constexpr std::uint8_t BAM_OFFSET_NAME = 0x90;
constexpr std::uint8_t BAM_OFFSET_ID = 0xa2;
constexpr std::uint8_t BAM_A0_AREA_SIZE = 0xaa - 0x90 + 1;

/* NOTE: I wonder if RLE-packing would save space in flash? */
constexpr std::array<std::uint8_t, 32> DIRHEADER = {
  1,    4,                                  /* BASIC start address */
  1,    1,                                  /* next line pointer */
  0,    0,                                  /* line number 0 */
  0x12, 0x22,                               /* Reverse on, quote */
  'N',  'O',  'D', 'I', 'S', 'K', 'E', 'M', /* 16 spaces as the disk name */
  'U',  ' ',  ' ', ' ', ' ', ' ', ' ', ' ', /* will be overwritten if needed */
  0x22, 0x20,                               /* quote, space */
  'D',  'S',  ' ', '2', 'A',                /* id DS, shift-space, dosmarker 2A */
  00                                        /* line-end marker */
};

constexpr std::array<std::uint8_t, 32> SYSPART_LINE = {
  1,    1,                                                 /* next line pointer */
  0,    0,                                                 /* number of free blocks (to be filled later) */
  ' ',  ' ',  ' ',                                         //
  '"',  'S',  'Y', 'S', 'T', 'E', 'M', '"',                //
  ' ',  ' ',  ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', //
  'S',  'Y',  'S',                                         //
  0x20, 0x20, 0x00                                         /* Filler and end marker */
};

constexpr std::array<std::uint8_t, 32> DIRFOOTER = {
  1,    1,                                                            /* next line pointer */
  0,    0,                                                            /* number of free blocks (to be filled later */
  'B',  'L',  'O',  'C',  'K',  'S',  ' ',  'F',  'R', 'E', 'E', '.', //
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,                     /* Filler and end marker */
  0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00                      //
};

/* ------------------------------------------------------------------------- */
/*  Utility functions                                                        */
/* ------------------------------------------------------------------------- */

/// filetypes + TYPE_LENGTH * type. NODISKEMU reads past the end of its table
/// for a type of 12 to 15 (a D80 in the partition directory is 15, and a Dxx
/// type byte with bit 3 set is another), into whatever the linker put after it
/// in the AVR's flash; that is not known, so those show as the table's "???".
std::uint8_t const* filetypeLetters( std::uint8_t type )
{
  std::size_t offset = std::size_t{ TYPE_LENGTH } * type;
  if ( offset >= FILETYPES.size() )
    offset = std::size_t{ TYPE_LENGTH } * TYPE_MASK;
  return FILETYPES.data() + offset;
}

/**
 * createentry - create a single directory entry in buf
 * @dent  : directory entry to be added
 * @buf   : buffer to be used
 * @format: entry format
 *
 * This function creates a directory entry for dent in the selected format
 * in the given buffer.
 */
void createentry( CbmDirent* dent, Buffer* buf, DirFormat format )
{
  std::uint8_t i = 0;
  std::uint8_t* data = buf->data;

  if ( format == DirFormat::CMD_LONG )
    i = 63;
  else if ( format == DirFormat::CMD_SHORT )
    i = 41;
  else
    i = 31;

  buf->lastused = i;
  /* Clear the line */
  std::memset( data, ' ', i );
  /* Line end marker */
  data[i] = 0;

  /* Next line pointer, 1571-compatible =) */
  if ( dent->remainder != 0xff )
    /* store remainder in low byte of link pointer          */
    /* +2 so it is never 0 (end-marker) or 1 (normal value) */
    *data++ = static_cast<std::uint8_t>( dent->remainder + 2 );
  else
    *data++ = 1;
  *data++ = 1;

  *data++ = static_cast<std::uint8_t>( dent->blocksize & 0xffU );
  *data++ = static_cast<std::uint8_t>( dent->blocksize >> 8U );

  /* Filler before file name */
  if ( dent->blocksize < 1000 )
    data++;
  if ( dent->blocksize < 100 )
    data++;
  if ( dent->blocksize < 10 )
    data++;
  *data++ = '"';

  /* copy and adjust the filename - C783 */
  std::memcpy( data, dent->name.data(), CBM_NAME_LENGTH );
  for ( i = 0; i <= CBM_NAME_LENGTH; i++ )
    if ( dent->name[i] == 0x22 || dent->name[i] == 0 || i == 16 )
    {
      data[i] = '"';
      while ( i <= CBM_NAME_LENGTH )
      {
        if ( data[i] == 0 )
          data[i] = ' ';
        else
          data[i] &= 0x7fU;
        i++;
      }
    }

  /* Skip name and final quote */
  data += CBM_NAME_LENGTH + 1;

  if ( ( dent->typeflags & FLAG_SPLAT ) != 0 )
    *data = '*';

  /* File type */
  std::memcpy( data + 1,
               filetypeLetters( dent->typeflags & EXT_TYPE_MASK ),
               ( static_cast<std::uint8_t>( format ) & static_cast<std::uint8_t>( DirFormat::CMD_SHORT ) ) != 0
                   ? 1
                   : TYPE_LENGTH );

  /* RO marker */
  if ( ( dent->typeflags & FLAG_RO ) != 0 )
    data[4] = '<';

  if ( ( static_cast<std::uint8_t>( format ) & static_cast<std::uint8_t>( DirFormat::CMD_LONG ) ) != 0 )
  {
    data += 7;
    data = appendnumber( data, dent->date.month );
    *data++ = '/';
    data = appendnumber( data, dent->date.day );
    *data++ = '/';
    data = appendnumber( data, dent->date.year % 100 ) + 3;
    data = appendnumber(
        data, ( dent->date.hour > 12 ? static_cast<std::uint8_t>( dent->date.hour - 12 ) : dent->date.hour ) );
    *data++ = '.';
    data = appendnumber( data, dent->date.minute ) + 1;
    *data++ = ( dent->date.hour > 11 ? 'P' : 'A' );
    *data++ = 'M';
    while ( *data != 0 )
      *data++ = 1;
  }
  else if ( format == DirFormat::CMD_SHORT )
  {
    /* Add date/time stamp */
    data += 3;
    data = appendnumber( data, dent->date.month );
    *data++ = '/';
    data = appendnumber( data, dent->date.day ) + 1;
    data = appendnumber(
        data, ( dent->date.hour > 12 ? static_cast<std::uint8_t>( dent->date.hour - 12 ) : dent->date.hour ) );
    *data++ = '.';
    // 40 columns friendly for CBM/PET (CONFIG_HAVE_IEC's CMD compatible
    // layout leaves a space here)
    data = appendnumber( data, dent->date.minute );
    *data++ = ( dent->date.hour > 11 ? 'P' : 'A' );
    while ( *data != 0 )
      *data++ = 1;
  }
  else
  {
    /* Extension: Hidden marker */
    if ( ( dent->typeflags & FLAG_HIDDEN ) != 0 )
      data[5] = 'H';
  }
}

/* ------------------------------------------------------------------------- */
/*  Callbacks                                                                */
/* ------------------------------------------------------------------------- */

/**
 * dir_footer - generate the directory footer
 * @buf: buffer to be used
 *
 * This is the final callback used during directory generation. It generates
 * the "BLOCKS FREE" message and indicates that this is the final buffer to
 * be sent. Always returns 0 for success.
 */
std::uint8_t dirFooter( Dos& dos, Buffer* buf )
{
  std::uint16_t blocks = 0;

  /* Copy the "BLOCKS FREE" message */
  std::memcpy( buf->data, DIRFOOTER.data(), DIRFOOTER.size() );

  blocks = diskFree( dos, buf->pvt.dir.dh.part );
  buf->data[2] = static_cast<std::uint8_t>( blocks & 0xffU );
  buf->data[3] = static_cast<std::uint8_t>( blocks >> 8U );

  buf->position = 0;
  buf->lastused = 31;
  buf->sendeoi = true;

  return 0;
}

/* Callback for the partition directory */
std::uint8_t pdirRefill( Dos& dos, Buffer* buf )
{
  // NODISKEMU leaves dent uninitialised, and createentry reads its remainder
  // for the line's link: what the AVR's stack held. Here it is zero, so the
  // link is 2.
  CbmDirent dent{};

  buf->position = 0;

  /* read volume name */
  while ( buf->pvt.pdir.part < dos.parser.maxPart )
  {
    if ( diskLabel( dos, buf->pvt.pdir.part, dent.name.data() ) != 0 )
    {
      freeBuffer( dos, buf );
      return 1;
    }

    dent.blocksize = static_cast<std::uint16_t>( buf->pvt.pdir.part + 1 );

    if ( dos.parser.partition.at( buf->pvt.pdir.part ).fop == &D64OPS )
    {
      /* Use the correct partition type for Dxx images */
      dent.typeflags = static_cast<std::uint8_t>(
          ( dos.parser.partition.at( buf->pvt.pdir.part ).imagetype & D64_TYPE_MASK ) + TYPE_NAT - 1 );
    }
    else
    {
      /* Anything else is "native" */
      dent.typeflags = TYPE_NAT;
    }

    buf->pvt.pdir.part++;

    /* Parse the name pattern */
    if ( buf->pvt.pdir.matchstr != nullptr && matchName( dos, buf->pvt.pdir.matchstr, &dent, 0 ) == 0 )
      continue;

    createentry( &dent, buf, DirFormat::CBM );
    return 0;
  }
  buf->lastused = 1;
  buf->sendeoi = true;
  std::memset( buf->data, 0, 2 );
  return 0;
}

/**
 * dir_refill - generate the next directory entry
 * @buf: buffer to be used
 *
 * This function generates a single directory entry with the next matching
 * file. If there is no more matching file the footer will be generated
 * instead. Used as a callback during directory generation.
 */
std::uint8_t dirRefill( Dos& dos, Buffer* buf )
{
  CbmDirent dent{};

  buf->position = 0;

  if ( buf->pvt.dir.counter != 0 )
  {
    /* Redisplay image file as directory */
    buf->pvt.dir.counter = 0;
    std::memcpy( &dent, buf->data + 256 - sizeof( dent ), sizeof( dent ) );
    dent.typeflags = TYPE_DIR;
    createentry( &dent, buf, buf->pvt.dir.format );
    return 0;
  }

  switch ( nextMatch( dos,
                      &buf->pvt.dir.dh,
                      buf->pvt.dir.matchstr,
                      buf->pvt.dir.matchStart,
                      buf->pvt.dir.matchEnd,
                      buf->pvt.dir.filetype,
                      &dent ) )
  {
  case 0:
    if ( dos.files.imageAsDir != IMAGE_DIR_NORMAL && dent.opstype == OpsType::FAT &&
         checkImageext( dent.pvt.fat.realname.data() ) != ImgType::UNKNOWN )
    {
      if ( dos.files.imageAsDir == IMAGE_DIR_DIR )
      {
        dent.typeflags = static_cast<std::uint8_t>( ( dent.typeflags & 0xf0U ) | TYPE_DIR );
      }
      else
      {
        /* Prepare to redisplay image file as directory */
        buf->pvt.dir.counter = 1;
        /* Use the end of the buffer as temporary storage */
        std::memcpy( buf->data + 256 - sizeof( dent ), &dent, sizeof( dent ) );
      }
    }
    createentry( &dent, buf, buf->pvt.dir.format );
    return 0;

  case -1:
    return dirFooter( dos, buf );

  default:
    freeBuffer( dos, buf );
    return 1;
  }
}

/**
 * rawdir_dummy_refill - generate raw dummy directory entries
 * @buf: buffer to be used
 *
 * This function generates an empty raw directory entry which is
 * used to pad a raw directory to its correct size.
 */
std::uint8_t rawdirDummyRefill( Dos& /*dos*/, Buffer* buf )
{
  if ( buf->pvt.dir.counter++ != 0 )
    buf->position = 0;
  else
    buf->position = 2;

  if ( buf->pvt.dir.counter == 8 )
    buf->sendeoi = true;

  return 0;
}

/**
 * rawdir_refill - generate the next raw directory entry
 * @buf: buffer to be used
 *
 * This function generates a single raw directory entry for the next file.
 * Used as a callback during directory generation.
 */
std::uint8_t rawdirRefill( Dos& dos, Buffer* buf )
{
  CbmDirent dent{};

  std::memset( buf->data, 0, 32 );

  if ( ( buf->pvt.dir.counter & 0x80U ) == 0 )
  {
    switch ( readdir( dos, &buf->pvt.dir.dh, &dent ) )
    {
    case -1:
      /* last entry, switch to dummy entries */
      return rawdirDummyRefill( dos, buf );

    default:
      /* error in readdir */
      freeBuffer( dos, buf );
      return 1;

    case 0:
      /* entry found, creation below */
      break;
    }

    if ( dos.files.imageAsDir != IMAGE_DIR_NORMAL && dent.opstype == OpsType::FAT &&
         checkImageext( dent.pvt.fat.realname.data() ) != ImgType::UNKNOWN )
    {
      if ( dos.files.imageAsDir == IMAGE_DIR_DIR )
      {
        dent.typeflags = static_cast<std::uint8_t>( ( dent.typeflags & 0xf0U ) | TYPE_DIR );
      }
      else
      {
        /* Prepare to redisplay image file as directory */
        buf->pvt.dir.counter |= 0x80U;
        std::memcpy( buf->data + 256 - sizeof( dent ), &dent, sizeof( dent ) );
      }
    }
  }
  else
  {
    /* Redisplay image file as directory */
    buf->pvt.dir.counter &= 0x7fU;
    std::memcpy( &dent, buf->data + 256 - sizeof( dent ), sizeof( dent ) );
    dent.typeflags = TYPE_DIR;
  }

  buf->data[DIR_OFS_TRACK] = 1;
  buf->data[DIR_OFS_SIZE_LOW] = static_cast<std::uint8_t>( dent.blocksize & 0xffU );
  buf->data[DIR_OFS_SIZE_HI] = static_cast<std::uint8_t>( dent.blocksize >> 8U );
  buf->data[DIR_OFS_FILE_TYPE] = static_cast<std::uint8_t>( dent.typeflags ^ FLAG_SPLAT );

  /* Copy file name without 0-byte */
  std::memset( buf->data + DIR_OFS_FILE_NAME, 0xa0, CBM_NAME_LENGTH );
  std::memcpy( buf->data + DIR_OFS_FILE_NAME, dent.name.data(), ustrlen( dent.name.data() ) );

  /* Every 8th entry is two bytes shorter   */
  /* because the t/s link bytes are skipped */
  if ( ( ( buf->pvt.dir.counter++ ) & 0x7fU ) != 0 )
    buf->position = 0;
  else
    buf->position = 2;

  buf->lastused = 31;

  if ( ( buf->pvt.dir.counter & 0x7fU ) == 8 )
    buf->pvt.dir.counter &= 0x80U;

  return 0;
}

/**
 * load_directory - Prepare directory generation and create header
 * @secondary: secondary address used for reading the directory
 *
 * This function prepeares directory reading and fills the buffer
 * with the header line of the directory listing.
 * BUG: There is a not-well-known feature in the 1541/1571 disk
 * drives (and possibly others) that returns unparsed directory
 * sectors if $ is opened with a secondary address != 0. This
 * is not emulated here.
 */
void loadDirectory( Dos& dos, std::uint8_t secondary )
{
  Buffer* buf = nullptr;
  Path path{};
  std::uint8_t pos = 1;
  std::uint8_t* commandBuffer = dos.commands.commandBuffer.data();
  std::uint8_t const commandLength = dos.commands.commandLength;

  buf = allocBuffer( dos );
  if ( buf == nullptr )
    return;

  std::uint8_t* name = nullptr;

  buf->secondary = secondary;
  buf->read = true;
  buf->lastused = 31;

  if ( commandLength > 2 && secondary == 0 && commandBuffer[1] == '=' )
  {
    if ( commandBuffer[2] == 'P' )
    {
      /* Parse Partition Directory */

      /* copy static header to start of buffer */
      std::memcpy( buf->data, DIRHEADER.data(), DIRHEADER.size() );
      std::memcpy( buf->data + 32, SYSPART_LINE.data(), SYSPART_LINE.size() );
      buf->lastused = 63;

      /* set partition number */
      buf->data[HEADER_OFFSET_DRIVE] = dos.parser.maxPart;

      /* Let the refill callback handle everything else */
      buf->refill = pdirRefill;

      if ( commandLength > 3 )
      {
        /* Parse the name pattern */
        if ( parsePath( dos, commandBuffer + 3, &path, &name, 0 ) != 0 )
          return;

        buf->pvt.pdir.matchstr = name;
      }
      stickBuffer( buf );

      return;
    }
    if ( commandBuffer[2] == 'T' )
    {
      buf->pvt.dir.format = DirFormat::CMD_SHORT;
      pos = 3;
    }
  }

  if ( commandBuffer[pos] != 0 )
  { /* do we have a path to scan? */
    if ( commandLength > 2 )
    {
      /* Parse the name pattern */
      if ( parsePath( dos, commandBuffer + pos, &path, &name, 0 ) != 0 )
        return;

      if ( opendir( dos, &buf->pvt.dir.dh, &path ) != 0 )
        return;

      buf->pvt.dir.matchstr = name;

      /* Check for a filetype match */
      name = ustrchr( name, '=' );
      if ( name != nullptr )
      {
        *name++ = 0;
        switch ( *name )
        {
        case 'S':
          buf->pvt.dir.filetype = TYPE_SEQ;
          break;

        case 'P':
          buf->pvt.dir.filetype = TYPE_PRG;
          break;

        case 'U':
          buf->pvt.dir.filetype = TYPE_USR;
          break;

        case 'R':
          buf->pvt.dir.filetype = TYPE_REL;
          break;

        case 'C': /* This is guessed, not verified */
          buf->pvt.dir.filetype = TYPE_CBM;
          break;

        case 'B': /* CMD compatibility */
        case 'D': /* Specifying DEL matches everything anyway */
          buf->pvt.dir.filetype = TYPE_DIR;
          break;

        case 'H': /* Extension: Also show hidden files */
          buf->pvt.dir.filetype = FLAG_HIDDEN;
          break;

        default:
          break;
        }
        if ( buf->pvt.dir.filetype != 0 )
        {
          name++;
          if ( *name++ != ',' )
          {
            goto scandone;
          }
        }
        while ( *name != 0 )
        {
          switch ( *name++ )
          {
          case '>':
            if ( parseDate( dos, &dos.commands.dateMatchStart, &name ) != 0 )
              goto scandone;
            if ( dos.commands.dateMatchStart.month != 0 && dos.commands.dateMatchStart.day != 0 ) // ignore 00/00/00
              buf->pvt.dir.matchStart = &dos.commands.dateMatchStart;
            break;
          case '<':
            if ( parseDate( dos, &dos.commands.dateMatchEnd, &name ) != 0 )
              goto scandone;
            if ( dos.commands.dateMatchEnd.month != 0 && dos.commands.dateMatchEnd.day != 0 ) // ignore 00/00/00
              buf->pvt.dir.matchEnd = &dos.commands.dateMatchEnd;
            break;
          case 'L':
            /* don't switch to long format if 'N' has already been sent */
            if ( buf->pvt.dir.format != DirFormat::CBM )
              buf->pvt.dir.format = DirFormat::CMD_LONG;
            break;
          case 'N':
            buf->pvt.dir.format = DirFormat::CBM; /* turn off extended listing */
            break;
          default:
            goto scandone;
          }
          // NOLINTNEXTLINE(bugprone-inc-dec-in-conditions): && sequences it; NODISKEMU's test
          if ( *name != 0 && *name++ != ',' )
          {
            goto scandone;
          }
        }
      }
    }
    else
    {
      /* Command string is two characters long, parse the drive */
      if ( commandBuffer[1] == '0' )
      {
        path.part = dos.parser.currentPart;
      }
      else if ( commandBuffer[1] >= '0' && commandBuffer[1] <= '9' ) // isdigit
      {
        path.part = static_cast<std::uint8_t>( commandBuffer[1] - '0' - 1 );
      }
      else
      {
        buf->pvt.dir.matchstr = commandBuffer + 1;
        path.part = dos.parser.currentPart;
      }
      if ( path.part >= dos.parser.maxPart )
      {
        setError( dos, ERROR_DRIVE_NOT_READY );
        return;
      }
      path.dir = dos.parser.partition.at( path.part ).currentDir;
      if ( opendir( dos, &buf->pvt.dir.dh, &path ) != 0 )
        return;
    }
  }
  else
  {
    path.part = dos.parser.currentPart;
    path.dir = dos.parser.partition.at( path.part ).currentDir; // if you do not do this, get_label will fail below.
    if ( opendir( dos, &buf->pvt.dir.dh, &path ) != 0 )
      return;
  }

scandone:
  if ( secondary != 0 )
  {
    /* Raw directory */

    if ( dos.parser.partition.at( path.part ).fop == &D64OPS )
    {
      /* No need to fake it for D64 files */
      d64RawDirectory( dos, &path, buf );
      return;
    }

    /* prepare a fake BAM sector */
    std::memset( buf->data, 0, 256 );
    std::memset( buf->data + BAM_OFFSET_NAME - 2, 0xa0, BAM_A0_AREA_SIZE );

    /* fill label and id */
    if ( dirLabel( dos, &path, buf->data + BAM_OFFSET_NAME - 2 ) != 0 )
      return;

    if ( diskId( dos, &path, buf->data + BAM_OFFSET_ID - 2 ) != 0 )
      return;

    /* change padding of label and id to 0xa0 */
    name = buf->data + BAM_OFFSET_NAME - 2 + CBM_NAME_LENGTH;
    while ( *--name == ' ' )
      *name = 0xa0;

    if ( buf->data[BAM_OFFSET_ID + 2 - 2] == 0x20 )
      buf->data[BAM_OFFSET_ID + 2 - 2] = 0xa0;

    /* DOS version marker */
    buf->data[0] = 'A';

    buf->refill = rawdirRefill;
    buf->lastused = 253;
  }
  else
  {
    /* copy static header to start of buffer */
    std::memcpy( buf->data, DIRHEADER.data(), DIRHEADER.size() );

    /* set partition number */
    buf->data[HEADER_OFFSET_DRIVE] = static_cast<std::uint8_t>( path.part + 1 );

    /* read directory name */
    if ( dirLabel( dos, &path, buf->data + HEADER_OFFSET_NAME ) != 0 )
      return;

    /* read id */
    if ( diskId( dos, &path, buf->data + HEADER_OFFSET_ID ) != 0 )
      return;

    /* Let the refill callback handle everything else */
    buf->refill = dirRefill;
  }

  /* Keep the buffer around */
  stickBuffer( buf );
}

} // namespace

/**
 * directbuffer_refill - refill callback for direct buffers
 * @buf: buffer to be used
 *
 * This function is used as the refill callback for direct buffers ('#')
 * and will switch to the next or the first buffer in the chain. Always
 * returns 0.
 */
std::uint8_t directbufferRefill( Dos& /*dos*/, Buffer* buf )
{
  std::uint8_t const sec = buf->secondary;

  buf->secondary = static_cast<std::uint8_t>( BUFFER_SEC_CHAIN - sec );

  if ( buf->pvt.buffer.next == nullptr )
    buf = buf->pvt.buffer.first;
  else
    buf = buf->pvt.buffer.next;

  buf->secondary = sec;
  buf->position = 0;
  buf->mustflush = false;
  return 0;
}

namespace
{

/**
 * largebuffer_cleanup - cleanup callback for large buffers
 * @buf: buffer to be cleaned
 *
 * This function is used as the cleanup callback for large buffer and
 * will free all buffers used in the large buffer chain. This does mean
 * that the free_buffer call done after calling cleanup will be
 * passed an already-freed buffer, but that case is accounted for in
 * free_buffer and results in a no-op. Always returns 0.
 */
std::uint8_t largebufferCleanup( Dos& dos, Buffer* buf )
{
  buf = buf->pvt.buffer.first;
  while ( buf != nullptr )
  {
    freeBuffer( dos, buf );
    buf = buf->pvt.buffer.next;
  }
  return 0;
}

/* ------------------------------------------------------------------------- */
/*  External interface for the various operations                            */
/* ------------------------------------------------------------------------- */

/**
 * open_buffer - function to open a direct-access buffer
 * @secondary: secondary address used
 *
 * This function is called when the computer wants to open a direct
 * access buffer (#).
 */
void openBuffer( Dos& dos, std::uint8_t secondary )
{
  Buffer* buf = nullptr;
  Buffer* prev = nullptr;
  std::uint8_t count = 0;

  if ( dos.commands.commandLength == 3 && dos.commands.commandBuffer[1] == '#' )
  {
    /* Open a large buffer */
    count = static_cast<std::uint8_t>( dos.commands.commandBuffer[2] - '0' );
    if ( count == 0 )
      return;

    /* Allocate a chain of linked buffers */
    buf = allocLinkedBuffers( dos, count );
    if ( buf == nullptr )
      return;

    do
    {
      buf->secondary = static_cast<std::uint8_t>( BUFFER_SEC_CHAIN - secondary );
      buf->refill = directbufferRefill;
      buf->cleanup = largebufferCleanup;
      buf->read = true;
      buf->lastused = 255;
      buf->pvt.buffer.part = dos.parser.currentPart; // for completeness, not needed for large buffers yet
      markWriteBuffer( buf );
      prev = buf;
      buf = buf->pvt.buffer.next;
    } while ( buf != nullptr );

    prev->sendeoi = true;
    buf = prev->pvt.buffer.first;

    /* Set the first buffer as active by using the real secondary */
    buf->secondary = secondary;
  }
  else
  {
    /* Normal buffer request */
    // FIXME: This command can specify a specific buffer number.
    buf = allocBuffer( dos );
    if ( buf == nullptr )
      return;

    buf->secondary = secondary;
    buf->read = true;
    buf->position = 1; /* Sic! */
    buf->lastused = 255;
    buf->sendeoi = true;
    buf->pvt.buffer.size = 1;
    buf->pvt.buffer.part = dos.parser.currentPart;
    /* directbuffer_refill is used to check for # buffers in iec.c */
    buf->refill = directbufferRefill;
    buf->pvt.buffer.first = buf;
    markWriteBuffer( buf );
  }
}

} // namespace

/**
 * file_open_previous - reopens the last opened file
 *
 * This function reopens the last opened file in read mode.
 * The secondary address for this is always 0.
 */
void fileOpenPrevious( Dos& dos )
{
  CbmDirent dent = dos.files.previousFileDirent;
  Path path = dos.files.previousFilePath;
  Buffer* buf = allocBuffer( dos );

  if ( buf == nullptr )
    return;

  buf->secondary = 0;

  openRead( dos, &path, &dent, buf );
}

/**
 * file_open - open a file on given secondary
 * @secondary: secondary address used in OPEN call
 *
 * This function opens the file named in command_buffer on the given
 * secondary address. All special names and prefixes/suffixed are handled
 * here, e.g. $/#/@/,S,W
 */
void fileOpen( Dos& dos, std::uint8_t secondary )
{
  Buffer* buf = nullptr;
  std::uint8_t i = 0;
  std::uint8_t recordlen = 0;
  auto& commandBuffer = dos.commands.commandBuffer;
  auto& commandLength = dos.commands.commandLength;

  /* If the secondary is already in use, close the existing buffer */
  buf = findBuffer( dos, secondary );
  if ( buf != nullptr )
  {
    /* FIXME: What should we do if an error occurs? */
    cleanupAndFreeBuffer( dos, buf );
  }

  /* Assume everything will go well unless proven otherwise */
  setError( dos, ERROR_OK );

  /* Strip 0x0d characters from end of name (C2BD-C2CA) */
  if ( commandLength > 1 )
  {
    if ( commandBuffer[commandLength - 1] == 0x0d )
      commandLength -= 1;
    else if ( commandBuffer[commandLength - 2] == 0x0d )
      commandLength -= 2;
  }

  /* Clear the remainder of the command buffer, simplifies parsing */
  std::memset( commandBuffer.data() + commandLength, 0, commandBuffer.size() - commandLength );

  /* Direct access? */
  if ( commandBuffer[0] == '#' )
  {
    openBuffer( dos, secondary );
    return;
  }

  /* Parse type+mode suffixes */
  std::uint8_t* ptr = commandBuffer.data();
  OpenMode mode = OPEN_READ;
  std::uint8_t filetype = TYPE_DEL;

  /* check for a single * to load the previous file */
  if ( secondary == 0 && commandLength == 1 && commandBuffer[0] == '*' )
  {
    if ( dos.files.previousFileDirent.name[0] != 0 )
    {
      /* previous file is valid */
      fileOpenPrevious( dos );
      return;
    }

    /* force file type to PRG - d7cd/d81c */
    filetype = TYPE_PRG;
  }

  /* check file type and mode */
  while ( i++ < 2 && *ptr != 0 && ( ptr = ustrchr( ptr, ',' ) ) != nullptr )
  {
    *ptr = 0;
    ptr++;
    switch ( *ptr )
    {
    case 0:
      break;

    case 'R': /* Read */
      mode = OPEN_READ;
      break;

    case 'W': /* Write */
      mode = OPEN_WRITE;
      break;

    case 'A': /* Append */
      mode = OPEN_APPEND;
      break;

    case 'M': /* Modify */
      mode = OPEN_MODIFY;
      break;

    case 'D': /* DEL */
      filetype = TYPE_DEL;
      break;

    case 'S': /* SEQ */
      filetype = TYPE_SEQ;
      break;

    case 'P': /* PRG */
      filetype = TYPE_PRG;
      break;

    case 'U': /* USR */
      filetype = TYPE_USR;
      break;

    case 'L': /* REL */
      filetype = TYPE_REL;
      mode = OPEN_WRITE;
      ptr = ustrchr( ptr, ',' );
      if ( ptr != nullptr )
        recordlen = *( ++ptr );
      i = 2; // stop the scan
      break;

    default:
      break;
    }
  }

  /* Load directory? */
  if ( commandBuffer[0] == '$' )
  {
    loadDirectory( dos, secondary );
    return;
  }

  /* Parse path+partition numbers */
  std::uint8_t* fname = nullptr;
  std::int8_t res = 0;
  CbmDirent dent{};
  Path path{};

  /* Parse path and file name */
  if ( parsePath( dos, commandBuffer.data(), &path, &fname, 0 ) != 0 )
    return;

  /* Filename matching */
  if ( opendir( dos, &dos.buffers.matchdh, &path ) != 0 )
    return;

  do
  {
    res = nextMatch( dos, &dos.buffers.matchdh, fname, nullptr, nullptr, FLAG_HIDDEN, &dent );
    if ( res > 0 )
      /* Error, abort */
      return;

    /* Don't match on DEL or DIR */
    if ( ( dent.typeflags & TYPE_MASK ) != TYPE_DEL && ( dent.typeflags & TYPE_MASK ) != TYPE_DIR )
      break;

    /* But do match if it's for writing */
    if ( mode == OPEN_WRITE || secondary == 1 )
      break;
  } while ( res == 0 );

  if ( res != 0 && filetype == TYPE_REL && recordlen == 0 )
  {
    setError( dos, ERROR_SYNTAX_UNABLE );
    return;
  }

  /* If match found is a REL... */
  if ( res == 0 && ( dent.typeflags & TYPE_MASK ) == TYPE_REL )
  {
    /* requested type must be REL or DEL */
    if ( filetype != TYPE_REL && filetype != TYPE_DEL )
    {
      setError( dos, ERROR_FILE_TYPE_MISMATCH );
      return;
    }
    filetype = TYPE_REL;
    mode = OPEN_MODIFY;
  }

  /* Force mode+type for secondaries 0/1 */
  switch ( secondary )
  {
  case 0:
    mode = OPEN_READ;
    if ( filetype == TYPE_DEL )
      filetype = TYPE_PRG;
    break;

  case 1:
    mode = OPEN_WRITE;
    if ( filetype == TYPE_DEL )
      filetype = TYPE_PRG;
    break;

  default:
    if ( filetype == TYPE_DEL )
      filetype = TYPE_SEQ;
  }

  if ( mode == OPEN_WRITE )
  {
    if ( res == 0 )
    {
      /* Match found */
      if ( commandBuffer[0] == '@' )
      {
        /* Make sure there is a free buffer to open the new file later */
        if ( !checkFreeBuffers( dos ) )
        {
          setError( dos, ERROR_NO_CHANNEL );
          return;
        }

        /* Copy dent because file_delete may change it */
        CbmDirent dentcopy = dent;

        /* Rewrite existing file: Delete the old one */
        if ( fileDelete( dos, &path, &dentcopy ) == 255 )
          return;
      }
      else
      {
        /* Write existing file without replacement: Raise error */
        setError( dos, ERROR_FILE_EXISTS );
        return;
      }
    }
    else
    {
      /* Normal write or non-existing rewrite */
      /* Doesn't exist: Copy name to dent */
      std::memset( &dent, 0, sizeof( dent ) );
      ustrncpy( dent.name.data(), fname, CBM_NAME_LENGTH );
      setError( dos, ERROR_OK ); // because first_match has set FNF
    }
  }
  else if ( res != 0 )
  {
    /* File not found */
    setError( dos, ERROR_FILE_NOT_FOUND );
    return;
  }

  /* Grab a buffer */
  buf = allocBuffer( dos );
  if ( buf == nullptr )
    return;

  buf->secondary = secondary;

  if ( filetype == TYPE_REL )
  {
    openRel( dos, &path, &dent, buf, recordlen, static_cast<std::uint8_t>( mode == OPEN_MODIFY ) );
    return;
  }

  dos.files.previousFilePath = path;
  dos.files.previousFileDirent = dent;

  switch ( mode )
  {
  case OPEN_MODIFY:
  case OPEN_READ:
    /* Modify is the same as read, but allows reading *ed files.        */
    /* FAT doesn't have anything equivalent, so both are mapped to READ */
    openRead( dos, &path, &dent, buf );
    break;

  case OPEN_WRITE:
  case OPEN_APPEND:
    openWrite( dos, &path, &dent, filetype, buf, static_cast<std::uint8_t>( mode == OPEN_APPEND ) );
    break;
  }
}

} // namespace pet::sd2pet
