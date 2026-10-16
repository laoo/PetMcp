// Copyright (C) 2007-2018 Ingo Korb <ingo@akana.de>, as NODISKEMU (a fork of sd2iec)
// carries it; ASCII/PET conversion Copyright (C) 2008 Jim Brain <brain@jbrain.com>.
// GPL-2.0. Ported to C++ for PetMcp, 2026 (THIRD_PARTY.md).

#pragma once

// fatops.c and fatops.h (NODISKEMU at commit e51a6df): the card's own files,
// on HostFs where NODISKEMU has FatFs, and the mounting of the images on it.

#include "sd2pet/DosTypes.hpp"

#include <cstdint>
#include <filesystem>

namespace pet::sd2pet
{

struct FatOpsState
{
  /// 0: never x00; 1: x00 except PRG; 2: always x00; 3: type extension
  /// except PRG; 4: always a type extension.
  std::uint8_t fileExtensionMode{};
};

void fatopsInit( Dos& dos, std::uint8_t preserveDir );
void parseError( Dos& dos, FResult res, std::uint8_t readflag );
std::uint8_t fatDelete( Dos& dos, Path* path, CbmDirent* dent );
std::uint8_t fatChdir( Dos& dos, Path* path, CbmDirent* dent );
void fatMkdir( Dos& dos, Path* path, std::uint8_t* dirname );
void fatOpenRead( Dos& dos, Path* path, CbmDirent* filename, Buffer* buf );
void fatOpenWrite( Dos& dos, Path* path, CbmDirent* filename, std::uint8_t type, Buffer* buf, std::uint8_t append );
void fatOpenRel( Dos& dos, Path* path, CbmDirent* dent, Buffer* buf, std::uint8_t length, std::uint8_t mode );
FResult createFile( Dos& dos, Path* path, CbmDirent* dent, std::uint8_t type, Buffer* buf, std::uint8_t recordlen );
std::uint8_t fatFileSeek( Dos& dos, Buffer* buf, std::uint32_t position, std::uint8_t index );
std::uint8_t fatGetdirlabel( Dos& dos, Path* path, std::uint8_t* label );
std::uint8_t fatGetid( Dos& dos, Path* path, std::uint8_t* id );
std::uint16_t fatFreeblocks( Dos& dos, std::uint8_t part );
std::uint8_t fatOpendir( Dos& dos, Dh* dh, Path* dir );
std::int8_t fatReaddir( Dos& dos, Dh* dh, CbmDirent* dent );
void fatReadSector( Dos& dos, Buffer* buf, std::uint8_t part, std::uint8_t track, std::uint8_t sector );
void fatWriteSector( Dos& dos, Buffer* buf, std::uint8_t part, std::uint8_t track, std::uint8_t sector );
void formatDummy( Dos& dos, std::uint8_t drive, std::uint8_t* name, std::uint8_t* id );

extern FileOps const FATOPS;

std::uint8_t imageUnmount( Dos& dos, std::uint8_t part );
std::uint8_t imageChdir( Dos& dos, Path* path, CbmDirent* dent );
void imageMkdir( Dos& dos, Path* path, std::uint8_t* dirname );
std::uint8_t imageRead( Dos& dos, std::uint8_t part, std::uint32_t offset, void* buffer, std::uint16_t bytes );
std::uint8_t
imageWrite( Dos& dos, std::uint8_t part, std::uint32_t offset, void* buffer, std::uint16_t bytes, std::uint8_t flush );

enum class ImgType : std::uint8_t
{
  UNKNOWN,
  IS_DISK
};

ImgType checkImageext( std::uint8_t* name );
void pet2asc( std::uint8_t* buf );

/// Not NODISKEMU's: makes the directory holding the host file `image` the
/// current one on partition 0, and enters the image as `CD` of its entry
/// would. The file may be outside the card. Sets the error channel as CD
/// does, and returns non-zero when it failed.
std::uint8_t fatMountHostImage( Dos& dos, std::filesystem::path const& image );

} // namespace pet::sd2pet
