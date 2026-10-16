// Copyright (C) 2007-2018 Ingo Korb <ingo@akana.de>, as NODISKEMU (a fork of sd2iec)
// carries it. GPL-2.0. Ported to C++ for PetMcp, 2026 (THIRD_PARTY.md).

#pragma once

// NODISKEMU's DOS, ported (docs/decisions/0010-sd2pet.md): what was the
// firmware's globals, by the file that held them, and the wrappers of
// wrapops.h. Every function of the port takes the Dos it works on, first, so
// that two machines have two drives.
//
// Holds pointers into itself (a buffer's data, the BAM buffers), so it is
// neither copied nor moved.

#include "sd2pet/Buffers.hpp"
#include "sd2pet/D64Ops.hpp"
#include "sd2pet/DosCommands.hpp"
#include "sd2pet/DosTypes.hpp"
#include "sd2pet/ErrorMessages.hpp"
#include "sd2pet/FatOps.hpp"
#include "sd2pet/FileOps.hpp"
#include "sd2pet/HostFs.hpp"
#include "sd2pet/P00Cache.hpp"
#include "sd2pet/Parser.hpp"

#include <cstdint>
#include <exception>

namespace pet::sd2pet
{

struct Dos
{
  Dos() = default;
  ~Dos() = default;
  Dos( Dos const& ) = delete;
  Dos& operator=( Dos const& ) = delete;
  Dos( Dos&& ) = delete;
  Dos& operator=( Dos&& ) = delete;

  /// The SD card.
  HostFs card;
  BuffersState buffers;
  ErrorState errors;
  ParserState parser;
  P00CacheState p00cache;
  D64State d64;
  FatOpsState fat;
  FileOpsState files;
  DosCommandsState commands;
  /// bus.h's device_address.
  std::uint8_t deviceAddress{ 8 };
};

/// Thrown where the firmware would loop for ever: the drive stops, as the
/// ATmega would, and only its ATN interrupt still answers. The emulator must
/// not loop with it.
struct DriveHung : std::exception
{
};

/// What main.c does before the bus loop: the buffers, the file system, and
/// the error channel's 73 that ieee_mainloop sets.
void dosInit( Dos& dos );

// wrapops.h: the calls of the current partition's operations.

inline void openRead( Dos& dos, Path* path, CbmDirent* name, Buffer* buf )
{
  dos.parser.partition.at( path->part ).fop->openRead( dos, path, name, buf );
}

inline void openWrite( Dos& dos, Path* path, CbmDirent* name, std::uint8_t type, Buffer* buf, std::uint8_t append )
{
  dos.parser.partition.at( path->part ).fop->openWrite( dos, path, name, type, buf, append );
}

inline void openRel( Dos& dos, Path* path, CbmDirent* name, Buffer* buf, std::uint8_t len, std::uint8_t mode )
{
  dos.parser.partition.at( path->part ).fop->openRel( dos, path, name, buf, len, mode );
}

inline std::uint8_t fileDelete( Dos& dos, Path* path, CbmDirent* name )
{
  return dos.parser.partition.at( path->part ).fop->fileDelete( dos, path, name );
}

inline std::uint8_t diskLabel( Dos& dos, std::uint8_t part, std::uint8_t* label )
{
  return dos.parser.partition.at( part ).fop->diskLabel( dos, part, label );
}

inline std::uint8_t dirLabel( Dos& dos, Path* path, std::uint8_t* label )
{
  return dos.parser.partition.at( path->part ).fop->dirLabel( dos, path, label );
}

inline std::uint8_t diskId( Dos& dos, Path* path, std::uint8_t* id )
{
  return dos.parser.partition.at( path->part ).fop->diskId( dos, path, id );
}

inline std::uint16_t diskFree( Dos& dos, std::uint8_t drv )
{
  return dos.parser.partition.at( drv ).fop->diskFree( dos, drv );
}

inline void readSector( Dos& dos, Buffer* buf, std::uint8_t drv, std::uint8_t t, std::uint8_t s )
{
  dos.parser.partition.at( drv ).fop->readSector( dos, buf, drv, t, s );
}

inline void writeSector( Dos& dos, Buffer* buf, std::uint8_t drv, std::uint8_t t, std::uint8_t s )
{
  dos.parser.partition.at( drv ).fop->writeSector( dos, buf, drv, t, s );
}

inline void format( Dos& dos, std::uint8_t drv, std::uint8_t* name, std::uint8_t* id )
{
  dos.parser.partition.at( drv ).fop->format( dos, drv, name, id );
}

inline std::uint8_t opendir( Dos& dos, Dh* dh, Path* path )
{
  return dos.parser.partition.at( path->part ).fop->opendir( dos, dh, path );
}

inline std::int8_t readdir( Dos& dos, Dh* dh, CbmDirent* dent )
{
  return dos.parser.partition.at( dh->part ).fop->readdir( dos, dh, dent );
}

inline void mkdir( Dos& dos, Path* path, std::uint8_t* dir )
{
  dos.parser.partition.at( path->part ).fop->mkdir( dos, path, dir );
}

inline std::uint8_t chdir( Dos& dos, Path* path, CbmDirent* dent )
{
  return dos.parser.partition.at( path->part ).fop->chdir( dos, path, dent );
}

inline void rename( Dos& dos, Path* path, CbmDirent* oldName, std::uint8_t* newName )
{
  dos.parser.partition.at( path->part ).fop->rename( dos, path, oldName, newName );
}

} // namespace pet::sd2pet
