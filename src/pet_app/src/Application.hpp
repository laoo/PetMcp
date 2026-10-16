#pragma once

#include "EmulationThread.hpp"
#include "GpuTexture.hpp"
#include "ScreenRenderer.hpp"

#include "pet/Emulator.hpp"
#include "pet/control/Dispatcher.hpp"
#include "pet/server/McpHttpServer.hpp"
#include "pet/server/McpServer.hpp"
#include "pet/server/TcpLineServer.hpp"

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace pet::app
{

/// The desktop frontend: an SDL3 window rendered through SDL_GPU, with Dear
/// ImGui docked over it (docs/decisions/0008-the-renderer-is-sdl-gpu.md). It
/// reaches the emulator only through the dispatcher, as every other client does.
class Application
{
public:
  /// Opens the window and the GPU device and sets up ImGui. Answers why when
  /// any of them cannot be had. `settings` say where the ROMs and the card are.
  static std::expected<std::unique_ptr<Application>, std::string> create( Settings settings );

  /// Serves the control protocol to other programs while the window is open:
  /// JSON-lines on TCP `linePort`, MCP over HTTP on `mcpPort`, each unless 0.
  /// Their requests run on the emulation thread, on the machine on screen.
  /// Throws std::runtime_error when a port cannot be had.
  void serve( std::uint16_t linePort, std::uint16_t mcpPort );

  ~Application();

  Application( Application const& ) = delete;
  Application& operator=( Application const& ) = delete;
  Application( Application&& ) = delete;
  Application& operator=( Application&& ) = delete;

  /// Runs until the window is closed or Quit is chosen.
  void run();

private:
  Application( SDL_Window* window, SDL_GPUDevice* device, Settings settings );

  /// Answers a request of the control protocol, on the emulation thread.
  control::Json request( std::string const& method, control::Json params = control::Json::object() );

  /// Takes the last picture the emulation thread completed.
  void updateEmulation();

  /// Lays out one frame of the user interface.
  void drawInterface();
  void drawMenuBar();
  void drawDisplayMenu();
  void drawScreenWindow();
  void drawStatusWindow();

  /// Uploads what changed, then renders the interface into the swapchain.
  void renderFrame();

  SDL_Window* mWindow;
  SDL_GPUDevice* mDevice;
  std::unique_ptr<GpuTexture> mScreen;
  std::vector<std::uint8_t> mFrame;
  bool mFrameChanged{ true };
  std::unique_ptr<EmulationThread> mEmulation;
  std::unique_ptr<server::TcpLineServer> mLineServer;
  std::unique_ptr<server::McpServer> mMcp;
  std::unique_ptr<server::McpHttpServer> mMcpHttp;
  std::unique_ptr<ScreenRenderer> mRenderer;
  /// Where the display settings are kept, beside imgui.ini; empty when they
  /// are not kept.
  std::filesystem::path mDisplayPath;
  DisplaySettings mDisplay;
  /// Whether the screen was drawn into the renderer's target this frame.
  bool mScreenShown{};
  std::string mImguiIniPath;
  bool mQuit{};
  bool mShowStatus{ true };
  bool mPaused{};
  std::int64_t mPicturesShown{ -1 };
  bool mShowImguiDemo{};
};

} // namespace pet::app
