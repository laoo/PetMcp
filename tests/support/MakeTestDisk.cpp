// Writes the drive's test disk, hello.d64, into the directory given, for the
// protocol's disk transcript (tests/control/disk.jsonl).

#include "support/DiskImage.hpp"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>

int main( int argc, char** argv )
{
  if ( argc != 2 )
  {
    std::cerr << "usage: pet_test_disk DIRECTORY\n";
    return 2;
  }
  try
  {
    std::filesystem::path const directory{ argv[1] };
    std::filesystem::create_directories( directory );
    std::vector<std::uint8_t> const disk = pet::test::helloDisk();
    std::ofstream file{ directory / "hello.d64", std::ios::binary };
    file.write( reinterpret_cast<char const*>( disk.data() ), static_cast<std::streamsize>( disk.size() ) );
    if ( !file )
    {
      std::cerr << "cannot write " << ( directory / "hello.d64" ).string() << '\n';
      return 1;
    }
  }
  catch ( std::exception const& error )
  {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
