#pragma once

// errormsg.c (NODISKEMU at commit e51a6df): the error channel's message.

#include "sd2pet/DosTypes.hpp"

#include <array>
#include <cstdint>

namespace pet::sd2pet
{

struct ErrorState
{
  std::uint8_t currentError{};
  /// The firmware's ERROR_BUFFER_SIZE bytes, and after them a stand-in for
  /// the RAM that follows it: M-R of up to 255 bytes reads on past its end
  /// ("we return nonsense in this case", doscmd.c). Here that is zeros.
  std::array<std::uint8_t, 256> errorBuffer{};
};

void setErrorTs( Dos& dos, std::uint8_t errornum, std::uint8_t track, std::uint8_t sector );
void setError( Dos& dos, std::uint8_t errornum );
std::uint8_t setOkMessage( Dos& dos, Buffer* buf );

// Commodore DOS error codes
inline constexpr std::uint8_t ERROR_OK = 0;
inline constexpr std::uint8_t ERROR_SCRATCHED = 1;
inline constexpr std::uint8_t ERROR_PARTITION_SELECTED = 2;
inline constexpr std::uint8_t ERROR_STATUS = 3;
inline constexpr std::uint8_t ERROR_LONGVERSION = 9;
inline constexpr std::uint8_t ERROR_READ_NOHEADER = 20;
inline constexpr std::uint8_t ERROR_READ_NOSYNC = 21;
inline constexpr std::uint8_t ERROR_READ_NODATA = 22;
inline constexpr std::uint8_t ERROR_READ_CHECKSUM = 23;
inline constexpr std::uint8_t ERROR_WRITE_VERIFY = 25;
inline constexpr std::uint8_t ERROR_WRITE_PROTECT = 26;
inline constexpr std::uint8_t ERROR_READ_HDRCHECKSUM = 27;
inline constexpr std::uint8_t ERROR_DISK_ID_MISMATCH = 29;
inline constexpr std::uint8_t ERROR_SYNTAX_UNKNOWN = 30;
inline constexpr std::uint8_t ERROR_SYNTAX_UNABLE = 31;
inline constexpr std::uint8_t ERROR_SYNTAX_TOOLONG = 32;
inline constexpr std::uint8_t ERROR_SYNTAX_JOKER = 33;
inline constexpr std::uint8_t ERROR_SYNTAX_NONAME = 34;
inline constexpr std::uint8_t ERROR_FILE_NOT_FOUND_39 = 39;
inline constexpr std::uint8_t ERROR_RECORD_MISSING = 50;
inline constexpr std::uint8_t ERROR_RECORD_OVERFLOW = 51;
inline constexpr std::uint8_t ERROR_FILE_TOO_LARGE = 52;
inline constexpr std::uint8_t ERROR_WRITE_FILE_OPEN = 60;
inline constexpr std::uint8_t ERROR_FILE_NOT_OPEN = 61;
inline constexpr std::uint8_t ERROR_FILE_NOT_FOUND = 62;
inline constexpr std::uint8_t ERROR_FILE_EXISTS = 63;
inline constexpr std::uint8_t ERROR_FILE_TYPE_MISMATCH = 64;
inline constexpr std::uint8_t ERROR_NO_BLOCK = 65;
inline constexpr std::uint8_t ERROR_ILLEGAL_TS_COMMAND = 66;
inline constexpr std::uint8_t ERROR_ILLEGAL_TS_LINK = 67;
inline constexpr std::uint8_t ERROR_NO_CHANNEL = 70;
inline constexpr std::uint8_t ERROR_DIR_ERROR = 71;
inline constexpr std::uint8_t ERROR_DISK_FULL = 72;
inline constexpr std::uint8_t ERROR_DOSVERSION = 73;
inline constexpr std::uint8_t ERROR_DRIVE_NOT_READY = 74;
inline constexpr std::uint8_t ERROR_PARTITION_ILLEGAL = 77;
inline constexpr std::uint8_t ERROR_BUFFER_TOO_SMALL = 78;
inline constexpr std::uint8_t ERROR_IMAGE_INVALID = 79;
inline constexpr std::uint8_t ERROR_UNKNOWN_DRIVECODE = 98;
inline constexpr std::uint8_t ERROR_CLOCK_UNSTABLE = 99;

/// Version number string, will be added to message 73: what the petSD+
/// build of the commit ported calls itself (VERSION in scripts/Makefile.main,
/// with its PRERELEASE of the commit's date and hash).
inline constexpr char const* VERSIONSTR = "NODISKEMU V1.0.0ALPHA0-20260918-E51A6DF";

/// Long version string, used for message 9: -$(CONFIGSUFFIX), which
/// scripts/configparser.pl makes of the MCU and the config file's name.
inline constexpr char const* LONGVERSTR = "-m1284p-petSD+";

} // namespace pet::sd2pet
