#pragma once

//The ares frontend for Android (and the headless test harness).
//No GUI toolkit and no ruby drivers: the host feeds input, and pulls video and audio.
//Only the Mega Drive family is built: Genesis, Sega CD, 32X and Sega CD 32X.

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace aresdroid {

//what to boot. Auto picks from the file and the options below.
enum class Machine : int {
  Auto        = 0,
  MegaDrive   = 1,  //cartridge (optionally + a Sega CD disc: "Mode 1")
  Mega32X     = 2,  //32X cartridge (optionally + a Sega CD disc: Sonic 32X Plus, OpenTyrian + CD)
  MegaCD      = 3,  //Sega CD disc
  MegaCD32X   = 4,  //Sega CD + 32X disc, with the RAM cart in the cartridge slot (CD32X)
};

//the pad, as a bit mask. Matches the order of the Fighting Pad's buttons.
enum Button : uint32_t {
  Up    = 1 << 0,  Down = 1 << 1,  Left = 1 << 2,  Right = 1 << 3,
  A     = 1 << 4,  B    = 1 << 5,  C    = 1 << 6,
  X     = 1 << 7,  Y    = 1 << 8,  Z    = 1 << 9,
  Mode  = 1 << 10, Start = 1 << 11,
};

struct Paths {
  std::string home;        //app-private folder: ares data, Sega CD internal backup RAM, saves
  std::string biosUS;      //Sega CD BIOS, one per region
  std::string biosJP;
  std::string biosEU;
};

struct RamCart {
  std::string file;        //where the cart's SRAM lives (battery-backed); created if missing
  uint32_t sizeMiB = 4;    //4 (MicroNut99 RAM cart), 8 or 16 (16MB-PCB-CD32X, paged)
  bool writeProtect = false;
  bool everdrive = false;  //a Mega EverDrive PRO as the RAM cart instead (16 MB, extended SSF mapper)
  std::string boot;        //EverDrive only: a launcher ROM, booted first from bank 0; it sets the
                           //mapper and releases CART_IN itself. Empty: the disc boots at once, with
                           //the EverDrive as a launcher would leave it
};

struct LoadRequest {
  std::string game;        //.cue / .chd for discs, .32x / .md / .bin / .gen / .smd / .zip for carts
  std::string disc;        //optional: a Sega CD disc to put in the drive together with a cartridge
  Machine machine = Machine::Auto;
  std::string region;      //"" = from the game (preferring NTSC-U), or NTSC-U / NTSC-J / PAL
  RamCart ramCart;
  bool recompiler = true;  //SH-2 recompiler (32X); off = interpreter
};

struct VideoFrame {
  std::vector<uint32_t> pixels;  //0xAARRGGBB, width * height
  uint32_t width = 0;
  uint32_t height = 0;
  double aspect = 4.0 / 3.0;     //display aspect ratio of the whole frame
  uint64_t serial = 0;           //incremented on every new frame
};

class Session {
public:
  Session();
  ~Session();

  //returns an empty string on success, else a message for the user
  std::string load(const Paths& paths, const LoadRequest& request);
  void unload();
  bool loaded() const;

  //what was actually booted (after Auto): e.g. "Mega CD 32X (NTSC-U) + 4 MB RAM cart"
  std::string description() const;
  Machine machine() const;

  //run one video frame of emulation (thread: the emulation thread)
  void runFrame();
  void reset();

  //input, from any thread
  void setButtons(uint32_t port, uint32_t mask);

  //video, from any thread: copies the newest frame if it is newer than `serial`
  bool copyFrame(VideoFrame& out, uint64_t serial) const;

  //audio: interleaved stereo float samples at `sampleRate`, pulled by the audio thread
  void setSampleRate(uint32_t sampleRate);
  uint32_t readAudio(float* out, uint32_t frames);
  uint32_t bufferedAudioFrames() const;

  //battery memory (cart SRAM, the RAM cart, Sega CD backup RAM) to disk
  void saveMemory();

  //save states
  bool saveState(const std::string& file);
  bool loadState(const std::string& file);

  //32X COMM0-COMM14 (index 0-7), as the 68000 sees them: the CD32X boot stacks post their
  //status in COMM12 (0x4B10 = game running), handy for the log view and the test harness
  uint16_t comm(uint32_t index) const;

  //log lines from the core and from the loader (shown in the app's log view)
  void setLogger(std::function<void(const std::string&)> logger);

  struct Impl;
private:
  Impl* self;
};

//helpers used by the UI
Machine guessMachine(const std::string& game, const std::string& disc);
std::string machineName(Machine);

}
