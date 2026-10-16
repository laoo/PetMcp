#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace pet::machine
{

/// The 3032's ROMs, read from a directory under the names VICE gives them
/// (docs/architecture.md, "The machine").
struct Roms
{
  static constexpr std::string_view BASIC_FILE = "basic-2.901465-01-02.bin";
  static constexpr std::string_view EDITOR_FILE = "edit-2-n.901447-24.bin";
  static constexpr std::string_view KERNAL_FILE = "kernal-2.901465-03.bin";
  static constexpr std::string_view CHARACTERS_FILE = "characters-2.901447-10.bin";

  /// BASIC 2 at $C000, 8 KB.
  std::vector<std::uint8_t> basic;
  /// The screen editor at $E000, 2 KB.
  std::vector<std::uint8_t> editor;
  /// The kernal at $F000, 4 KB.
  std::vector<std::uint8_t> kernal;
  /// The character generator, 2 KB: the graphics set, then the text set.
  std::vector<std::uint8_t> characters;

  /// The four from `directory`, or why they cannot be had: a file missing or
  /// of the wrong size, named.
  static std::expected<Roms, std::string> load( std::filesystem::path const& directory );
};

} // namespace pet::machine
