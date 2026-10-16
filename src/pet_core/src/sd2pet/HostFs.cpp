// Parts of this file are ported from FatFs, whose notice follows:
//
//  FatFs - FAT file system module  R0.06                     (C)ChaN, 2008
//  Copyright (C) 2008, ChaN, all right reserved.
//
//  * The FatFs module is a free software and there is no warranty.
//  * You can use, modify and/or redistribute it for personal, non-profit or
//  * commercial use without restriction under your responsibility.
//  * Redistributions of source code must retain the above copyright notice.
//
// FatFs's interface to the DOS, over a host directory: the functions of
// NODISKEMU's src/ff.c and ff.h at commit e51a6df that fatops.c, d64ops.c and
// doscmd.c call (ChaN's FatFs R0.06 with Jim Brain's long names and
// NODISKEMU's l_* functions), as NODISKEMU configures it: _USE_LFN 1,
// _MAX_LFN_LENGTH 20, _USE_CURR_DIR 1, _USE_CHDIR 0, _USE_NTFLAG 0,
// _USE_DRIVE_PREFIX 0, _USE_DEFERRED_MOUNT 0.
//
// What the DOS can observe is FatFs's: the FR_* code of each failure, the
// parsing of a path into an 8.3 or a long name (make_dirfile), which of a
// file's two names a lookup matches (trace_path), what f_readdir gives as the
// long name, the short names FatFs makes for the long names it creates
// (create_short_name, fix_short_name), the "." and ".." entries of a
// subdirectory, and the attributes. What a FAT card has and a host directory
// has not is made up:
// - The order of a directory is the host names' byte order.
// - A directory's number stands for its start cluster: 0 is the card's root,
//   the others are given out from 3 as directories are met. A file has a
//   number too (its start cluster), 0 while it is empty, as on FAT.
// - A file met on the host has the short name a PC would have given it: its
//   own name when that is a valid 8.3 name, else a basis name, ~N and the
//   extension, numbered in name order within its directory. It has a long
//   name unless its 8.3 name says all (a PC records an all-lower-case body or
//   extension in the NT flags, which this FatFs ignores).
// - The root holds a volume label: the host directory's name.
// - The card is FAT32 with 32 KB clusters; its size and free space are the
//   host volume's. It is write-protected when its root is not writable.
// - Non-ASCII: a name FatFs creates is read as Latin-1 (FatFs stores each
//   character as the UCS-2 code of its byte); a long name read gives the low
//   byte of each character, as FatFs does.
// - Files written get the host's modification time. NODISKEMU's get_fattime
//   gives an undefined date on a petSD+ whose RTC is not found (read_rtc
//   leaves the time it is passed untouched).
//
// Left out: f_mkfs, f_chmod, f_chdir, f_truncate, f_utime, the string
// functions, partitions and the drive prefix, and the FAT itself.

