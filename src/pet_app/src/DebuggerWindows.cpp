#include "DebuggerWindows.hpp"

#include <imgui.h>
#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <utility>

namespace pet::app
{

namespace
{

/// The answer's result when it succeeded, null otherwise.
control::Json resultOf( control::Json const& response )
{
  return response.at( "ok" ) == true ? response.at( "result" ) : control::Json{};
}

std::string errorOf( control::Json const& response )
{
  return response.at( "ok" ) == true ? std::string{} : response.at( "error" ).at( "message" ).get<std::string>();
}

/// A 16-bit address typed as hex.
bool inputAddress( char const* label, std::uint16_t& address )
{
  ImGui::SetNextItemWidth( ImGui::CalcTextSize( "FFFF__" ).x + ( ImGui::GetStyle().FramePadding.x * 2 ) );
  return ImGui::InputScalar( label,
                             ImGuiDataType_U16,
                             &address,
                             nullptr,
                             nullptr,
                             "%04X",
                             ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue );
}

} // namespace

CpuWindow::CpuWindow( Requester request ) : mRequest{ std::move( request ) } {}

void CpuWindow::draw( bool& open )
{
  if ( !open )
  {
    return;
  }
  if ( !ImGui::Begin( "CPU", &open ) )
  {
    ImGui::End();
    return;
  }
  control::Json const state = resultOf( mRequest( "cpu.get_state", control::Json::object() ) );
  if ( state.is_null() )
  {
    ImGui::TextUnformatted( "The PET is not running" );
    ImGui::End();
    return;
  }

  bool const paused = resultOf( mRequest( "emu.status", control::Json::object() ) ).value( "paused", false );
  if ( ImGui::Button( paused ? "Run" : "Pause" ) )
  {
    static_cast<void>( mRequest( paused ? "emu.resume" : "emu.pause", control::Json::object() ) );
  }
  ImGui::SameLine();
  ImGui::BeginDisabled( !paused );
  if ( ImGui::Button( "Step" ) )
  {
    static_cast<void>( mRequest( "emu.step", control::Json::object() ) );
  }
  ImGui::SameLine();
  if ( ImGui::Button( "Step frame" ) )
  {
    static_cast<void>( mRequest( "emu.run_frames", { { "count", 1 } } ) );
  }
  ImGui::EndDisabled();

  auto const byte = [&state]( char const* name ) { return state.at( name ).get<unsigned>(); };
  ImGui::Text( "PC %04X  A %02X  X %02X  Y %02X  S %02X  P %02X %s",
               byte( "pc" ),
               byte( "a" ),
               byte( "x" ),
               byte( "y" ),
               byte( "s" ),
               byte( "p" ),
               state.at( "flags" ).get_ref<std::string const&>().c_str() );
  ImGui::Text( "Cycle %llu", static_cast<unsigned long long>( state.at( "cycle" ).get<std::uint64_t>() ) );

  // The code from the PC on; a click on a line sets or clears its breakpoint.
  ImGui::SeparatorText( "Code" );
  control::Json const breakpoints = resultOf( mRequest( "debug.breakpoint.list", control::Json::object() ) )
                                        .value( "breakpoints", control::Json::array() );
  control::Json const code =
      resultOf( mRequest( "cpu.disassemble", { { "address", byte( "pc" ) }, { "count", 16 } } ) );
  if ( !code.is_null() )
  {
    for ( auto const& instruction : code.at( "instructions" ) )
    {
      auto const address = instruction.at( "address" ).get<unsigned>();
      bool const breaking = std::ranges::any_of(
          breakpoints, [address]( control::Json const& breakpoint ) { return breakpoint.get<unsigned>() == address; } );
      std::string const line = fmt::format( "{} {:04X}  {:<8} {}",
                                            breaking ? '*' : ' ',
                                            address,
                                            instruction.at( "bytes_hex" ).get<std::string>(),
                                            instruction.at( "text" ).get<std::string>() );
      if ( ImGui::Selectable( line.c_str(), address == byte( "pc" ) ) )
      {
        mLastError = errorOf(
            mRequest( breaking ? "debug.breakpoint.remove" : "debug.breakpoint.add", { { "address", address } } ) );
      }
    }
  }
  drawBreakpoints();

  ImGui::End();
}

void CpuWindow::drawBreakpoints()
{
  ImGui::SeparatorText( "Breakpoints" );
  if ( inputAddress( "##breakpoint", mNewBreakpoint ) )
  {
    mLastError = errorOf( mRequest( "debug.breakpoint.add", { { "address", mNewBreakpoint } } ) );
  }
  ImGui::SameLine();
  if ( ImGui::Button( "Add" ) )
  {
    mLastError = errorOf( mRequest( "debug.breakpoint.add", { { "address", mNewBreakpoint } } ) );
  }
  control::Json const breakpoints = resultOf( mRequest( "debug.breakpoint.list", control::Json::object() ) )
                                        .value( "breakpoints", control::Json::array() );
  for ( auto const& breakpoint : breakpoints )
  {
    auto const address = breakpoint.get<unsigned>();
    ImGui::PushID( static_cast<int>( address ) );
    ImGui::Text( "$%04X", address );
    ImGui::SameLine();
    if ( ImGui::SmallButton( "Remove" ) )
    {
      mLastError = errorOf( mRequest( "debug.breakpoint.remove", { { "address", address } } ) );
    }
    ImGui::PopID();
  }
  if ( !mLastError.empty() )
  {
    ImGui::TextWrapped( "%s", mLastError.c_str() );
  }
}

MemoryWindow::MemoryWindow( Requester request ) : mRequest{ std::move( request ) } {}

void MemoryWindow::draw( bool& open )
{
  if ( !open )
  {
    return;
  }
  if ( !ImGui::Begin( "Memory", &open ) )
  {
    ImGui::End();
    return;
  }
  ImGui::TextUnformatted( "Go to" );
  ImGui::SameLine();
  if ( inputAddress( "##goto", mGoTo ) )
  {
    mScrollToGoTo = true;
  }

  constexpr int bytesPerRow = 16;
  constexpr int rows = 0x10000 / bytesPerRow;
  if ( ImGui::BeginChild( "bytes", ImVec2{ 0.0F, 0.0F }, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar ) )
  {
    float const rowHeight = ImGui::GetTextLineHeightWithSpacing();
    if ( mScrollToGoTo )
    {
      int const row = mGoTo / bytesPerRow;
      ImGui::SetScrollY( static_cast<float>( row ) * rowHeight );
      mScrollToGoTo = false;
    }
    ImGuiListClipper clipper;
    clipper.Begin( rows, rowHeight );
    while ( clipper.Step() )
    {
      // The rows on show, in one request.
      int const first = clipper.DisplayStart;
      int const count = clipper.DisplayEnd - clipper.DisplayStart;
      control::Json const read = resultOf(
          mRequest( "memory.read", { { "address", first * bytesPerRow }, { "size", count * bytesPerRow } } ) );
      if ( read.is_null() )
      {
        ImGui::TextUnformatted( "The PET is not running" );
        break;
      }
      auto const& hex = read.at( "data_hex" ).get_ref<std::string const&>();
      for ( int row = 0; row < count; ++row )
      {
        std::string line = fmt::format( "{:04X} ", ( first + row ) * bytesPerRow );
        std::string characters;
        for ( int column = 0; column < bytesPerRow; ++column )
        {
          std::size_t const at = static_cast<std::size_t>( ( row * bytesPerRow ) + column ) * 2;
          line += ' ';
          line += hex.substr( at, 2 );
          auto const value = std::stoul( hex.substr( at, 2 ), nullptr, 16 );
          characters += value >= 0x20 && value < 0x7f ? static_cast<char>( value ) : '.';
        }
        line += "  ";
        line += characters;
        ImGui::TextUnformatted( line.c_str() );
      }
    }
  }
  ImGui::EndChild();
  ImGui::End();
}

HistoryWindow::HistoryWindow( Requester request, std::function<void( double seconds )> keep )
    : mRequest{ std::move( request ) }, mKeep{ std::move( keep ) }
{
}

void HistoryWindow::draw( bool& open )
{
  if ( !open )
  {
    return;
  }
  if ( !ImGui::Begin( "History", &open ) )
  {
    ImGui::End();
    return;
  }
  control::Json const kept = resultOf( mRequest( "debug.trace", { { "count", 0 } } ) );
  if ( kept.is_null() )
  {
    ImGui::TextUnformatted( "The PET is not running" );
    ImGui::End();
    return;
  }
  auto const total = kept.at( "total" ).get<int>();
  if ( !mSecondsKnown )
  {
    mSeconds = kept.at( "seconds" ).get<double>();
    mSecondsKnown = true;
  }
  ImGui::TextUnformatted( "Keep" );
  ImGui::SameLine();
  ImGui::SetNextItemWidth( ImGui::GetFontSize() * 6.0F );
  ImGui::InputDouble( "seconds", &mSeconds, 0.5, 5.0, "%.2f" );
  if ( ImGui::IsItemDeactivatedAfterEdit() )
  {
    mSeconds = std::clamp( mSeconds, 0.01, 60.0 );
    if ( mRequest( "debug.trace.configure", { { "seconds", mSeconds } } ).at( "ok" ) == true )
    {
      mKeep( mSeconds );
    }
  }
  ImGui::SameLine();
  ImGui::Text( "(%d instructions)", total );
  ImGui::SameLine();
  ImGui::Checkbox( "Follow the newest", &mFollow );

  // The search, back from the newest, on the emulator's side.
  ImGui::Checkbox( "PC", &mFindByPc );
  ImGui::SameLine();
  ImGui::BeginDisabled( !mFindByPc );
  inputAddress( "##findpc", mFindPc );
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::SetNextItemWidth( ImGui::GetFontSize() * 10.0F );
  ImGui::InputTextWithHint( "##findtext", "disassembly holds", mFindText.data(), mFindText.size() );
  ImGui::SameLine();
  if ( ImGui::Button( "Find" ) )
  {
    control::Json params{ { "limit", 200 } };
    if ( mFindByPc )
    {
      params["pc"] = mFindPc;
    }
    if ( mFindText[0] != 0 )
    {
      params["disasm_contains"] = std::string{ mFindText.data() };
    }
    mFound = resultOf( mRequest( "debug.trace.find", std::move( params ) ) );
  }
  if ( !mFound.is_null() )
  {
    ImGui::SameLine();
    ImGui::Text(
        "%zu found%s", mFound.at( "matches" ).size(), mFound.at( "next_from" ).is_null() ? "" : ", more back" );
    if ( !mFound.at( "matches" ).empty() &&
         ImGui::BeginListBox( "##found", ImVec2{ -1.0F, ImGui::GetTextLineHeightWithSpacing() * 5.0F } ) )
    {
      for ( auto const& match : mFound.at( "matches" ) )
      {
        std::string const label = fmt::format( "{:>8}  {:04X}  {}",
                                               match.at( "index" ).get<std::int64_t>(),
                                               match.at( "pc" ).get<unsigned>(),
                                               match.at( "disasm" ).get<std::string>() );
        if ( ImGui::Selectable( label.c_str() ) )
        {
          mShowIndex = match.at( "index" ).get<std::int64_t>();
          mFollow = false;
        }
      }
      ImGui::EndListBox();
    }
  }

  ImGuiTableFlags const flags =
      ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;
  if ( ImGui::BeginTable( "history", 6, flags ) )
  {
    ImGui::TableSetupScrollFreeze( 0, 1 );
    for ( char const* heading : { "Cycle", "PC", "Bytes", "Instruction", "A  X  Y  S", "NV-BDIZC" } )
    {
      ImGui::TableSetupColumn( heading );
    }
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;
    clipper.Begin( total );
    while ( clipper.Step() )
    {
      // The rows on show, in one request. The table runs as the code ran, the
      // oldest at the top; the history is indexed back from the newest, and
      // answers newest first.
      int const shown = clipper.DisplayEnd - clipper.DisplayStart;
      control::Json const rows =
          resultOf( mRequest( "debug.trace", { { "from", total - clipper.DisplayEnd }, { "count", shown } } ) );
      if ( rows.is_null() )
      {
        break;
      }
      auto const& instructions = rows.at( "instructions" );
      for ( auto entry = instructions.rbegin(); entry != instructions.rend(); ++entry )
      {
        control::Json const& row = *entry;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text( "%llu", static_cast<unsigned long long>( row.at( "cycle" ).get<std::uint64_t>() ) );
        ImGui::TableNextColumn();
        ImGui::Text( "%04X", row.at( "pc" ).get<unsigned>() );
        ImGui::TableNextColumn();
        ImGui::TextUnformatted( row.at( "bytes_hex" ).get_ref<std::string const&>().c_str() );
        ImGui::TableNextColumn();
        ImGui::TextUnformatted( row.at( "disasm" ).get_ref<std::string const&>().c_str() );
        ImGui::TableNextColumn();
        ImGui::Text( "%02X %02X %02X %02X",
                     row.at( "a" ).get<unsigned>(),
                     row.at( "x" ).get<unsigned>(),
                     row.at( "y" ).get<unsigned>(),
                     row.at( "s" ).get<unsigned>() );
        ImGui::TableNextColumn();
        auto const p = row.at( "p" ).get<unsigned>();
        std::string flagsText;
        for ( unsigned bit = 0; bit < 8; ++bit )
        {
          char const name = "NV-BDIZC"[bit];
          flagsText += ( p & ( 0x80U >> bit ) ) != 0 || name == '-' ? name : '.';
        }
        ImGui::TextUnformatted( flagsText.c_str() );
      }
    }
    if ( mFollow )
    {
      ImGui::SetScrollHereY( 1.0F );
    }
    else if ( mShowIndex )
    {
      // Row 0 is the oldest; an index counts back from the newest. The index
      // found moves with every instruction run, so the row is that found
      // while the PET stands still.
      std::int64_t const row = std::max<std::int64_t>( 0, total - 1 - *mShowIndex );
      ImGui::SetScrollY( static_cast<float>( row ) * ImGui::GetTextLineHeightWithSpacing() );
      mShowIndex.reset();
    }
    ImGui::EndTable();
  }
  ImGui::End();
}

IoWindow::IoWindow( Requester request ) : mRequest{ std::move( request ) } {}

void IoWindow::draw( bool& open )
{
  if ( !open )
  {
    return;
  }
  if ( !ImGui::Begin( "I/O", &open ) )
  {
    ImGui::End();
    return;
  }
  control::Json const io = resultOf( mRequest( "io.get_state", control::Json::object() ) );
  if ( io.is_null() )
  {
    ImGui::TextUnformatted( "The PET is not running" );
    ImGui::End();
    return;
  }
  ImGui::Text( "IRQ %s", io.at( "irq" ) == true ? "pulled" : "-" );
  for ( auto const* name : { "pia1", "pia2" } )
  {
    auto const& pia = io.at( name );
    ImGui::SeparatorText( std::string{ name } == "pia1" ? "PIA1 $E810: keyboard, retrace, EOI"
                                                        : "PIA2 $E820: IEEE-488 data" );
    ImGui::Text( "A: port %02X  DDR %02X  CR %02X  CA2 %d",
                 pia.at( "port_a" ).get<unsigned>(),
                 pia.at( "ddr_a" ).get<unsigned>(),
                 pia.at( "control_a" ).get<unsigned>(),
                 pia.at( "ca2" ).get<bool>() ? 1 : 0 );
    ImGui::Text( "B: port %02X  DDR %02X  CR %02X  CB2 %d",
                 pia.at( "port_b" ).get<unsigned>(),
                 pia.at( "ddr_b" ).get<unsigned>(),
                 pia.at( "control_b" ).get<unsigned>(),
                 pia.at( "cb2" ).get<bool>() ? 1 : 0 );
  }
  auto const& via = io.at( "via" );
  auto const& registers = via.at( "registers" );
  ImGui::SeparatorText( "VIA $E840: IEEE-488 handshake, retrace, character set, sound" );
  ImGui::Text( "PB %02X DDRB %02X  PA %02X DDRA %02X",
               registers.at( 0 ).get<unsigned>(),
               registers.at( 2 ).get<unsigned>(),
               registers.at( 1 ).get<unsigned>(),
               registers.at( 3 ).get<unsigned>() );
  ImGui::Text( "T1 %04X latch %04X  T2 %04X",
               via.at( "t1" ).get<unsigned>(),
               via.at( "t1_latch" ).get<unsigned>(),
               via.at( "t2" ).get<unsigned>() );
  ImGui::Text( "SR %02X  ACR %02X  PCR %02X  IFR %02X  IER %02X",
               registers.at( 10 ).get<unsigned>(),
               registers.at( 11 ).get<unsigned>(),
               registers.at( 12 ).get<unsigned>(),
               via.at( "ifr" ).get<unsigned>(),
               via.at( "ier" ).get<unsigned>() );
  ImGui::End();
}

} // namespace pet::app
