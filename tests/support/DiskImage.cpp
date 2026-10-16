#include "support/DiskImage.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>

namespace pet::test
{

namespace
{

constexpr unsigned TRACKS = 35;
constexpr unsigned DIRECTORY_TRACK = 18;
constexpr std::size_t SECTOR = 256;
/// What a name or a label is padded with.
constexpr std::uint8_t PAD = 0xa0;

unsigned sectorsOf( unsigned track )
{
  if ( track <= 17 )
  {
    return 21;
  }
  if ( track <= 24 )
  {
    return 19;
  }
  return track <= 30 ? 18 : 17;
}

std::size_t offsetOf( unsigned track, unsigned sector )
{
  std::size_t offset = 0;
  for ( unsigned before = 1; before < track; ++before )
  {
    offset += sectorsOf( before ) * SECTOR;
  }
  return offset + ( sector * SECTOR );
}

class Disk
{
public:
  Disk() : mBytes( offsetOf( TRACKS + 1, 0 ) ), mUsed( TRACKS + 1 ) {}

  std::uint8_t* sector( unsigned track, unsigned sector )
  {
    return mBytes.data() + offsetOf( track, sector );
  }

  void use( unsigned track, unsigned sector )
  {
    mUsed.at( track ).at( sector ) = true;
  }

  /// The next free sector of a file, from track 17 down and then from 19 up,
  /// 10 sectors on within a track, as the DOS lays a file out.
  std::pair<unsigned, unsigned> allocate()
  {
    for ( unsigned const track : tracksInOrder() )
    {
      for ( unsigned step = 0; step < sectorsOf( track ); ++step )
      {
        unsigned const candidate = ( mNextSector + ( step * 10 ) ) % sectorsOf( track );
        if ( !mUsed.at( track ).at( candidate ) )
        {
          use( track, candidate );
          mNextSector = candidate + 10;
          return { track, candidate };
        }
      }
    }
    throw std::runtime_error{ "the disk is full" };
  }

  /// The BAM's entries, 4 bytes a track, from offset 4 of 18/0.
  void writeBam()
  {
    std::uint8_t* const bam = sector( DIRECTORY_TRACK, 0 );
    for ( unsigned track = 1; track <= TRACKS; ++track )
    {
      std::uint8_t* const entry = bam + ( std::size_t{ 4 } * track );
      unsigned free = 0;
      std::array<std::uint8_t, 3> bits{};
      for ( unsigned sector = 0; sector < sectorsOf( track ); ++sector )
      {
        if ( !mUsed.at( track ).at( sector ) )
        {
          ++free;
          bits.at( sector / 8 ) |= static_cast<std::uint8_t>( 1U << ( sector % 8 ) );
        }
      }
      entry[0] = static_cast<std::uint8_t>( free );
      entry[1] = bits[0];
      entry[2] = bits[1];
      entry[3] = bits[2];
    }
  }

  std::vector<std::uint8_t> take()
  {
    return std::move( mBytes );
  }

private:
  static std::vector<unsigned> tracksInOrder()
  {
    std::vector<unsigned> order;
    for ( unsigned distance = 1; distance < DIRECTORY_TRACK; ++distance )
    {
      order.push_back( DIRECTORY_TRACK - distance );
      if ( DIRECTORY_TRACK + distance <= TRACKS )
      {
        order.push_back( DIRECTORY_TRACK + distance );
      }
    }
    return order;
  }

  std::vector<std::uint8_t> mBytes;
  std::vector<std::array<bool, 21>> mUsed;
  unsigned mNextSector{};
};

void padded( std::uint8_t* to, std::string const& text, std::size_t size )
{
  for ( std::size_t at = 0; at < size; ++at )
  {
    to[at] = at < text.size() ? static_cast<std::uint8_t>( text[at] ) : PAD;
  }
}

} // namespace

std::vector<std::uint8_t> makeD64( std::string const& name, std::string const& id, std::vector<DiskFile> const& files )
{
  Disk disk;
  disk.use( DIRECTORY_TRACK, 0 );
  disk.use( DIRECTORY_TRACK, 1 );

  std::uint8_t* const bam = disk.sector( DIRECTORY_TRACK, 0 );
  bam[0] = DIRECTORY_TRACK;
  bam[1] = 1;
  bam[2] = 'A';
  padded( bam + 0x90, name, 16 );
  bam[0xa0] = PAD;
  bam[0xa1] = PAD;
  padded( bam + 0xa2, id, 2 );
  bam[0xa4] = PAD;
  bam[0xa5] = '2';
  bam[0xa6] = 'A';
  padded( bam + 0xa7, "", 4 );

  std::uint8_t* const directory = disk.sector( DIRECTORY_TRACK, 1 );
  directory[1] = 0xff;
  if ( files.size() > 8 )
  {
    throw std::runtime_error{ "more files than one directory sector holds" };
  }
  for ( std::size_t index = 0; index < files.size(); ++index )
  {
    DiskFile const& file = files[index];
    std::uint8_t* const entry = directory + ( index * 32 );
    std::size_t const blocks = ( file.bytes.size() + 253 ) / 254;
    std::pair<unsigned, unsigned> place = disk.allocate();
    entry[2] = 0x82; // PRG, closed
    entry[3] = static_cast<std::uint8_t>( place.first );
    entry[4] = static_cast<std::uint8_t>( place.second );
    padded( entry + 5, file.name, 16 );
    entry[30] = static_cast<std::uint8_t>( blocks & 0xffU );
    entry[31] = static_cast<std::uint8_t>( blocks >> 8U );
    for ( std::size_t block = 0; block < blocks; ++block )
    {
      std::uint8_t* const data = disk.sector( place.first, place.second );
      std::size_t const start = block * 254;
      std::size_t const length = std::min<std::size_t>( 254, file.bytes.size() - start );
      for ( std::size_t at = 0; at < length; ++at )
      {
        data[2 + at] = file.bytes[start + at];
      }
      if ( block + 1 < blocks )
      {
        place = disk.allocate();
        data[0] = static_cast<std::uint8_t>( place.first );
        data[1] = static_cast<std::uint8_t>( place.second );
      }
      else
      {
        data[0] = 0;
        data[1] = static_cast<std::uint8_t>( length + 1 );
      }
    }
  }
  disk.writeBam();
  return disk.take();
}

std::vector<std::uint8_t> helloProgram()
{
  std::string const line = "\x99\"HELLO FROM DISK\"";
  std::vector<std::uint8_t> program{ 0x01, 0x04 };
  unsigned const next = 0x0401 + 4 + static_cast<unsigned>( line.size() ) + 1;
  program.insert( program.end(),
                  { static_cast<std::uint8_t>( next & 0xffU ), static_cast<std::uint8_t>( next >> 8U ), 10, 0 } );
  program.insert( program.end(), line.begin(), line.end() );
  program.insert( program.end(), { 0, 0, 0 } );
  return program;
}

std::vector<std::uint8_t> helloDisk()
{
  return makeD64( "PETMCP TEST",
                  "PM",
                  { { .name = "HELLO", .bytes = helloProgram() }, { .name = "SECOND", .bytes = helloProgram() } } );
}

} // namespace pet::test