#include "sd2pet/HostFs.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace pet::sd2pet
{

namespace
{

namespace stdfs = std::filesystem;

/* File status flags (FIL.flag) beyond the access modes */
constexpr std::uint8_t FA_WRITTEN = 0x20; // FA__WRITTEN
constexpr std::uint8_t FA_ERROR = 0x80;   // FA__ERROR

constexpr std::uint8_t FA_CREATE_ANY = FA_CREATE_ALWAYS | FA_OPEN_ALWAYS | FA_CREATE_NEW;

// The made-up geometry: a FAT32 partition at the usual 4 MB offset of an SD
// card, 32 reserved sectors, 64 sectors of 512 bytes a cluster.
constexpr std::uint32_t SECTOR_SIZE = 512; // SS(fs)
constexpr std::uint8_t CLUSTER_SECTORS = 64;
constexpr std::uint64_t CLUSTER_BYTES = std::uint64_t{ SECTOR_SIZE } * CLUSTER_SECTORS;
constexpr std::uint32_t PARTITION_START = 8192;
constexpr std::uint32_t RESERVED_SECTORS = 32;
/// FatFs takes a volume for FAT32 from 0xFFF7 clusters (max_clust) on.
constexpr std::uint64_t MIN_CLUSTERS = 0xFFF7 - 2;
constexpr std::uint64_t MAX_CLUSTERS = 0x0FFFFFF5;

/// The first number given to a directory or file; 0 is the root, and 2 is
/// where FAT32 keeps the root.
constexpr std::uint32_t FIRST_NUMBER = 3;

using ShortName = std::array<std::uint8_t, 11>;
using DirName = std::array<std::uint8_t, 12>;

constexpr ShortName DOT_NAME = { '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
constexpr ShortName DOTDOT_NAME = { '.', '.', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' };

// ---------------------------------------------------------------------------
// Names

std::string toUtf8( stdfs::path const& path )
{
  auto const u8 = path.u8string();
  std::string out;
  out.reserve( u8.size() );
  for ( auto const c : u8 )
  {
    out.push_back( static_cast<char>( c ) );
  }
  return out;
}

stdfs::path fromUtf8( std::string const& name )
{
  std::u8string u8;
  u8.reserve( name.size() );
  for ( auto const c : name )
  {
    u8.push_back( static_cast<char8_t>( c ) );
  }
  return stdfs::path{ u8 };
}

/// A name FatFs writes, a byte to a character, as UTF-8.
std::string latin1ToUtf8( std::string const& bytes )
{
  std::string out;
  for ( auto const ch : bytes )
  {
    auto const c = static_cast<std::uint8_t>( ch );
    if ( c < 0x80 )
    {
      out.push_back( static_cast<char>( c ) );
    }
    else
    {
      out.push_back( static_cast<char>( 0xC0U | ( c >> 6U ) ) );
      out.push_back( static_cast<char>( 0x80U | ( c & 0x3FU ) ) );
    }
  }
  return out;
}

/// The characters of a host name; a byte that is not UTF-8 stands for itself.
std::vector<std::uint32_t> decodeUtf8( std::string const& name )
{
  std::vector<std::uint32_t> out;
  std::size_t i = 0;
  while ( i < name.size() )
  {
    auto const c = static_cast<std::uint8_t>( name[i] );
    std::size_t more = 0;
    std::uint32_t cp = c;
    if ( c >= 0xF0 && c < 0xF8 )
    {
      more = 3;
      cp = c & 0x07U;
    }
    else if ( c >= 0xE0 && c < 0xF0 )
    {
      more = 2;
      cp = c & 0x0FU;
    }
    else if ( c >= 0xC0 && c < 0xE0 )
    {
      more = 1;
      cp = c & 0x1FU;
    }
    bool valid = more > 0;
    for ( std::size_t k = 1; valid && k <= more; ++k )
    {
      if ( i + k >= name.size() || ( static_cast<std::uint8_t>( name[i + k] ) & 0xC0U ) != 0x80U )
      {
        valid = false;
      }
      else
      {
        cp = ( cp << 6U ) | ( static_cast<std::uint8_t>( name[i + k] ) & 0x3FU );
      }
    }
    if ( valid )
    {
      out.push_back( cp );
      i += more + 1;
    }
    else
    {
      out.push_back( c );
      ++i;
    }
  }
  return out;
}

/// A long name as FatFs reads it: the low byte of each UCS-2 character.
std::string lowBytes( std::string const& name )
{
  std::string out;
  for ( auto const cp : decodeUtf8( name ) )
  {
    out.push_back( static_cast<char>( cp & 0xFFU ) );
  }
  return out;
}

std::uint8_t toUpper( std::uint8_t c )
{
  return ( c >= 'a' && c <= 'z' ) ? static_cast<std::uint8_t>( c - 0x20 ) : c;
}

/// A character a short name may hold, besides the letters and digits.
bool isShortNameChar( std::uint8_t c )
{
  if ( ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) )
  {
    return true;
  }
  return c != 0 && std::strchr( "!#$%&'()-@^_`{}~", c ) != nullptr;
}

/// The 8.3 name a host name is when it is one (in upper case): one that
/// make_dirfile parses as 8.3, without a leading dot, in ASCII.
std::optional<ShortName> ownShortName( std::string const& name )
{
  if ( name.empty() || name[0] == '.' )
  {
    return std::nullopt;
  }
  auto const dot = name.find( '.' );
  if ( dot != std::string::npos && name.find( '.', dot + 1 ) != std::string::npos )
  {
    return std::nullopt;
  }
  auto const body = name.substr( 0, dot );
  auto const ext = dot == std::string::npos ? std::string{} : name.substr( dot + 1 );
  if ( body.empty() || body.size() > 8 || ext.size() > 3 || ( dot != std::string::npos && ext.empty() ) )
  {
    return std::nullopt;
  }
  ShortName out;
  out.fill( ' ' );
  for ( std::size_t i = 0; i < body.size(); ++i )
  {
    auto const c = toUpper( static_cast<std::uint8_t>( body[i] ) );
    if ( !isShortNameChar( c ) )
    {
      return std::nullopt;
    }
    out.at( i ) = c;
  }
  for ( std::size_t i = 0; i < ext.size(); ++i )
  {
    auto const c = toUpper( static_cast<std::uint8_t>( ext[i] ) );
    if ( !isShortNameChar( c ) )
    {
      return std::nullopt;
    }
    out.at( 8 + i ) = c;
  }
  return out;
}

/// Whether a valid 8.3 name needs a long name to keep its case: a PC keeps an
/// all-lower-case body or extension in the NT flags, a mixed one in a long name.
bool hasMixedCase( std::string const& name )
{
  auto const mixed = []( std::string const& part )
  {
    bool const lower = std::ranges::any_of( part, []( char c ) { return c >= 'a' && c <= 'z'; } );
    bool const upper = std::ranges::any_of( part, []( char c ) { return c >= 'A' && c <= 'Z'; } );
    return lower && upper;
  };
  auto const dot = name.find( '.' );
  if ( dot == std::string::npos )
  {
    return mixed( name );
  }
  return mixed( name.substr( 0, dot ) ) || mixed( name.substr( dot + 1 ) );
}

/// The short name a PC gives a host name that is not one: the basis name
/// (spaces and dots left out, the rest upper case or '_'), ~N, and the first
/// three characters of the extension; N counts up until the name is free.
ShortName pcShortName( std::string const& name, std::set<ShortName> const& used )
{
  auto chars = decodeUtf8( name );
  std::size_t first = 0;
  while ( first < chars.size() && chars[first] == '.' )
  {
    ++first;
  }
  std::size_t lastDot = chars.size();
  for ( std::size_t i = first; i < chars.size(); ++i )
  {
    if ( chars[i] == '.' )
    {
      lastDot = i;
    }
  }
  auto const map = []( std::uint32_t cp, std::string& out )
  {
    if ( cp == ' ' || cp == '.' )
    {
      return;
    }
    auto const c = cp < 0x80 ? toUpper( static_cast<std::uint8_t>( cp ) ) : std::uint8_t{ '_' };
    out.push_back( static_cast<char>( isShortNameChar( c ) ? c : '_' ) );
  };
  std::string body;
  std::string ext;
  for ( std::size_t i = first; i < lastDot; ++i )
  {
    map( chars[i], body );
  }
  for ( std::size_t i = lastDot + 1; i < chars.size(); ++i )
  {
    map( chars[i], ext );
  }
  if ( body.empty() )
  {
    body = "_";
  }
  ext.resize( std::min<std::size_t>( ext.size(), 3 ) );

  for ( unsigned n = 1;; ++n )
  {
    auto const tail = "~" + std::to_string( n );
    auto const basis = body.substr( 0, std::min( body.size(), 8 - tail.size() ) ) + tail;
    ShortName out;
    out.fill( ' ' );
    std::ranges::copy( basis, out.begin() );
    std::ranges::copy( ext, out.begin() + 8 );
    if ( !used.contains( out ) )
    {
      return out;
    }
  }
}

/// get_fileinfo's conversion of a directory entry's name.
void formatShortName( ShortName const& dir, std::array<std::uint8_t, 8 + 1 + 3 + 1>& fname )
{
  std::size_t p = 0;
  for ( std::size_t n = 0; n < 8; n++ )
  { /* Convert file name (body) */
    auto c = dir.at( n );
    if ( c == ' ' )
      break;
    if ( c == 0x05 )
      c = 0xE5;
    fname.at( p++ ) = c;
  }
  if ( dir[8] != ' ' )
  { /* Convert file name (extension) */
    fname.at( p++ ) = '.';
    for ( std::size_t n = 8; n < 11; n++ )
    {
      auto const c = dir.at( n );
      if ( c == ' ' )
        break;
      fname.at( p++ ) = c;
    }
  }
  fname.at( p ) = '\0';
}

std::string shortNameString( ShortName const& dir )
{
  std::array<std::uint8_t, 8 + 1 + 3 + 1> fname{};
  formatShortName( dir, fname );
  return std::string{ reinterpret_cast<char const*>( fname.data() ) };
}

/// make_dirfile: picks a paragraph of the path and makes it an entry's name.
/// Returns 1 for an invalid name, else the character that ended it ('\0' or
/// '/'); lfn tells whether it is a long name.
std::uint8_t makeDirfile( std::uint8_t const*& path, DirName& dirname, bool& lfn )
{
  std::uint8_t n = 0;
  std::uint8_t t = 8;
  std::uint8_t c = 0;

  lfn = false;
  std::fill_n( dirname.begin(), 8 + 3, ' ' ); /* Fill buffer with spaces */
  for ( ;; )
  {
    c = *path++;
    if ( c == '\0' || c == '/' )
    { /* Reached to end of str or directory separator */
      if ( n == 0 )
        return 1;
      dirname[11] = 0;
      return c;
    }
    if ( c < ' ' || c == 0x7F )
      return 1; /* Reject invisible chars */
    if ( c == '"' )
      return 1; /* Reject " */
    bool toLfn = false;
    if ( c == ' ' || ( c > ')' && c <= ',' ) || ( c > '9' && c <= '?' ) || c == '|' || ( c >= '[' && c <= ']' ) )
    {
      /* Reject * + , : ; < = > ? | [ \ ], accept ! # $ % & ' ( ) - 0-9 */
      toLfn = true;
    }
    else if ( c == '.' )
    {
      /* "." and ".." go on to md_l2 */
      if ( n != 0 && ( n != 1 || dirname[0] != '.' ) )
      {
        if ( n >= 1 && n <= 8 )
        { /* Enter extension part */
          n = 8;
          t = 11;
          continue;
        }
        toLfn = true;
      }
    }
    else if ( c >= 'a' && c <= 'z' )
    {
      c = static_cast<std::uint8_t>( c - 0x20 ); /* Convert to upper case */
    }
    if ( !toLfn )
    {
      /* md_l2 */
      if ( n >= t )
      {
        toLfn = true;
      }
      else
      {
        dirname.at( n++ ) = c;
        continue;
      }
    }
    /* md_l3 */
    do
    {
      if ( c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' )
        return 1;
      c = *path++;
    } while ( c != '\0' && c != '/' ); /* Reached to end of str or directory separator */
    lfn = true;
    return c;
  }
}

std::uint8_t dosChar( std::uint8_t x )
{
  if ( x > ( 'a' - 1 ) && x < ( 'z' + 1 ) )
    return static_cast<std::uint8_t>( x - 'a' + 'A' );
  /* Kept: ' ' to '*', A-Z, 0-9, the upper half, and , - ^ ~ = _ ` { } */
  bool const kept = ( x > ( ' ' - 1 ) && x < '+' ) || ( x > ( 'A' - 1 ) && x < ( 'Z' + 1 ) ) ||
                    ( x > ( '0' - 1 ) && x < ( '9' + 1 ) ) || ( x & 0x80U ) != 0 ||
                    ( x != 0 && std::strchr( ",-^~=_`{}", x ) != nullptr );
  return kept ? x : std::uint8_t{ '_' };
}

/// create_short_name: the short name FatFs gives a long name it creates.
ShortName createShortName( std::string const& longName )
{
  // FatFs reads the basis of a name without a dot from its first six
  // characters, past its end when it is shorter: zeros here, where FatFs
  // meets what its buffer held before.
  std::vector<std::uint8_t> padded( longName.begin(), longName.end() );
  padded.resize( std::max<std::size_t>( padded.size(), 6 ) + 1 );
  std::uint8_t const* name = padded.data();
  auto const len = longName.size();
  std::uint8_t i = 0;
  std::uint8_t k = 0;
  std::uint8_t l = 0;
  // FatFs counts in a BYTE, len it takes in an UINT.
  auto j = static_cast<std::uint8_t>( len );
  ShortName buf;

  buf.fill( ' ' );
  if ( name[0] != '.' )
  {
    while ( --j != 0 )
    { /* find the last '.' */
      if ( name[j] == '.' )
        break;
    }
    k = static_cast<std::uint8_t>( j + 1 );
    i = 8;
    if ( k > 1 )
    {
      while ( k < len && i < 11 )
      {
        if ( name[k] != ' ' )
        {
          // build extension
          buf.at( i++ ) = dosChar( name[k] );
        }
        k++;
      }
    }
    else
    {
      j = 6;
    }
  }
  i = 0; /* now build start */
  while ( i < 6 && l < j )
  {
    if ( name[l] != ' ' && name[l] != '.' )
    {
      buf.at( i++ ) = dosChar( name[l] );
    }
    l++;
  }
  if ( i < 3 )
  {
    buf.at( i++ ) = 'A'; /* FIXME: replace this with a way to generate a random or hash hex word. */
    buf.at( i++ ) = 'B';
    buf.at( i++ ) = 'C';
    buf.at( i++ ) = 'D';
  }
  buf.at( i++ ) = '~';
  buf.at( i++ ) = '1';
  if ( buf[0] == 0xe5 )
    buf[0] = 0x05;
  return buf;
}

/// fix_short_name: counts the ~N of a short name up.
bool fixShortName( ShortName& buf )
{
  std::uint8_t i = 8;
  bool c = false;
  while ( buf.at( --i ) == ' ' )
    ;
  do
  {
    if ( buf.at( i ) == '9' )
    {
      buf.at( i ) = '0';
      c = true;
    }
    else if ( c && buf.at( i ) == '~' )
    {
      buf.at( i ) = '1';
      break;
    }
    else
    {
      buf.at( i )++;
      c = false;
      break;
    }
  } while ( --i != 0 );
  if ( c )
  {
    if ( i == 0 )
      return false;
    buf.at( i - 1 ) = '~';
  }
  return true;
}

/// A host file's modification time as FAT packs it, in local time.
void fatTime( stdfs::path const& path, std::uint16_t& date, std::uint16_t& time )
{
  date = ( 1U << 5U ) | 1U; /* 1980-01-01 */
  time = 0;
  std::error_code ec;
  auto const ftime = stdfs::last_write_time( path, ec );
  if ( ec )
    return;
#ifdef _MSC_VER
  auto const sys = std::chrono::clock_cast<std::chrono::system_clock>( ftime );
#else
  auto const sys = stdfs::file_time_type::clock::to_sys( ftime );
#endif
  auto const t =
      std::chrono::system_clock::to_time_t( std::chrono::time_point_cast<std::chrono::system_clock::duration>( sys ) );
  std::tm tm{};
#ifdef _WIN32
  if ( localtime_s( &tm, &t ) != 0 )
    return;
#else
  if ( localtime_r( &t, &tm ) == nullptr )
    return;
#endif
  int const year = tm.tm_year + 1900;
  if ( year < 1980 )
    return;
  if ( year > 2107 )
  {
    date = static_cast<std::uint16_t>( ( 127U << 9U ) | ( 12U << 5U ) | 31U );
    time = static_cast<std::uint16_t>( ( 23U << 11U ) | ( 59U << 5U ) | 29U );
    return;
  }
  date = static_cast<std::uint16_t>( ( static_cast<unsigned>( year - 1980 ) << 9U ) |
                                     ( static_cast<unsigned>( tm.tm_mon + 1 ) << 5U ) |
                                     static_cast<unsigned>( tm.tm_mday ) );
  time = static_cast<std::uint16_t>( ( static_cast<unsigned>( tm.tm_hour ) << 11U ) |
                                     ( static_cast<unsigned>( tm.tm_min ) << 5U ) |
                                     ( static_cast<unsigned>( std::min( tm.tm_sec, 59 ) ) / 2U ) );
}

FResult hostError( std::error_code const& ec )
{
  if ( ec == std::errc::permission_denied || ec == std::errc::operation_not_permitted ||
       ec == std::errc::read_only_file_system )
    return FResult::WRITE_PROTECTED;
  if ( ec == std::errc::no_space_on_device )
    return FResult::DENIED;
  return FResult::RW_ERROR;
}

bool isWritable( stdfs::path const& path )
{
  std::error_code ec;
  auto const st = stdfs::status( path, ec );
  return !ec && ( st.permissions() & stdfs::perms::owner_write ) != stdfs::perms::none;
}

bool equalsIgnoreCase( std::string const& a, std::string const& b )
{
  return a.size() == b.size() &&
         std::equal(
             a.begin(),
             a.end(),
             b.begin(),
             []( char x, char y )
             { return toUpper( static_cast<std::uint8_t>( x ) ) == toUpper( static_cast<std::uint8_t>( y ) ); } );
}

enum class ItemKind : std::uint8_t
{
  LABEL,
  DOT,
  DOTDOT,
  CHILD
};

/// An entry of a directory as f_readdir reads it.
struct Item
{
  ItemKind kind;
  std::uint32_t node;
};

/// A directory or file met: what FAT keeps in its directory entry.
struct Node
{
  std::uint32_t parent{};
  /// The host name, UTF-8; empty for the root.
  std::string name;
  /// For a directory outside the card: where it is.
  stdfs::path outside;
  bool isDir{};
  bool alive{ true };
  bool hasShort{};
  ShortName shortName{};
  /// Whether the entry has a long name, for one FatFs created.
  std::optional<bool> createdWithLfn;
  // A directory's entries, by host name, and in f_readdir's order.
  std::map<std::string, std::uint32_t> children;
  std::vector<Item> listing;
  bool scanned{};
};

struct OpenFile
{
  std::fstream stream;
  stdfs::path path;
  /// Opened by l_opencluster: reads past the file's end give the rest of its
  /// cluster, zeros here.
  bool byCluster{};
};

/// What trace_path found.
struct Traced
{
  /// The directory the last paragraph was looked up in.
  std::uint32_t dir{};
  /// False when that directory is FAT's cluster 0 reached through "..", which
  /// FatFs reads as no directory at all.
  bool dirValid{ true };
  /// The entry found; none with FR_OK for the root.
  std::optional<Item> item;
  DirName fn{};
  bool lfn{};
  /// The last paragraph, when it is a long name.
  std::string longName;
};

} // namespace

struct HostFs::State
{
  stdfs::path root;
  /// f_mount succeeded (fs_type is set).
  bool mounted{};
  /// The card was changed since (disk_status's STA_NOINIT).
  bool changed{ true };
  std::vector<Node> nodes;
  std::map<std::int32_t, OpenFile> files;
  std::int32_t nextHandle{ 1 };
  std::optional<ShortName> label;

  void reset()
  {
    files.clear();
    nodes.clear();
    nodes.resize( FIRST_NUMBER );
    nodes[0].isDir = true;
    for ( std::size_t i = 1; i < FIRST_NUMBER; ++i )
    {
      nodes[i].alive = false;
    }
    label.reset();
    auto normal = root.lexically_normal();
    if ( !normal.has_filename() )
      normal = normal.parent_path();
    auto const labelName = toUtf8( normal.filename() );
    if ( !labelName.empty() )
    {
      ShortName out;
      out.fill( ' ' );
      std::size_t n = 0;
      for ( auto const cp : decodeUtf8( labelName ) )
      {
        if ( n == out.size() )
          break;
        auto const c = cp < 0x80 ? toUpper( static_cast<std::uint8_t>( cp ) ) : std::uint8_t{ '_' };
        out.at( n++ ) = ( c == ' ' || isShortNameChar( c ) ) ? c : std::uint8_t{ '_' };
      }
      label = out;
    }
  }

  [[nodiscard]] bool exists( std::uint32_t n ) const
  {
    return n < nodes.size() && nodes[n].alive;
  }

  [[nodiscard]] stdfs::path pathOf( std::uint32_t n ) const
  {
    if ( !exists( n ) || root.empty() )
      return {};
    if ( n == 0 )
      return root;
    auto const& node = nodes[n];
    if ( !node.outside.empty() )
      return node.outside;
    auto parent = pathOf( node.parent );
    if ( parent.empty() )
      return {};
    return parent / fromUtf8( node.name );
  }

  std::uint32_t newNode( std::uint32_t parent, std::string name, bool isDir )
  {
    Node node;
    node.parent = parent;
    node.name = std::move( name );
    node.isDir = isDir;
    nodes.push_back( std::move( node ) );
    return static_cast<std::uint32_t>( nodes.size() - 1 );
  }

  /// Reads a directory from the host: its entries keep their numbers and
  /// short names, new ones are given theirs.
  void scan( std::uint32_t dir )
  {
    if ( dir >= nodes.size() )
      return;
    std::vector<std::pair<std::string, bool>> found;
    auto const path = pathOf( dir );
    if ( !path.empty() && nodes[dir].isDir )
    {
      std::error_code ec;
      for ( auto it = stdfs::directory_iterator( path, ec ); !ec && it != stdfs::directory_iterator();
            it.increment( ec ) )
      {
        std::error_code tec;
        bool const isDir = it->is_directory( tec );
        // FIFOs, sockets and devices are no files a card could hold.
        if ( !isDir && !it->is_regular_file( tec ) )
          continue;
        found.emplace_back( toUtf8( it->path().filename() ), isDir );
      }
    }
    std::ranges::sort( found );

    std::map<std::string, std::uint32_t> children;
    std::vector<std::uint32_t> ids;
    for ( auto const& [name, isDir] : found )
    {
      auto const old = nodes[dir].children.find( name );
      std::uint32_t id = 0;
      if ( old != nodes[dir].children.end() && nodes[old->second].isDir == isDir )
        id = old->second;
      else
        id = newNode( dir, name, isDir );
      children.emplace( name, id );
      ids.push_back( id );
    }
    for ( auto const& [name, id] : nodes[dir].children )
    {
      auto const now = children.find( name );
      if ( now == children.end() || now->second != id )
        nodes[id].alive = false;
    }
    nodes[dir].children = std::move( children );

    // Short names: a valid 8.3 name is its own; a name given before is kept
    // while it is free; the rest are made up, in name order.
    std::set<ShortName> used;
    std::vector<bool> done( ids.size() );
    for ( std::size_t i = 0; i < ids.size(); ++i )
    {
      auto& node = nodes[ids[i]];
      if ( auto const own = ownShortName( node.name ); own && used.insert( *own ).second )
      {
        node.shortName = *own;
        node.hasShort = true;
        done[i] = true;
      }
    }
    for ( std::size_t i = 0; i < ids.size(); ++i )
    {
      auto& node = nodes[ids[i]];
      if ( !done[i] && node.hasShort && used.insert( node.shortName ).second )
        done[i] = true;
    }
    for ( std::size_t i = 0; i < ids.size(); ++i )
    {
      auto& node = nodes[ids[i]];
      if ( !done[i] )
      {
        node.shortName = pcShortName( node.name, used );
        node.hasShort = true;
        node.createdWithLfn.reset();
        used.insert( node.shortName );
      }
    }

    std::vector<Item> listing;
    if ( dir == 0 )
    {
      if ( label )
        listing.push_back( Item{ .kind = ItemKind::LABEL, .node = 0 } );
    }
    else if ( nodes[dir].alive )
    {
      listing.push_back( Item{ .kind = ItemKind::DOT, .node = dir } );
      listing.push_back( Item{ .kind = ItemKind::DOTDOT, .node = dir } );
    }
    for ( auto const id : ids )
    {
      listing.push_back( Item{ .kind = ItemKind::CHILD, .node = id } );
    }
    nodes[dir].listing = std::move( listing );
    nodes[dir].scanned = true;
  }

  std::vector<Item> const& listingOf( std::uint32_t dir )
  {
    static std::vector<Item> const NONE;
    if ( dir >= nodes.size() )
      return NONE;
    if ( !nodes[dir].scanned )
      scan( dir );
    return nodes[dir].listing;
  }

  [[nodiscard]] ShortName shortNameOf( Item const& item ) const
  {
    switch ( item.kind )
    {
    case ItemKind::LABEL:
      return label.value_or( ShortName{} );
    case ItemKind::DOT:
      return DOT_NAME;
    case ItemKind::DOTDOT:
      return DOTDOT_NAME;
    case ItemKind::CHILD:
      break;
    }
    return nodes[item.node].shortName;
  }

  /// The long name FatFs would find in the LFN entries before the item, as
  /// the low bytes of its characters.
  [[nodiscard]] std::optional<std::string> longNameOf( Item const& item ) const
  {
    if ( item.kind != ItemKind::CHILD )
      return std::nullopt;
    auto const& node = nodes[item.node];
    bool hasLfn = false;
    if ( node.createdWithLfn )
    {
      hasLfn = *node.createdWithLfn;
    }
    else
    {
      auto const own = ownShortName( node.name );
      hasLfn = !own || *own != node.shortName || hasMixedCase( node.name );
    }
    if ( !hasLfn )
      return std::nullopt;
    return lowBytes( node.name );
  }

  /// get_fileinfo
  void fillInfo( std::uint32_t dir, Item const& item, FatFileInfo* fno ) const
  {
    formatShortName( shortNameOf( item ), fno->fname );
    fno->fsize = 0;
    switch ( item.kind )
    {
    case ItemKind::LABEL:
      fno->fattrib = AM_VOL;
      fno->clust = 0;
      fatTime( pathOf( 0 ), fno->fdate, fno->ftime );
      return;
    case ItemKind::DOT:
      fno->fattrib = AM_DIR;
      fno->clust = dir;
      fatTime( pathOf( dir ), fno->fdate, fno->ftime );
      return;
    case ItemKind::DOTDOT:
      fno->fattrib = AM_DIR;
      fno->clust = dir < nodes.size() ? nodes[dir].parent : 0;
      fatTime( pathOf( dir ), fno->fdate, fno->ftime );
      return;
    case ItemKind::CHILD:
      break;
    }
    auto const& node = nodes[item.node];
    auto const path = pathOf( item.node );
    fatTime( path, fno->fdate, fno->ftime );
    if ( node.isDir )
    {
      fno->fattrib = AM_DIR;
      fno->clust = item.node;
    }
    else
    {
      std::error_code ec;
      auto const size = stdfs::file_size( path, ec );
      fno->fsize = ec ? 0
                      : static_cast<std::uint32_t>(
                            std::min<std::uintmax_t>( size, std::numeric_limits<std::uint32_t>::max() ) );
      fno->fattrib = AM_ARC;
      if ( !isWritable( path ) )
        fno->fattrib |= AM_RDO;
      /* An empty file has no cluster */
      fno->clust = fno->fsize == 0 ? 0 : item.node;
    }
    /* macOS marks the files it hides by their name hidden on FAT */
    if ( !node.name.empty() && node.name[0] == '.' )
      fno->fattrib |= AM_HID;
  }

  [[nodiscard]] FatFileInfo infoOf( std::uint32_t dir, Item const& item ) const
  {
    FatFileInfo info{};
    fillInfo( dir, item, &info );
    return info;
  }

  [[nodiscard]] bool matches( Item const& item, Traced const& t ) const
  {
    if ( t.lfn )
    {
      /* A long name matches the LFN entries, case-insensitively */
      auto const lfn = longNameOf( item );
      return lfn && equalsIgnoreCase( *lfn, t.longName );
    }
    if ( item.kind == ItemKind::LABEL )
      return false;
    auto const name = shortNameOf( item );
    return std::equal( name.begin(), name.end(), t.fn.begin() );
  }

  /// trace_path, after auto_mount took a heading '/' away.
  FResult trace( std::uint32_t currDir, std::uint8_t const* path, Traced& t )
  {
    std::uint32_t dir = ( currDir == 0 || path[0] == '/' ) ? 0 : currDir;
    std::size_t start = 0;
    bool valid = true;

    t.dir = dir;
    t.item.reset();
    if ( *path == '\0' ) /* Null path means the root directory */
      return FResult::OK;

    for ( ;; )
    {
      auto const* spath = path;
      auto const ds = makeDirfile( path, t.fn, t.lfn ); /* Get a paragraph into fn[] */
      t.longName = t.lfn ? std::string( spath, path - 1 ) : std::string{};
      if ( ds == 1 )
        return FResult::INVALID_NAME;
      t.dir = dir;
      t.dirValid = valid;
      t.item.reset();
      if ( valid )
      {
        scan( dir );
        auto const& items = nodes[dir].listing;
        for ( std::size_t i = start; i < items.size(); ++i )
        {
          if ( matches( items[i], t ) )
          {
            t.item = items[i];
            break;
          }
        }
      }
      if ( !t.item )
        return ds == 0 ? FResult::NO_FILE : FResult::NO_PATH;
      if ( ds == 0 ) /* Matched with end of path */
        return FResult::OK;
      auto const info = infoOf( dir, *t.item );
      if ( ( info.fattrib & AM_DIR ) == 0 ) /* Cannot trace because it is a file */
        return FResult::NO_PATH;
      dir = info.clust; /* Restart scanning at the new directory */
      start = 2;
      valid = dir != 0 && exists( dir );
    }
  }

  /// auto_mount
  [[nodiscard]] FResult autoMount( std::uint8_t chkWp ) const
  {
    if ( !mounted || changed )
      return FResult::NOT_READY;
    if ( chkWp != 0 && !isWritable( root ) ) /* Check write protection if needed */
      return FResult::WRITE_PROTECTED;
    return FResult::OK;
  }

  /// validate
  [[nodiscard]] FResult validate() const
  {
    if ( !mounted )
      return FResult::INVALID_OBJECT;
    if ( changed )
      return FResult::NOT_READY;
    return FResult::OK;
  }

  OpenFile* fileOf( FatFile const* fp )
  {
    if ( fp->handle <= 0 )
      return nullptr;
    auto const it = files.find( fp->handle );
    return it == files.end() ? nullptr : &it->second;
  }

  /// FatFs overwrites a FIL it opens into. A file opened by cluster is only
  /// ever read for its header, and left open; close it here, so that it does
  /// not keep a host file open.
  void dropClusterFile( FatFile const* fp )
  {
    if ( auto const* of = fileOf( fp ); of != nullptr && of->byCluster )
      files.erase( fp->handle );
  }

  std::int32_t addFile( OpenFile file )
  {
    auto const handle = nextHandle++;
    files.emplace( handle, std::move( file ) );
    return handle;
  }

  /// The host name, short name and kind of a new entry for the last paragraph
  /// traced (add_direntry). Returns false when no short name is free.
  bool newEntry( Traced const& t, std::string& hostName, ShortName& sfn )
  {
    if ( !t.lfn )
    {
      std::copy_n( t.fn.begin(), sfn.size(), sfn.begin() );
      hostName = latin1ToUtf8( shortNameString( sfn ) );
      return true;
    }
    hostName = latin1ToUtf8( t.longName );
    std::set<ShortName> used;
    for ( auto const& item : listingOf( t.dir ) )
    {
      if ( item.kind != ItemKind::LABEL )
        used.insert( shortNameOf( item ) );
    }
    sfn = createShortName( t.longName );
    for ( ;; )
    {
      if ( !used.contains( sfn ) ) /* chk_filename */
        break;
      if ( !fixShortName( sfn ) )
        return false;
    }
    return true;
  }

  /// Records an entry FatFs created, before the host lists it.
  std::uint32_t addEntry( Traced const& t, std::string const& hostName, ShortName const& sfn, bool isDir )
  {
    auto const id = newNode( t.dir, hostName, isDir );
    nodes[id].shortName = sfn;
    nodes[id].hasShort = true;
    nodes[id].createdWithLfn = t.lfn;
    nodes[t.dir].children[hostName] = id;
    scan( t.dir );
    return id;
  }

  /// The number of a host directory under the root, or none.
  std::optional<std::uint32_t> findUnderRoot( stdfs::path const& relative )
  {
    std::uint32_t id = 0;
    for ( auto const& part : relative )
    {
      auto const name = toUtf8( part );
      if ( name.empty() || name == "." )
        continue;
      scan( id );
      auto const& children = nodes[id].children;
      auto it = children.find( name );
      if ( it == children.end() )
        it = std::ranges::find_if( children, [&]( auto const& c ) { return equalsIgnoreCase( c.first, name ); } );
      if ( it == children.end() || !nodes[it->second].isDir )
        return std::nullopt;
      id = it->second;
    }
    return id;
  }
};

HostFs::HostFs() : mState{ std::make_unique<State>() }
{
  mState->reset();
}

HostFs::~HostFs() = default;

void HostFs::setRoot( std::filesystem::path root )
{
  mState->root = std::move( root );
  mState->reset();
  mState->changed = true;
}

std::filesystem::path const& HostFs::root() const
{
  return mState->root;
}

bool HostFs::present() const
{
  return !mState->root.empty();
}

std::uint32_t HostFs::clusterOf( std::filesystem::path const& directory )
{
  auto& s = *mState;
  std::error_code ec;
  auto const target = stdfs::weakly_canonical( stdfs::absolute( directory, ec ), ec );
  auto const root = stdfs::weakly_canonical( stdfs::absolute( s.root, ec ), ec );
  if ( !s.root.empty() )
  {
    auto const relative = target.lexically_relative( root );
    if ( !relative.empty() && *relative.begin() != ".." )
    {
      if ( auto const id = s.findUnderRoot( relative ) )
        return *id;
    }
  }
  for ( std::uint32_t id = FIRST_NUMBER; id < s.nodes.size(); ++id )
  {
    if ( s.nodes[id].alive && s.nodes[id].outside == target )
      return id;
  }
  /* Outside the card: its ".." is the root */
  auto const id = s.newNode( 0, toUtf8( target.filename() ), true );
  s.nodes[id].outside = target;
  return id;
}

std::filesystem::path HostFs::pathOf( std::uint32_t cluster ) const
{
  return mState->pathOf( cluster );
}

std::filesystem::path HostFs::pathOfFile( FatFile const* fp ) const
{
  if ( fp->handle <= 0 )
    return {};
  auto const it = mState->files.find( fp->handle );
  return it == mState->files.end() ? std::filesystem::path{} : it->second.path;
}

std::string HostFs::shortNameOf( std::filesystem::path const& file )
{
  auto& s = *mState;
  auto const dir = clusterOf( file.parent_path() );
  s.scan( dir );
  auto const name = toUtf8( file.filename() );
  auto const& children = s.nodes[dir].children;
  auto it = children.find( name );
  if ( it == children.end() )
    it = std::ranges::find_if( children, [&]( auto const& c ) { return equalsIgnoreCase( c.first, name ); } );
  if ( it == children.end() )
    return {};
  return shortNameString( s.nodes[it->second].shortName );
}

FResult HostFs::fMount( FatVolume* fs )
{
  auto& s = *mState;
  *fs = FatVolume{}; /* Clean-up the file system object */
  s.mounted = false;
  if ( !present() )
    return FResult::NOT_READY;
  std::error_code ec;
  if ( !stdfs::is_directory( s.root, ec ) )
    return FResult::NOT_READY;

  auto const space = stdfs::space( s.root, ec );
  auto const clusters =
      std::clamp<std::uint64_t>( ec ? 0 : space.capacity / CLUSTER_BYTES, MIN_CLUSTERS, MAX_CLUSTERS );
  fs->fatbase = PARTITION_START + RESERVED_SECTORS;
  fs->csize = CLUSTER_SECTORS;
  fs->maxClust = static_cast<std::uint32_t>( clusters + 2 );
  fs->fsType = FS_FAT32;
  s.mounted = true;
  s.changed = false;
  return FResult::OK;
}

FResult HostFs::fOpen( FatVolume* fs, FatFile* fp, std::uint8_t const* path, std::uint8_t mode )
{
  auto& s = *mState;
  s.dropClusterFile( fp );
  fp->handle = 0; /* Clear file object */
  mode &= FA_READ | FA_WRITE | FA_CREATE_ALWAYS | FA_OPEN_ALWAYS | FA_CREATE_NEW;
  auto res = s.autoMount( mode & ( FA_WRITE | FA_CREATE_ANY ) );
  if ( res != FResult::OK )
    return res;
  if ( *path == '/' ) /* Strip heading slash */
    path++;

  Traced t;
  res = s.trace( fs->currDir, path, t ); /* Trace the file path */
  OpenFile file;
  std::uint32_t size = 0;
  bool truncate = false;
  if ( ( mode & FA_CREATE_ANY ) != 0 )
  {
    if ( res != FResult::OK )
    { /* No file, create new */
      if ( res != FResult::NO_FILE )
        return res;
      if ( !t.dirValid )
        return FResult::DENIED;
      std::string hostName;
      ShortName sfn{};
      if ( !s.newEntry( t, hostName, sfn ) )
        return FResult::RW_ERROR;
      auto const dirPath = s.pathOf( t.dir );
      file.path = dirPath / fromUtf8( hostName );
      std::error_code ec;
      if ( dirPath.empty() || stdfs::exists( stdfs::symlink_status( file.path, ec ) ) )
        return FResult::DENIED;
      file.stream.open( file.path, std::ios::in | std::ios::out | std::ios::binary | std::ios::trunc );
      if ( !file.stream.is_open() )
        return isWritable( dirPath ) ? FResult::DENIED : FResult::WRITE_PROTECTED;
      s.addEntry( t, hostName, sfn, false );
      mode |= FA_CREATE_ALWAYS;
    }
    else
    {                                      /* Any object is already existing */
      if ( ( mode & FA_CREATE_NEW ) != 0 ) /* Cannot create new */
        return FResult::EXIST;
      if ( !t.item ) /* Cannot overwrite it */
        return FResult::DENIED;
      auto const info = s.infoOf( t.dir, *t.item );
      if ( ( info.fattrib & AM_DIR ) != 0 )
        return FResult::IS_DIRECTORY;
      if ( ( info.fattrib & AM_RDO ) != 0 )
        return FResult::IS_READONLY;
      truncate = ( mode & FA_CREATE_ALWAYS ) != 0; /* Resize it to zero if needed */
      size = truncate ? 0 : info.fsize;
      file.path = s.pathOf( t.item->node );
      auto openMode = std::ios::in | std::ios::out | std::ios::binary;
      if ( truncate )
        openMode |= std::ios::trunc;
      file.stream.open( file.path, openMode );
      if ( !file.stream.is_open() )
        return FResult::DENIED;
    }
    if ( ( mode & FA_CREATE_ALWAYS ) != 0 )
      mode |= FA_WRITTEN; /* Set file changed flag */
  }
  else
  { /* Open an existing file */
    if ( res != FResult::OK )
      return res; /* Trace failed */
    if ( !t.item )
      return FResult::NO_FILE;
    auto const info = s.infoOf( t.dir, *t.item );
    if ( ( info.fattrib & AM_DIR ) != 0 ) /* It is a directory */
      return FResult::NO_FILE;
    if ( ( mode & FA_WRITE ) != 0 && ( info.fattrib & AM_RDO ) != 0 ) /* R/O violation */
      return FResult::DENIED;
    size = info.fsize;
    file.path = s.pathOf( t.item->node );
    auto openMode = std::ios::in | std::ios::binary;
    if ( ( mode & FA_WRITE ) != 0 )
      openMode |= std::ios::out;
    file.stream.open( file.path, openMode );
    if ( !file.stream.is_open() )
      return ( mode & FA_WRITE ) != 0 ? FResult::DENIED : FResult::RW_ERROR;
  }

  fp->handle = s.addFile( std::move( file ) );
  fp->flag = mode; /* File access mode */
  fp->fsize = size;
  fp->fptr = 0; /* Initialize file pointer */
  return FResult::OK;
}

FResult HostFs::lOpencluster( FatVolume* fs, FatFile* fp, std::uint32_t clust )
{
  auto& s = *mState;
  s.dropClusterFile( fp );
  OpenFile file;
  file.byCluster = true;
  if ( s.exists( clust ) && !s.nodes[clust].isDir )
  {
    file.path = s.pathOf( clust );
    file.stream.open( file.path, std::ios::in | std::ios::binary );
  }
  fp->handle = s.addFile( std::move( file ) );
  fp->flag = FA_READ;
  fp->fsize = std::uint32_t{ fs->csize } * SECTOR_SIZE;
  fp->fptr = 0;
  return FResult::OK;
}

FResult HostFs::fRead( FatFile* fp, void* buff, unsigned btr, unsigned* br )
{
  auto& s = *mState;
  *br = 0;
  auto* file = s.fileOf( fp );
  if ( file == nullptr )
    return FResult::INVALID_OBJECT;
  auto const res = s.validate(); /* Check validity of the object */
  if ( res != FResult::OK )
    return res;
  if ( ( fp->flag & FA_ERROR ) != 0 ) /* Check error flag */
    return FResult::RW_ERROR;
  if ( ( fp->flag & FA_READ ) == 0 ) /* Check access mode */
    return FResult::DENIED;
  auto const remain = fp->fsize - fp->fptr;
  btr = std::min( btr, remain ); /* Truncate read count by number of bytes left */
  if ( btr == 0 )
    return FResult::OK;
  if ( !file->stream.is_open() )
  {
    fp->flag |= FA_ERROR;
    return FResult::RW_ERROR;
  }

  auto* data = static_cast<char*>( buff );
  file->stream.clear();
  file->stream.seekg( fp->fptr );
  std::streamsize got = 0;
  if ( file->stream )
  {
    file->stream.read( data, btr );
    got = file->stream.gcount();
  }
  file->stream.clear();
  // FAT holds the size in the directory entry: what the host file lacks of
  // it reads as zeros.
  std::fill( data + got, data + btr, '\0' );
  fp->fptr += btr;
  *br = btr;
  return FResult::OK;
}

FResult HostFs::fWrite( FatFile* fp, void const* buff, unsigned btw, unsigned* bw )
{
  auto& s = *mState;
  *bw = 0;
  auto* file = s.fileOf( fp );
  if ( file == nullptr )
    return FResult::INVALID_OBJECT;
  auto const res = s.validate(); /* Check validity of the object */
  if ( res != FResult::OK )
    return res;
  if ( ( fp->flag & FA_ERROR ) != 0 ) /* Check error flag */
    return FResult::RW_ERROR;
  if ( ( fp->flag & FA_WRITE ) == 0 ) /* Check access mode */
    return FResult::DENIED;
  if ( fp->fsize + btw < fp->fsize ) /* File size cannot reach 4GB */
    return FResult::OK;
  if ( btw == 0 )
    return FResult::OK;

  file->stream.clear();
  file->stream.seekp( fp->fptr );
  file->stream.write( static_cast<char const*>( buff ), btw );
  file->stream.flush();
  if ( !file->stream )
  {
    // FatFs stops where it finds no free cluster and answers FR_OK with what
    // it wrote: the host wrote nothing it can vouch for.
    file->stream.clear();
    return FResult::OK;
  }
  fp->fptr += btw;
  *bw = btw;
  fp->fsize = std::max( fp->fsize, fp->fptr ); /* Update file size if needed */
  fp->flag |= FA_WRITTEN;                      /* Set file changed flag */
  return FResult::OK;
}

FResult HostFs::fSync( FatFile* fp )
{
  auto& s = *mState;
  auto* file = s.fileOf( fp );
  if ( file == nullptr )
    return FResult::INVALID_OBJECT;
  auto const res = s.validate(); /* Check validity of the object */
  if ( res != FResult::OK || ( fp->flag & FA_WRITTEN ) == 0 )
    return res;

  /* A seek past the end made the file longer */
  file->stream.clear();
  file->stream.seekp( 0, std::ios::end );
  auto const end = static_cast<std::uint32_t>( std::max<std::streamoff>( 0, file->stream.tellp() ) );
  if ( end < fp->fsize )
  {
    std::vector<char> const zeros( fp->fsize - end );
    file->stream.write( zeros.data(), static_cast<std::streamsize>( zeros.size() ) );
  }
  file->stream.flush();
  if ( !file->stream )
  {
    file->stream.clear();
    return FResult::RW_ERROR;
  }
  fp->flag &= static_cast<std::uint8_t>( ~FA_WRITTEN );
  return FResult::OK;
}

FResult HostFs::fClose( FatFile* fp )
{
  auto const res = fSync( fp );
  if ( res == FResult::OK )
  {
    mState->files.erase( fp->handle );
    fp->handle = 0;
  }
  return res;
}

FResult HostFs::fLseek( FatFile* fp, std::uint32_t ofs )
{
  auto& s = *mState;
  if ( s.fileOf( fp ) == nullptr )
    return FResult::INVALID_OBJECT;
  auto const res = s.validate(); /* Check validity of the object */
  if ( res != FResult::OK )
    return res;
  if ( ( fp->flag & FA_ERROR ) != 0 )
    return FResult::RW_ERROR;
  if ( fp->fptr == ofs ) /* Don't seek if the target is the current position */
    return FResult::OK;
  if ( ofs > fp->fsize && ( fp->flag & FA_WRITE ) == 0 ) /* In read-only mode, clip offset with the file size */
    ofs = fp->fsize;
  fp->fptr = ofs;
  if ( fp->fptr > fp->fsize )
  { /* Set changed flag if the file was extended */
    fp->fsize = fp->fptr;
    fp->flag |= FA_WRITTEN;
  }
  return FResult::OK;
}

FResult HostFs::lOpendir( FatVolume* /*fs*/, std::uint32_t cluster, FatDir* dirobj )
{
  dirobj->cluster = cluster;
  dirobj->index = 0;
  if ( mState->mounted )
    mState->scan( cluster );
  return FResult::OK;
}

FResult HostFs::fReaddir( FatDir* dj, FatFileInfo* fno )
{
  auto& s = *mState;
  if ( fno->lfn != nullptr )
    fno->lfn[0] = 0; /* set first char to null */

  auto const res = s.validate(); /* Check validity of the object */
  if ( res != FResult::OK )
    return res;

  fno->fname[0] = 0;
  auto const& items = s.listingOf( dj->cluster );
  if ( dj->index >= items.size() ) /* Has it reached to end of dir? */
    return FResult::OK;
  auto const item = items[dj->index++];
  s.fillInfo( dj->cluster, item, fno );
  if ( fno->lfn != nullptr )
  {
    // A long name that does not fit _MAX_LFN_LENGTH is skipped as a whole.
    if ( auto const lfn = s.longNameOf( item ); lfn && lfn->size() <= MAX_LFN_LENGTH )
    {
      std::ranges::copy( *lfn, fno->lfn );
      fno->lfn[lfn->size()] = 0;
    }
  }
  return FResult::OK;
}

FResult HostFs::fStat( FatVolume* fs, std::uint8_t const* path, FatFileInfo* fno )
{
  auto& s = *mState;
  auto res = s.autoMount( 0 );
  if ( res != FResult::OK )
    return res;
  if ( *path == '/' )
    path++;
  Traced t;
  res = s.trace( fs->currDir, path, t ); /* trace the file path */
  if ( res == FResult::OK )
  { /* Trace completed */
    if ( t.item )
      s.fillInfo( t.dir, *t.item, fno ); /* Found an object */
    else
      res = FResult::INVALID_NAME; /* It is root dir */
  }
  return res;
}

FResult HostFs::fUnlink( FatVolume* fs, std::uint8_t const* path )
{
  auto& s = *mState;
  auto res = s.autoMount( 1 );
  if ( res != FResult::OK )
    return res;
  if ( *path == '/' )
    path++;
  Traced t;
  res = s.trace( fs->currDir, path, t ); /* trace the file path */
  if ( res != FResult::OK )              /* Trace failed */
    return res;
  if ( !t.item ) /* It is the root directory */
    return FResult::INVALID_NAME;
  auto const info = s.infoOf( t.dir, *t.item );
  if ( ( info.fattrib & AM_RDO ) != 0 ) /* It is a R/O object */
    return FResult::DENIED;

  if ( ( info.fattrib & AM_DIR ) != 0 )
  { /* It is a sub-directory */
    auto const dclust = info.clust;
    if ( dclust == fs->currDir ) /* Never delete the current directory */
      return FResult::DIR_NOT_EMPTY;
    // FatFs would read cluster 0 of a ".." as a directory: it is the root,
    // which is never empty enough to go.
    if ( dclust == 0 || !s.exists( dclust ) )
      return FResult::DIR_NOT_EMPTY;
    s.scan( dclust ); /* Check if the sub-dir is empty or not */
    auto const& items = s.nodes[dclust].listing;
    if ( std::any_of( items.begin() + std::min<std::ptrdiff_t>( 2, std::ssize( items ) ),
                      items.end(),
                      []( Item const& item ) { return item.kind != ItemKind::LABEL; } ) )
      return FResult::DIR_NOT_EMPTY; /* The directory is not empty */
  }

  auto const node = t.item->node;
  std::error_code ec;
  stdfs::remove( s.pathOf( node ), ec );
  if ( ec )
    return hostError( ec );
  s.nodes[node].alive = false;
  s.scan( t.dir );
  return FResult::OK;
}

FResult HostFs::fMkdir( FatVolume* fs, std::uint8_t const* path )
{
  auto& s = *mState;
  auto res = s.autoMount( 1 );
  if ( res != FResult::OK )
    return res;
  if ( *path == '/' )
    path++;
  Traced t;
  res = s.trace( fs->currDir, path, t ); /* trace the file path */
  if ( res == FResult::OK )              /* Any file or directory is already existing */
    return FResult::EXIST;
  if ( res != FResult::NO_FILE )
    return res;
  if ( !t.dirValid )
    return FResult::DENIED;

  std::string hostName;
  ShortName sfn{};
  if ( !s.newEntry( t, hostName, sfn ) )
    return FResult::RW_ERROR;
  auto const dirPath = s.pathOf( t.dir );
  if ( dirPath.empty() )
    return FResult::DENIED;
  std::error_code ec;
  if ( !stdfs::create_directory( dirPath / fromUtf8( hostName ), ec ) )
    return ec ? hostError( ec ) : FResult::DENIED;
  s.addEntry( t, hostName, sfn, true );
  return FResult::OK;
}

FResult HostFs::fRename( FatVolume* fs, std::uint8_t const* pathOld, std::uint8_t const* pathNew )
{
  auto& s = *mState;
  auto res = s.autoMount( 1 );
  if ( res != FResult::OK )
    return res;
  if ( *pathOld == '/' )
    pathOld++;

  Traced told;
  res = s.trace( fs->currDir, pathOld, told ); /* trace the file path */
  if ( res != FResult::OK )                    /* The old object is not found */
    return res;
  if ( !told.item )
    return FResult::NO_FILE;
  // FatFs would make a second entry for the directory and lose its "." or
  // "..": refused, which a FAT card is better off with.
  if ( told.item->kind != ItemKind::CHILD )
    return FResult::DENIED;

  Traced tnew;
  res = s.trace( fs->currDir, pathNew, tnew ); /* trace the file path */
  if ( res == FResult::OK )                    /* The new object name is already existing */
    return FResult::EXIST;
  if ( res != FResult::NO_FILE ) /* Is there no old name? */
    return res;
  if ( !tnew.dirValid )
    return FResult::DENIED;

  std::string hostName;
  ShortName sfn{};
  if ( !s.newEntry( tnew, hostName, sfn ) ) /* need to get shortname, check it, and add all names */
    return FResult::RW_ERROR;
  auto const node = told.item->node;
  auto const dirPath = s.pathOf( tnew.dir );
  if ( dirPath.empty() )
    return FResult::DENIED;
  std::error_code ec;
  stdfs::rename( s.pathOf( node ), dirPath / fromUtf8( hostName ), ec );
  if ( ec )
    return hostError( ec );

  s.nodes[s.nodes[node].parent].children.erase( s.nodes[node].name );
  s.nodes[node].parent = tnew.dir;
  s.nodes[node].name = hostName;
  s.nodes[node].shortName = sfn;
  s.nodes[node].hasShort = true;
  s.nodes[node].createdWithLfn = tnew.lfn;
  s.nodes[tnew.dir].children[hostName] = node;
  s.scan( told.dir );
  s.scan( tnew.dir );
  return FResult::OK;
}

FResult HostFs::lGetfree( FatVolume* fs, std::uint8_t const* /*path*/, std::uint32_t* nclust, std::uint32_t maxclust )
{
  auto& s = *mState;
  auto const res = s.autoMount( 0 );
  if ( res != FResult::OK )
    return res;
  std::error_code ec;
  auto const space = stdfs::space( s.root, ec );
  if ( ec )
    return FResult::RW_ERROR;
  auto n = std::min<std::uint64_t>( space.available / CLUSTER_BYTES, fs->maxClust - 2 );
  if ( maxclust != 0 && n >= maxclust )
    n = maxclust;
  *nclust = static_cast<std::uint32_t>( n );
  return FResult::OK;
}

} // namespace pet::sd2pet
