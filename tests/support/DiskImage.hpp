#pragma once

// D64 images for the drive's tests, built here rather than carried in the
// tree: 35 tracks, the BAM and the directory on track 18, each file's sectors
// chained from track 17 down, as c1541 lays a fresh disk out.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pet::test
{

/// A PRG file: its name, PETSCII, and its bytes, the load address first.
struct DiskFile
{
  std::string name;
  std::vector<std::uint8_t> bytes;
};

/// A D64 named `name` with ID `id`, holding `files`.
std::vector<std::uint8_t> makeD64( std::string const& name, std::string const& id, std::vector<DiskFile> const& files );

/// The BASIC program the drive's tests load and run, at $0401:
/// 10 PRINT"HELLO FROM DISK"
std::vector<std::uint8_t> helloProgram();

/// hello.d64: "PETMCP TEST", ID PM, holding HELLO and SECOND, each the
/// program above.
std::vector<std::uint8_t> helloDisk();

} // namespace pet::test
