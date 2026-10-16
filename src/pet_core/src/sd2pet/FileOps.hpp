#pragma once

// fileops.c and fileops.h (NODISKEMU at commit e51a6df): OPEN of a secondary
// address: files, $ and the other directories, # buffers.

#include "sd2pet/DosTypes.hpp"

#include <array>
#include <cstdint>

namespace pet::sd2pet
{

enum OpenMode : std::uint8_t
{
  OPEN_READ,
  OPEN_WRITE,
  OPEN_APPEND,
  OPEN_MODIFY
};

/// fileops.c's globals and statics.
struct FileOpsState
{
  /// flags.h's image_as_dir: IMAGE_DIR_NORMAL, _DIR or _BOTH.
  std::uint8_t imageAsDir{};
  /* saved dirent of the last opened file */
  CbmDirent previousFileDirent{};
  Path previousFilePath{};
};

/// The letters of the file types, three to each, by the type's number: DEL to
/// DIR, ???, then the partition directory's NAT, 41, 71 and 81 (fileops.c's
/// filetypes).
extern std::array<std::uint8_t, 36> const FILETYPES;

/* Refill-callback for large buffers, only used for comparision */
std::uint8_t directbufferRefill( Dos& dos, Buffer* buf );

/* reopen the last opened file on secondary 0 */
void fileOpenPrevious( Dos& dos );

/* Parses a filename in command_buffer and opens that file */
void fileOpen( Dos& dos, std::uint8_t secondary );

} // namespace pet::sd2pet
