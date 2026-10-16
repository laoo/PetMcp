#include "Application.hpp"

#include "pet/video/Screen.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace pet::app
{

namespace
{

constexpr auto SCREEN_WIDTH = static_cast<float>( video::SCREEN_WIDTH );
constexpr auto SCREEN_HEIGHT = static_cast<float>( video::SCREEN_HEIGHT );

// The window opens at about three times the screen, plus room for the menu bar
// and a side panel, scaled for the display it opens on.
constexpr int INITIAL_WIDTH = 1600;
constexpr int INITIAL_HEIGHT = 900;

constexpr char const* SCREEN_WINDOW = "Screen";
constexpr char const* STATUS_WINDOW = "Status";

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
  std::unique_ptr<Application> application{ new Application{ window, device, std::move( settings ) } };
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
      mFrame{ blankScreen() }, mEmulation{ std::make_unique<EmulationThread>( std::move( settings ) ) },
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
  mScreen.reset();
  mRenderer.reset();
  // The servers hand requests to the emulation thread: they go first.
  mMcpHttp.reset();
  mMcp.reset();
  mLineServer.reset();
  mEmulation.reset();
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
  if ( mEmulation->takePicture( mPicturesShown, mFrame ) )
  {
    mFrameChanged = true;
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
  mScreenShown = false;
  if ( open )
  {
    // Sized in the display's pixels, which the shader draws, centred; scaled
    // by whole multiples, or as large as fits.
    float const pixelsPerPoint = ImGui::GetIO().DisplayFramebufferScale.y;
    ImVec2 const available = ImGui::GetContentRegionAvail();
    float scale = std::min( available.x * pixelsPerPoint / SCREEN_WIDTH, available.y * pixelsPerPoint / SCREEN_HEIGHT );
    if ( mDisplay.integerScale )
    {
      scale = std::max( 1.0F, std::floor( scale ) );
    }
    auto const width = static_cast<std::uint32_t>( std::lround( SCREEN_WIDTH * scale ) );
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
      if ( result.at( "paused" ) == true )
      {
        ImGui::TextUnformatted( "Paused" );
      }
    }
    else
    {
      ImGui::Text( "emu.status failed: %s", response.at( "error" ).dump().c_str() );
    }
    ImGui::Text( "Interface: %.1f fps", static_cast<double>( ImGui::GetIO().Framerate ) );
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
