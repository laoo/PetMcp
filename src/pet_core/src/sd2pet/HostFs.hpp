#pragma once

// The SD card, as a host directory: the part of FatFs (NODISKEMU's src/ff.h at
// commit e51a6df) the DOS calls, over the host's file system. The DOS keeps
// FatFs's types and calls, so its code reads as NODISKEMU's; what FatFs found
// on the card, HostFs finds in the directory. A directory is known by a number
// that stands where FatFs has its start cluster: 0 is the card's root, and the
// others are given out as the DOS meets them, and kept while the card is in;
// files are numbered alike. A file has an 8.3 name, as on FAT: its own when it
// is one, or one made up the way FAT makes them (NAME~1.EXT) when it is not. A
// directory reads in the order of its host names; the root holds a volume
// label, the host directory's name. HostFs.cpp says what else is made up.

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace pet::sd2pet
{

/// FRESULT
enum class FResult : std::uint8_t
{
  OK = 0,
  NOT_READY,
  NO_FILE,
  NO_PATH,
  INVALID_NAME,
  INVALID_DRIVE,
  DENIED,
  EXIST,
  RW_ERROR,
  WRITE_PROTECTED,
  NOT_ENABLED,
  NO_FILESYSTEM,
  INVALID_OBJECT,
  MKFS_ABORTED,
  IS_DIRECTORY,
  IS_READONLY,
  DIR_NOT_EMPTY,
  NOT_DIRECTORY
};

// File access modes, as f_open takes them.
inline constexpr std::uint8_t FA_READ = 0x01;
inline constexpr std::uint8_t FA_OPEN_EXISTING = 0x00;
inline constexpr std::uint8_t FA_WRITE = 0x02;
inline constexpr std::uint8_t FA_CREATE_NEW = 0x04;
inline constexpr std::uint8_t FA_CREATE_ALWAYS = 0x08;
inline constexpr std::uint8_t FA_OPEN_ALWAYS = 0x10;

// FAT sub types (FatVolume::fsType).
inline constexpr std::uint8_t FS_FAT12 = 1;
inline constexpr std::uint8_t FS_FAT16 = 2;
inline constexpr std::uint8_t FS_FAT32 = 3;

// File attributes.
inline constexpr std::uint8_t AM_RDO = 0x01;
inline constexpr std::uint8_t AM_HID = 0x02;
inline constexpr std::uint8_t AM_SYS = 0x04;
inline constexpr std::uint8_t AM_VOL = 0x08;
inline constexpr std::uint8_t AM_LFN = 0x0F;
inline constexpr std::uint8_t AM_DIR = 0x10;
inline constexpr std::uint8_t AM_ARC = 0x20;

/// The longest long name f_readdir gives (ff.h's _MAX_LFN_LENGTH).
inline constexpr std::size_t MAX_LFN_LENGTH = 20;

/// FATFS, as much of it as the DOS reads.
struct FatVolume
{
  /// The directory names are looked up in.
  std::uint32_t currDir;
  std::uint32_t fatbase;
  std::uint32_t maxClust;
  /// FS_FAT32 while mounted, 0 when not (FATFS's fs_type).
  std::uint8_t fsType;
  std::uint8_t csize;
};

/// FIL: an open file, as its place in HostFs's table of them.
struct FatFile
{
  /// HostFs's number for it, from 1; 0 (as a zeroed FatFile has it) when
  /// not open. Numbers are not reused while the card is in.
  std::int32_t handle;
  std::uint8_t flag;
  std::uint32_t fptr;
  std::uint32_t fsize;
};

/// DIR: a directory being read.
struct FatDir
{
  std::uint32_t cluster;
  std::uint32_t index;
};

/// FILINFO
struct FatFileInfo
{
  std::uint32_t fsize;
  std::uint16_t fdate;
  std::uint16_t ftime;
  std::uint8_t fattrib;
  std::uint32_t clust;
  std::array<std::uint8_t, 8 + 1 + 3 + 1> fname;
  /// Where f_readdir puts the long name, or null.
  std::uint8_t* lfn;
};

class HostFs
{
public:
  HostFs();
  ~HostFs();
  HostFs( HostFs const& ) = delete;
  HostFs& operator=( HostFs const& ) = delete;
  HostFs( HostFs&& ) = delete;
  HostFs& operator=( HostFs&& ) = delete;

  /// Puts in the card: the directory `root`. An empty path takes it out.
  /// Closes every file and forgets every directory's number.
  void setRoot( std::filesystem::path root );
  [[nodiscard]] std::filesystem::path const& root() const;
  /// Whether a card is in: f_mount answers NOT_READY when not.
  [[nodiscard]] bool present() const;

  /// The number a host directory goes by, given one if it has none. A
  /// directory outside the card may be given one too (Dos::mount does, for an
  /// image outside it); its parent is the card's root.
  std::uint32_t clusterOf( std::filesystem::path const& directory );
  /// The host directory a number stands for, or an empty path.
  [[nodiscard]] std::filesystem::path pathOf( std::uint32_t cluster ) const;
  /// The host file an open FatFile reads or writes, or an empty path when it
  /// is not open.
  [[nodiscard]] std::filesystem::path pathOfFile( FatFile const* fp ) const;
  /// The 8.3 name of the host file `file`, as f_readdir gives it in fname
  /// ("NAME.EXT"), or an empty string when its directory does not hold it.
  /// Gives the file's directory a number, as clusterOf does.
  std::string shortNameOf( std::filesystem::path const& file );

  FResult fMount( FatVolume* fs );
  FResult fOpen( FatVolume* fs, FatFile* fp, std::uint8_t const* path, std::uint8_t mode );
  FResult fRead( FatFile* fp, void* buff, unsigned btr, unsigned* br );
  FResult fWrite( FatFile* fp, void const* buff, unsigned btw, unsigned* bw );
  FResult fLseek( FatFile* fp, std::uint32_t ofs );
  FResult fClose( FatFile* fp );
  FResult fSync( FatFile* fp );
  FResult fReaddir( FatDir* dj, FatFileInfo* fno );
  FResult fStat( FatVolume* fs, std::uint8_t const* path, FatFileInfo* fno );
  FResult fUnlink( FatVolume* fs, std::uint8_t const* path );
  FResult fMkdir( FatVolume* fs, std::uint8_t const* path );
  FResult fRename( FatVolume* fs, std::uint8_t const* pathOld, std::uint8_t const* pathNew );
  FResult lOpendir( FatVolume* fs, std::uint32_t cluster, FatDir* dirobj );
  FResult lOpencluster( FatVolume* fs, FatFile* fp, std::uint32_t clust );
  FResult lGetfree( FatVolume* fs, std::uint8_t const* path, std::uint32_t* nclust, std::uint32_t maxclust );

private:
  struct State;
  std::unique_ptr<State> mState;
};

} // namespace pet::sd2pet
