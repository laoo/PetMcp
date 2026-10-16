#include "Application.hpp"

#include "pet/video/Screen.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <utility>

namespace pet::app
{

namespace
{

constexpr auto SCREEN_HEIGHT = static_cast<float>( video::SCREEN_HEIGHT );

// The window opens at about three times the screen, plus room for the menu bar
// and a side panel, scaled for the display it opens on.
constexpr int INITIAL_WIDTH = 1600;
constexpr int INITIAL_HEIGHT = 900;

constexpr char const* SCREEN_WINDOW = "Screen";
constexpr char const* STATUS_WINDOW = "Status";
constexpr char const* INPUT_WINDOW = "Input";

/// The frames after power-up by which the PET is at READY, with a margin: it
/// gets there in 88.
constexpr std::int64_t READY_FRAME = 100;

/// Whether `path` names a disk image the SD2PET mounts, by its extension.
bool isDiskImage( std::filesystem::path const& path )
{
  std::string extension = path.extension().string();
  std::ranges::transform(
      extension, extension.begin(), []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
  return extension == ".d64" || extension == ".d71" || extension == ".d81" || extension == ".d80" ||
         extension == ".d82" || extension == ".dnp";
}

std::string sdlError( std::string const& what )
{
  return what + ": " + SDL_GetError();
}

/// A black screen, until the machine draws one.
std::vector<std::uint8_t> blankScreen()
{
  std::vector<std::uint8_t> rgba( video::SCREEN_WIDTH * video::SCREEN_HEIGHT * 4, 0 );
  for ( std::size_t alpha = 3; alpha < rgba.size(); alpha += 4 )
  {
    rgba[alpha] = 0xff;
  }
  return rgba;
}

} // namespace

std::expected<std::unique_ptr<Application>, std::string> Application::create( Settings settings )
{
  if ( !SDL_Init( SDL_INIT_VIDEO ) )
  {
    return std::unexpected( sdlError( "SDL_Init" ) );
  }

  float const scale = SDL_GetDisplayContentScale( SDL_GetPrimaryDisplay() );
  SDL_Window* const window = SDL_CreateWindow( "PetMcp",
                                               static_cast<int>( INITIAL_WIDTH * scale ),
                                               static_cast<int>( INITIAL_HEIGHT * scale ),
                                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY );
  if ( window == nullptr )
  {
    return std::unexpected( sdlError( "SDL_CreateWindow" ) );
  }

  // Every shader format the ImGui backend ships bytecode for, so that SDL may
  // pick the native API: Metal on macOS, Vulkan or D3D12 elsewhere.
  SDL_GPUDevice* const device = SDL_CreateGPUDevice(
      SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB, false, nullptr );
  if ( device == nullptr )
  {
    SDL_DestroyWindow( window );
    return std::unexpected( sdlError( "SDL_CreateGPUDevice" ) );
  }
  if ( !SDL_ClaimWindowForGPUDevice( device, window ) )
  {
    SDL_DestroyGPUDevice( device );
    SDL_DestroyWindow( window );
    return std::unexpected( sdlError( "SDL_ClaimWindowForGPUDevice" ) );
  }
  SDL_SetGPUSwapchainParameters( device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, SDL_GPU_PRESENTMODE_VSYNC );

  // From here on the destructor owns the window and the device.
  bool const romsGiven = !settings.romDirectory.empty();
  bool const cardGiven = !settings.cardDirectory.empty();
  std::unique_ptr<Application> application{ new Application{ window, device, std::move( settings ) } };
  if ( !romsGiven && application->mSettings.contains( "rom_directory" ) &&
       application->mSettings.at( "rom_directory" ).is_string() )
  {
    application->setRomDirectory( application->mSettings.at( "rom_directory" ).get<std::string>(), false );
  }
  if ( !cardGiven && application->mSettings.contains( "card_directory" ) &&
       application->mSettings.at( "card_directory" ).is_string() )
  {
    application->setCardDirectory( application->mSettings.at( "card_directory" ).get<std::string>(), false );
  }
  if ( !application->mScreen->valid() )
  {
    return std::unexpected( sdlError( "creating the screen texture" ) );
  }
  if ( !application->mRenderer->valid() )
  {
    return std::unexpected( sdlError( "creating the screen's shaders" ) );
  }
  return application;
}

Application::Application( SDL_Window* window, SDL_GPUDevice* device, Settings settings )
    : mWindow{ window }, mDevice{ device },
      mScreen{ std::make_unique<GpuTexture>( device,
                                             static_cast<std::uint32_t>( video::SCREEN_WIDTH ),
                                             static_cast<std::uint32_t>( video::SCREEN_HEIGHT ) ) },
      mRawFrame{ blankScreen() }, mFrame{ blankScreen() }, mAudio{ AudioOutput::open() },
      mEmulation{ std::make_unique<EmulationThread>( std::move( settings ), mAudio.get() ) },
      mRenderer{ std::make_unique<ScreenRenderer>( device ) }
{
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

  // The layout is kept with the user's preferences rather than in whatever
  // directory the application was started from.
  if ( char* const prefPath = SDL_GetPrefPath( "PetMcp", "petmcp" ); prefPath != nullptr )
  {
    mImguiIniPath = std::string{ prefPath } + "imgui.ini";
    mDisplayPath = std::filesystem::path{ prefPath } / "display.json";
    mKeyboardPath = std::filesystem::path{ prefPath } / "keyboard.json";
    mSettingsPath = std::filesystem::path{ prefPath } / "settings.json";
    SDL_free( prefPath );
    io.IniFilename = mImguiIniPath.c_str();
  }
  else
  {
    io.IniFilename = nullptr;
  }
  if ( !mDisplayPath.empty() )
  {
    mDisplay = loadDisplaySettings( mDisplayPath );
  }
  if ( !mKeyboardPath.empty() )
  {
    mKeyboard.load( mKeyboardPath );
  }
  if ( std::ifstream stream{ mSettingsPath }; !mSettingsPath.empty() && stream )
  {
    auto json = control::Json::parse( stream, nullptr, false );
    if ( json.is_object() )
    {
      mSettings = std::move( json );
    }
  }

  auto const requester = [this]( std::string const& method, control::Json params )
  { return request( method, std::move( params ) ); };
  mCpu = std::make_unique<CpuWindow>( requester );
  mMemory = std::make_unique<MemoryWindow>( requester );
  mIo = std::make_unique<IoWindow>( requester );
  mDrive = std::make_unique<DriveWindow>( requester );
  mHistory = std::make_unique<HistoryWindow>( requester,
                                              [this]( double seconds ) { keepSetting( "history_seconds", seconds ); } );
  if ( mSettings.contains( "history_seconds" ) && mSettings.at( "history_seconds" ).is_number() )
  {
    static_cast<void>( request( "debug.trace.configure", { { "seconds", mSettings.at( "history_seconds" ) } } ) );
  }

  float const scale = SDL_GetDisplayContentScale( SDL_GetPrimaryDisplay() );
  ImGui::StyleColorsDark();
  ImGui::GetStyle().ScaleAllSizes( scale );
  ImGui::GetStyle().FontScaleDpi = scale;

  ImGui_ImplSDL3_InitForSDLGPU( mWindow );
  ImGui_ImplSDLGPU3_InitInfo initInfo{};
  initInfo.Device = mDevice;
  initInfo.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat( mDevice, mWindow );
  initInfo.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
  ImGui_ImplSDLGPU3_Init( &initInfo );
}

Application::~Application()
{
  SDL_WaitForGPUIdle( mDevice );
  ImGui_ImplSDL3_Shutdown();
  ImGui_ImplSDLGPU3_Shutdown();
  ImGui::DestroyContext();
  mCpu.reset();
  mMemory.reset();
  mIo.reset();
  mDrive.reset();
  mHistory.reset();
  mScreen.reset();
  mRenderer.reset();
  // The servers hand requests to the emulation thread: they go first.
  mMcpHttp.reset();
  mMcp.reset();
  mLineServer.reset();
  // The emulation thread feeds the audio stream: it goes first.
  mEmulation.reset();
  mAudio.reset();
  SDL_ReleaseWindowFromGPUDevice( mDevice, mWindow );
  SDL_DestroyGPUDevice( mDevice );
  SDL_DestroyWindow( mWindow );
  SDL_Quit();
}

void Application::run()
{
  while ( !mQuit )
  {
    SDL_Event event;
    while ( SDL_PollEvent( &event ) )
    {
      ImGui_ImplSDL3_ProcessEvent( &event );
      ImGuiIO const& io = ImGui::GetIO();
      mKeyboard.handle( event, mScreenFocused && !io.WantTextInput );
      if ( event.type == SDL_EVENT_DROP_FILE && event.drop.data != nullptr )
      {
        runProgram( event.drop.data );
      }
      else if ( event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_O && !event.key.repeat &&
                ( event.key.mod & ( SDL_KMOD_GUI | SDL_KMOD_CTRL ) ) != 0 )
      {
        openProgram();
      }
      if ( event.type == SDL_EVENT_QUIT ||
           ( event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID( mWindow ) ) )
      {
        mQuit = true;
      }
    }

    // A minimised window has no swapchain to present to; waiting here keeps
    // the loop from spinning until it is restored.
    if ( ( SDL_GetWindowFlags( mWindow ) & SDL_WINDOW_MINIMIZED ) != 0 )
    {
      SDL_Delay( 10 );
      continue;
    }

    takeChosen();
    updateEmulation();

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    drawInterface();
    ImGui::Render();
    renderFrame();
  }
}

control::Json Application::request( std::string const& method, control::Json params )
{
  return mEmulation->handle( control::Json{ { "id", 1 }, { "method", method }, { "params", std::move( params ) } } );
}

void Application::serve( std::uint16_t linePort, std::uint16_t mcpPort )
{
  auto const handler = [this]( control::Json const& request ) { return mEmulation->handle( request ); };
  if ( linePort != 0 )
  {
    mLineServer = std::make_unique<server::TcpLineServer>( linePort, handler );
    spdlog::info( "serving JSON-lines on 127.0.0.1:{}", mLineServer->port() );
  }
  if ( mcpPort != 0 )
  {
    mMcp = std::make_unique<server::McpServer>( mEmulation->methods(), handler );
    mMcpHttp = std::make_unique<server::McpHttpServer>( mcpPort, *mMcp );
    spdlog::info( "serving MCP on http://127.0.0.1:{}/mcp", mMcpHttp->port() );
  }
}

void Application::updateEmulation()
{
  mEmulation->setHostKeys( mKeyboard.matrix() );
  if ( mEmulation->takePicture( mPicturesShown, mRawFrame ) )
  {
    tint();
  }
}

void Application::tint()
{
  auto const colour = colourOf( mDisplay.phosphor );
  mFrame.resize( mRawFrame.size() );
  for ( std::size_t at = 0; at + 3 < mRawFrame.size(); at += 4 )
  {
    bool const lit = mRawFrame[at] != 0;
    mFrame[at] = lit ? colour[0] : 0;
    mFrame[at + 1] = lit ? colour[1] : 0;
    mFrame[at + 2] = lit ? colour[2] : 0;
    mFrame[at + 3] = 0xff;
  }
  mFrameChanged = true;
}

void Application::runProgram( std::filesystem::path const& path )
{
  auto const status = request( "emu.status" );
  bool const ready =
      status.at( "ok" ) == true && status.at( "result" ).value( "frame", std::int64_t{ 0 } ) >= READY_FRAME;
  if ( !ready )
  {
    mProgramWaiting = path;
    return;
  }
  mProgramWaiting.reset();
  auto const response = isDiskImage( path ) ? request( "disk.run", { { "path", path.string() } } )
                                            : request( "program.load", { { "path", path.string() }, { "run", true } } );
  mLastError = response.at( "ok" ) == true ? std::string{} : response.at( "error" ).at( "message" ).get<std::string>();
}

void Application::takeChosen()
{
  std::optional<std::filesystem::path> program;
  std::optional<std::filesystem::path> directory;
  std::optional<std::filesystem::path> card;
  {
    std::scoped_lock const lock{ mChosenMutex };
    program.swap( mChosenProgram );
    directory.swap( mChosenRomDirectory );
    card.swap( mChosenCardDirectory );
  }
  if ( directory )
  {
    setRomDirectory( *directory, true );
  }
  if ( card )
  {
    setCardDirectory( *card, true );
  }
  if ( program )
  {
    runProgram( *program );
  }
  else if ( mProgramWaiting )
  {
    runProgram( *mProgramWaiting );
  }
}

void Application::openProgram()
{
  static constexpr std::array<SDL_DialogFileFilter, 3> FILTERS{
    { { .name = "PET programs and disks", .pattern = "prg;d64;d71;d81;d80;d82;dnp" },
      { .name = "PET programs", .pattern = "prg" },
      { .name = "Disk images", .pattern = "d64;d71;d81;d80;d82;dnp" } }
  };
  SDL_ShowOpenFileDialog(
      []( void* self, char const* const* files, int /*filter*/ )
      {
        // Null on an error, empty when the dialog was cancelled.
        if ( files != nullptr && files[0] != nullptr )
        {
          auto* const application = static_cast<Application*>( self );
          std::scoped_lock const lock{ application->mChosenMutex };
          application->mChosenProgram = files[0];
        }
      },
      this,
      mWindow,
      FILTERS.data(),
      static_cast<int>( FILTERS.size() ),
      nullptr,
      false );
}

void Application::chooseRomDirectory()
{
  SDL_ShowOpenFolderDialog(
      []( void* self, char const* const* folders, int /*filter*/ )
      {
        if ( folders != nullptr && folders[0] != nullptr )
        {
          auto* const application = static_cast<Application*>( self );
          std::scoped_lock const lock{ application->mChosenMutex };
          application->mChosenRomDirectory = folders[0];
        }
      },
      this,
      mWindow,
      nullptr,
      false );
}

void Application::chooseCardDirectory()
{
  SDL_ShowOpenFolderDialog(
      []( void* self, char const* const* folders, int /*filter*/ )
      {
        if ( folders != nullptr && folders[0] != nullptr )
        {
          auto* const application = static_cast<Application*>( self );
          std::scoped_lock const lock{ application->mChosenMutex };
          application->mChosenCardDirectory = folders[0];
        }
      },
      this,
      mWindow,
      nullptr,
      false );
}

void Application::setCardDirectory( std::filesystem::path const& directory, bool keep )
{
  auto const response = request( "disk.card", { { "path", directory.string() } } );
  if ( response.at( "ok" ) != true )
  {
    mLastError = response.at( "error" ).at( "message" ).get<std::string>();
    return;
  }
  mLastError.clear();
  if ( keep )
  {
    keepSetting( "card_directory", directory.string() );
  }
}

void Application::setRomDirectory( std::filesystem::path const& directory, bool keep )
{
  auto const response = request( "emu.set_rom_directory", { { "path", directory.string() } } );
  if ( response.at( "ok" ) != true )
  {
    mLastError = response.at( "error" ).at( "message" ).get<std::string>();
    return;
  }
  mLastError.clear();
  if ( keep )
  {
    keepSetting( "rom_directory", directory.string() );
  }
}

void Application::keepSetting( std::string const& key, control::Json value )
{
  mSettings[key] = std::move( value );
  if ( mSettingsPath.empty() )
  {
    return;
  }
  std::ofstream stream{ mSettingsPath };
  stream << mSettings.dump( 2 ) << '\n';
  if ( !stream )
  {
    spdlog::warn( "cannot write the settings to {}", mSettingsPath.string() );
  }
}

void Application::drawInterface()
{
  drawMenuBar();
  ImGuiID const dockspace = ImGui::DockSpaceOverViewport();
  // On the first run, before imgui.ini has a layout to restore, the screen
  // fills the dockspace and the status panel floats over it.
  ImGui::SetNextWindowDockID( dockspace, ImGuiCond_FirstUseEver );
  drawScreenWindow();
  drawStatusWindow();
  drawInputWindow();
  mCpu->draw( mShowCpu );
  mMemory->draw( mShowMemory );
  mIo->draw( mShowIo );
  mDrive->draw( mShowDrive );
  mHistory->draw( mShowHistory );
  drawSoundWindow();

  if ( mShowImguiDemo )
  {
    ImGui::ShowDemoWindow( &mShowImguiDemo );
  }
}

void Application::drawMenuBar()
{
  if ( !ImGui::BeginMainMenuBar() )
  {
    return;
  }
  if ( ImGui::BeginMenu( "File" ) )
  {
    if ( ImGui::MenuItem( "Open program or disk...",
                          SDL_GetPlatform() == std::string_view{ "macOS" } ? "Cmd+O" : "Ctrl+O" ) )
    {
      openProgram();
    }
    if ( ImGui::MenuItem( "Choose ROM directory..." ) )
    {
      chooseRomDirectory();
    }
    if ( ImGui::MenuItem( "Choose SD card directory..." ) )
    {
      chooseCardDirectory();
    }
    ImGui::Separator();
    if ( ImGui::MenuItem( "Quit" ) )
    {
      mQuit = true;
    }
    ImGui::EndMenu();
  }
  if ( ImGui::BeginMenu( "Emulation" ) )
  {
    // Paused through the protocol, as an agent pauses it, so that the menu
    // shows a pause an agent made too.
    mPaused = mEmulation->paused();
    if ( ImGui::MenuItem( "Pause", nullptr, &mPaused ) )
    {
      static_cast<void>( request( mPaused ? "emu.pause" : "emu.resume" ) );
    }
    if ( ImGui::MenuItem( "Reset" ) )
    {
      static_cast<void>( request( "emu.reset" ) );
    }
    if ( ImGui::MenuItem( "Power off and on" ) )
    {
      static_cast<void>( request( "emu.reset", { { "power", true } } ) );
    }

    ImGui::EndMenu();
  }
  if ( ImGui::BeginMenu( "Display" ) )
  {
    drawDisplayMenu();
    ImGui::EndMenu();
  }
  if ( ImGui::BeginMenu( "View" ) )
  {
    ImGui::MenuItem( STATUS_WINDOW, nullptr, &mShowStatus );
    ImGui::MenuItem( INPUT_WINDOW, nullptr, &mShowInput );
    ImGui::MenuItem( "CPU", nullptr, &mShowCpu );
    ImGui::MenuItem( "History", nullptr, &mShowHistory );
    ImGui::MenuItem( "Memory", nullptr, &mShowMemory );
    ImGui::MenuItem( "I/O", nullptr, &mShowIo );
    ImGui::MenuItem( "Drive", nullptr, &mShowDrive );
    ImGui::MenuItem( "Sound", nullptr, &mShowSound );
    ImGui::Separator();
    ImGui::MenuItem( "ImGui demo", nullptr, &mShowImguiDemo );
    ImGui::EndMenu();
  }
  ImGui::EndMainMenuBar();
}

void Application::drawDisplayMenu()
{
  bool changed = false;
  for ( auto const preset :
        { DisplaySettings::Preset::SHARP, DisplaySettings::Preset::SCANLINES, DisplaySettings::Preset::CRT } )
  {
    if ( ImGui::MenuItem( std::string{ nameOf( preset ) }.c_str(), nullptr, mDisplay.preset == preset ) )
    {
      mDisplay.preset = preset;
      changed = true;
    }
  }
  ImGui::Separator();
  for ( auto const phosphor :
        { DisplaySettings::Phosphor::GREEN, DisplaySettings::Phosphor::WHITE, DisplaySettings::Phosphor::AMBER } )
  {
    if ( ImGui::MenuItem( std::string{ nameOf( phosphor ) }.c_str(), nullptr, mDisplay.phosphor == phosphor ) )
    {
      mDisplay.phosphor = phosphor;
      tint();
      changed = true;
    }
  }
  ImGui::Separator();
  for ( auto const shape : { DisplaySettings::Shape::SQUARE, DisplaySettings::Shape::MONITOR } )
  {
    if ( ImGui::MenuItem( std::string{ nameOf( shape ) }.c_str(), nullptr, mDisplay.shape == shape ) )
    {
      mDisplay.shape = shape;
      changed = true;
    }
  }
  ImGui::Separator();
  changed |= ImGui::MenuItem( "Whole multiples only", nullptr, &mDisplay.integerScale );
  // A slider changes the picture as it is dragged, and is kept when let go.
  // Its ID is its own: the Scanlines preset's menu item has the same label.
  auto const slider = [&changed]( char const* label, float& value )
  {
    ImGui::PushID( "setting" );
    ImGui::SliderFloat( label, &value, 0.0F, 1.0F );
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::PopID();
  };
  if ( mDisplay.preset != DisplaySettings::Preset::SHARP )
  {
    ImGui::Separator();
    slider( "Scanlines", mDisplay.scanlines );
  }
  if ( mDisplay.preset == DisplaySettings::Preset::CRT )
  {
    slider( "Curvature", mDisplay.curvature );
    slider( "Mask", mDisplay.mask );
  }
  if ( changed && !mDisplayPath.empty() )
  {
    saveDisplaySettings( mDisplay, mDisplayPath );
  }
}

void Application::drawScreenWindow()
{
  ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2{ 0.0F, 0.0F } );
  bool const open = ImGui::Begin( SCREEN_WINDOW, nullptr, ImGuiWindowFlags_NoScrollbar );
  ImGui::PopStyleVar();
  bool const focused = ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows );
  if ( mScreenFocused && !focused )
  {
    // Keys held as the screen loses the keyboard would never be let go.
    mKeyboard.releaseAll();
  }
  mScreenFocused = focused;
  mScreenShown = false;
  if ( open )
  {
    // Sized in the display's pixels, which the shader draws, centred; scaled
    // by whole multiples of its height, or as large as fits, in the shape the
    // Display menu chooses.
    float const pixelsPerPoint = ImGui::GetIO().DisplayFramebufferScale.y;
    ImVec2 const available = ImGui::GetContentRegionAvail();
    float const shownWidth = SCREEN_HEIGHT * aspectOf( mDisplay.shape );
    float scale = std::min( available.x * pixelsPerPoint / shownWidth, available.y * pixelsPerPoint / SCREEN_HEIGHT );
    if ( mDisplay.integerScale )
    {
      scale = std::max( 1.0F, std::floor( scale ) );
    }
    auto const width = static_cast<std::uint32_t>( std::lround( shownWidth * scale ) );
    auto const height = static_cast<std::uint32_t>( std::lround( SCREEN_HEIGHT * scale ) );
    ImVec2 const size{ static_cast<float>( width ) / pixelsPerPoint, static_cast<float>( height ) / pixelsPerPoint };
    // Centred, on a whole pixel of the display: the shader's output is shown a
    // pixel for a pixel, and half a pixel off would sample some of its rows
    // twice and others not at all.
    ImVec2 const cursor = ImGui::GetCursorScreenPos();
    auto const onPixel = [pixelsPerPoint]( float points )
    { return std::round( points * pixelsPerPoint ) / pixelsPerPoint; };
    ImVec2 const corner{ onPixel( cursor.x + std::max( 0.0F, ( available.x - size.x ) / 2.0F ) ),
                         onPixel( cursor.y + std::max( 0.0F, ( available.y - size.y ) / 2.0F ) ) };
    ImGui::SetCursorScreenPos( corner );

    // The shader's output is shown a pixel for a pixel: sampled nearest, the
    // sampler restored for the rest of the interface.
    if ( SDL_GPUTexture* const target = width > 0 && height > 0 ? mRenderer->target( width, height ) : nullptr )
    {
      ImDrawList* const drawList = ImGui::GetWindowDrawList();
      ImGuiPlatformIO const& platform = ImGui::GetPlatformIO();
      drawList->AddCallback( platform.DrawCallback_SetSamplerNearest, nullptr );
      drawList->AddImage( ImTextureRef{ reinterpret_cast<ImTextureID>( target ) },
                          corner,
                          ImVec2{ corner.x + size.x, corner.y + size.y } );
      ImGui::Dummy( size );
      drawList->AddCallback( platform.DrawCallback_SetSamplerLinear, nullptr );
      mScreenShown = true;
    }
  }
  ImGui::End();
}

