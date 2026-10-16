#pragma once

#include "DebuggerWindows.hpp"
#include "EmulationThread.hpp"
#include "GpuTexture.hpp"
#include "HostKeyboard.hpp"
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
#include <mutex>
#include <optional>
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

  /// Loads the .prg at `path` and runs it, through the dispatcher, as an agent
  /// would; when the PET has only just been powered up, once it is ready.
  void runProgram( std::filesystem::path const& path );

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

  /// Hands the keyboard to the emulation thread, and takes the last picture
  /// it completed.
  void updateEmulation();
  /// Lights the picture's dots in the phosphor's colour.
  void tint();
  /// Takes what the file dialogs chose, if they have, and loads a program
  /// waiting for the PET to be ready.
  void takeChosen();

  void openProgram();
  void chooseRomDirectory();
  void chooseCardDirectory();
  /// Reads the ROMs from `directory`, and keeps it for the next start when
  /// `keep`.
  void setRomDirectory( std::filesystem::path const& directory, bool keep );
  /// Puts `directory` in the SD2PET as its card; with `keep`, remembers it.
  void setCardDirectory( std::filesystem::path const& directory, bool keep );
  void keepSetting( std::string const& key, control::Json value );

  /// Lays out one frame of the user interface.
  void drawInterface();
  void drawMenuBar();
  void drawDisplayMenu();
  void drawScreenWindow();
  void drawStatusWindow();
  void drawInputWindow();
  void drawSoundWindow();

  /// Uploads what changed, then renders the interface into the swapchain.
  void renderFrame();

  SDL_Window* mWindow;
  SDL_GPUDevice* mDevice;
  std::unique_ptr<GpuTexture> mScreen;
  /// The picture as the emulator drew it, lit dots white, and as shown.
  std::vector<std::uint8_t> mRawFrame;
  std::vector<std::uint8_t> mFrame;
  bool mFrameChanged{ true };
  /// Null when no audio device could be opened: the emulation then runs
  /// silent, paced by the clock alone.
  std::unique_ptr<AudioOutput> mAudio;
  std::unique_ptr<EmulationThread> mEmulation;
  std::unique_ptr<server::TcpLineServer> mLineServer;
  std::unique_ptr<server::McpServer> mMcp;
  std::unique_ptr<server::McpHttpServer> mMcpHttp;
  std::unique_ptr<ScreenRenderer> mRenderer;
  HostKeyboard mKeyboard;
  std::unique_ptr<CpuWindow> mCpu;
  std::unique_ptr<MemoryWindow> mMemory;
  std::unique_ptr<IoWindow> mIo;
  std::unique_ptr<DriveWindow> mDrive;
  std::unique_ptr<HistoryWindow> mHistory;
  bool mShowHistory{};
  bool mShowCpu{};
  bool mShowMemory{};
  bool mShowIo{};
  bool mShowDrive{};
  bool mShowSound{};
  /// Where the display settings, the keyboard map and the application's own
  /// settings are kept, beside imgui.ini; empty when they are not kept.
  std::filesystem::path mDisplayPath;
  std::filesystem::path mKeyboardPath;
  std::filesystem::path mSettingsPath;
  control::Json mSettings = control::Json::object();
  DisplaySettings mDisplay;
  /// Whether the screen was drawn into the renderer's target this frame, and
  /// whether it had the focus, which gives the PET the keyboard.
  bool mScreenShown{};
  bool mScreenFocused{};
  std::string mImguiIniPath;
  bool mQuit{};
  bool mShowStatus{ true };
  bool mShowInput{};
  bool mPaused{};
  std::int64_t mPicturesShown{ -1 };
  bool mShowImguiDemo{};
  std::string mLastError;
  /// What the dialogs chose, waiting to be taken: they answer on threads of
  /// their own.
  std::mutex mChosenMutex;
  std::optional<std::filesystem::path> mChosenProgram;
  std::optional<std::filesystem::path> mChosenRomDirectory;
  std::optional<std::filesystem::path> mChosenCardDirectory;
  /// A program to run once the PET is ready.
  std::optional<std::filesystem::path> mProgramWaiting;
};

} // namespace pet::app