void Application::drawStatusWindow()
{
  if ( !mShowStatus )
  {
    return;
  }
  if ( ImGui::Begin( STATUS_WINDOW, &mShowStatus ) )
  {
    // Asked through the dispatcher, as an agent would ask, so that what the
    // window shows is what the protocol answers.
    auto const response = request( "emu.status" );
    if ( response.at( "ok" ) == true )
    {
      auto const& result = response.at( "result" );
      ImGui::Text( "Version: %s", result.at( "version" ).get_ref<std::string const&>().c_str() );
      if ( result.at( "running" ) == true )
      {
        ImGui::Text( "Frame: %lld", static_cast<long long>( result.at( "frame" ).get<std::int64_t>() ) );
        ImGui::Text( "Cycle: %lld", static_cast<long long>( result.at( "cycles" ).get<std::int64_t>() ) );
      }
      else
      {
        ImGui::TextWrapped( "Not running: %s", result.at( "error" ).get_ref<std::string const&>().c_str() );
        ImGui::TextWrapped( "Choose the ROMs' directory with File > Choose ROM directory." );
      }
      if ( result.at( "paused" ) == true )
      {
        ImGui::TextUnformatted( "Paused" );
      }
      if ( result.contains( "breakpoint" ) )
      {
        ImGui::Text( "Stopped at the breakpoint at $%04X", result.at( "breakpoint" ).at( "address" ).get<unsigned>() );
      }
      if ( result.contains( "watchpoint" ) )
      {
        auto const& watched = result.at( "watchpoint" );
        ImGui::Text( "Stopped after a %s of $%04X by $%04X",
                     watched.at( "access" ).get_ref<std::string const&>().c_str(),
                     watched.at( "address" ).get<unsigned>(),
                     watched.at( "pc" ).get<unsigned>() );
      }
    }
    if ( !mLastError.empty() )
    {
      ImGui::TextWrapped( "%s", mLastError.c_str() );
    }
    ImGui::Text( "Interface: %.1f fps", static_cast<double>( ImGui::GetIO().Framerate ) );
  }
  ImGui::End();
}

void Application::drawInputWindow()
{
  if ( !mShowInput )
  {
    return;
  }
  if ( ImGui::Begin( INPUT_WINDOW, &mShowInput ) )
  {
    ImGui::TextWrapped( "Keys that make a character type it; these keys are bound to the PET's own. SHIFT on the "
                        "host shifts them." );
    if ( ImGui::BeginTable( "bindings", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit ) )
    {
      auto const& bindings = mKeyboard.bindings();
      for ( std::size_t i = 0; i < bindings.size(); ++i )
      {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted( bindings.at( i ).name.data(),
                                bindings.at( i ).name.data() + bindings.at( i ).name.size() );
        ImGui::TableNextColumn();
        char const* bound = "-";
        if ( mKeyboard.capturing() == i )
        {
          bound = "press a key...";
        }
        else if ( bindings.at( i ).key != SDLK_UNKNOWN )
        {
          bound = SDL_GetKeyName( bindings.at( i ).key );
        }
        ImGui::TextUnformatted( bound );
        ImGui::TableNextColumn();
        ImGui::PushID( static_cast<int>( i ) );
        if ( ImGui::SmallButton( "Bind" ) )
        {
          mKeyboard.capture( i );
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    if ( !mKeyboardPath.empty() && !mKeyboard.capturing() && ImGui::Button( "Keep these" ) )
    {
      mKeyboard.save( mKeyboardPath );
    }

    // The PET's matrix, the keys down lit.
    ImGui::SeparatorText( "Keys down" );
    machine::KeyMatrix const held = mKeyboard.matrix();
    for ( unsigned row = 0; row < machine::KEYBOARD_ROWS; ++row )
    {
      for ( unsigned column = 0; column < machine::KEYBOARD_COLUMNS; ++column )
      {
        std::string_view const name = machine::nameOf(
            machine::PetKey{ .row = static_cast<std::uint8_t>( row ), .column = static_cast<std::uint8_t>( column ) } );
        bool const down = ( held.at( row ) & ( 1U << column ) ) != 0;
        if ( column > 0 )
        {
          ImGui::SameLine();
        }
        ImGui::BeginDisabled( !down );
        ImGui::Button( std::string{ name.empty() ? "." : name.substr( 0, 3 ) }.c_str(), ImVec2{ 36.0F, 0.0F } );
        ImGui::EndDisabled();
      }
    }
  }
  ImGui::End();
}

void Application::drawSoundWindow()
{
  if ( !mShowSound )
  {
    return;
  }
  if ( ImGui::Begin( "Sound", &mShowSound ) )
  {
    ImGui::Text( "Output: %s", mAudio ? "default device" : "none" );
    if ( mAudio )
    {
      ImGui::SameLine();
      ImGui::Text( "(%.0f ms queued)", mAudio->queuedSeconds() * 1000.0 );
    }
    auto const response = request( "expansion.get_state" );
    if ( response.at( "ok" ) != true )
    {
      ImGui::TextWrapped( "%s", response.at( "error" ).at( "message" ).get_ref<std::string const&>().c_str() );
      ImGui::End();
      return;
    }
    auto const& board = response.at( "result" );
    ImGui::Text( "Characters: %s, buffer %u shown",
                 board.at( "characters" ).get_ref<std::string const&>().c_str(),
                 board.at( "shown_buffer" ).get<unsigned>() );
    ImGui::Text( "Banks: RAM0 $%02X at $A000, RAM1 $%02X at $B000",
                 board.at( "banks" ).at( 0 ).get<unsigned>(),
                 board.at( "banks" ).at( 1 ).get<unsigned>() );
    // The registers of both chips, side by side.
    ImGuiTableFlags const flags =
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingFixedFit;
    if ( ImGui::BeginTable( "registers", 3, flags ) )
    {
      ImGui::TableSetupColumn( "Register" );
      for ( unsigned chip = 0; chip < 2; ++chip )
      {
        auto const& state = board.at( "chips" ).at( chip );
        std::string const heading = fmt::format( "AY{} {} MHz{}",
                                                 chip + 1,
                                                 state.at( "clock_hz" ).get<unsigned>() / 1'000'000,
                                                 chip == 0 ? ", left" : ", right" );
        ImGui::TableSetupColumn( heading.c_str() );
      }
      ImGui::TableHeadersRow();
      static constexpr std::array<char const*, 16> NAMES{
        "0 A fine",       "1 A coarse",    "2 B fine",  "3 B coarse", "4 C fine",   "5 C coarse",
        "6 noise",        "7 mixer",       "8 A level", "9 B level",  "10 C level", "11 env. fine",
        "12 env. coarse", "13 env. shape", "14 port A", "15 port B"
      };
      for ( unsigned reg = 0; reg < 16; ++reg )
      {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted( NAMES.at( reg ) );
        for ( unsigned chip = 0; chip < 2; ++chip )
        {
          auto const& state = board.at( "chips" ).at( chip );
          ImGui::TableNextColumn();
          bool const latched = state.at( "latch" ).get<unsigned>() == reg;
          ImGui::Text( "%02X%s", state.at( "registers" ).at( reg ).get<unsigned>(), latched ? " <" : "" );
        }
      }
      ImGui::EndTable();
    }
  }
  ImGui::End();
}

void Application::renderFrame()
{
  ImDrawData* const drawData = ImGui::GetDrawData();
  bool const minimised = drawData->DisplaySize.x <= 0.0F || drawData->DisplaySize.y <= 0.0F;

  SDL_GPUCommandBuffer* const commands = SDL_AcquireGPUCommandBuffer( mDevice );
  if ( commands == nullptr )
  {
    return;
  }

  if ( mFrameChanged )
  {
    mScreen->upload( commands, mFrame );
    mFrameChanged = false;
  }
  if ( mScreenShown )
  {
    mRenderer->render( commands, *mScreen, mDisplay );
  }

  SDL_GPUTexture* swapchain = nullptr;
  if ( SDL_WaitAndAcquireGPUSwapchainTexture( commands, mWindow, &swapchain, nullptr, nullptr ) &&
       swapchain != nullptr && !minimised )
  {
    // The backend has to upload its vertices before the render pass begins.
    ImGui_ImplSDLGPU3_PrepareDrawData( drawData, commands );

    SDL_GPUColorTargetInfo target{};
    target.texture = swapchain;
    target.clear_color = SDL_FColor{ .r = 0.0F, .g = 0.0F, .b = 0.0F, .a = 1.0F };
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* const pass = SDL_BeginGPURenderPass( commands, &target, 1, nullptr );
    ImGui_ImplSDLGPU3_RenderDrawData( drawData, commands, pass );
    SDL_EndGPURenderPass( pass );
  }
  SDL_SubmitGPUCommandBuffer( commands );
}

} // namespace pet::app
