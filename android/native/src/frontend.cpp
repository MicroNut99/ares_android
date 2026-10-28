#include <mia/mia.hpp>
#include <md/md.hpp>

#include "frontend.hpp"

#include <algorithm>
#include <thread>

//nall's main.cpp (linked in through the nall object library) refers to this
namespace nall { auto main(Arguments) -> void; }
auto nall::main(Arguments) -> void {}

namespace aresdroid {

namespace {

//single producer (emulation thread), single consumer (audio thread)
struct AudioRing {
  std::vector<float> data;  //interleaved stereo
  std::atomic<uint32_t> readIndex{0};
  std::atomic<uint32_t> writeIndex{0};
  uint32_t capacity = 0;    //in frames

  void allocate(uint32_t frames) {
    capacity = frames;
    data.assign(frames * 2, 0.0f);
    readIndex = 0;
    writeIndex = 0;
  }
  uint32_t available() const {
    return writeIndex.load(std::memory_order_acquire) - readIndex.load(std::memory_order_acquire);
  }
  void push(float left, float right) {
    uint32_t w = writeIndex.load(std::memory_order_relaxed);
    if(w - readIndex.load(std::memory_order_acquire) >= capacity) return;  //full: drop
    uint32_t i = (w % capacity) * 2;
    data[i + 0] = left;
    data[i + 1] = right;
    writeIndex.store(w + 1, std::memory_order_release);
  }
  uint32_t pop(float* out, uint32_t frames) {
    uint32_t r = readIndex.load(std::memory_order_relaxed);
    uint32_t n = std::min(frames, writeIndex.load(std::memory_order_acquire) - r);
    for(uint32_t k = 0; k < n; k++) {
      uint32_t i = ((r + k) % capacity) * 2;
      out[k * 2 + 0] = data[i + 0];
      out[k * 2 + 1] = data[i + 1];
    }
    readIndex.store(r + n, std::memory_order_release);
    return n;
  }
};

auto toNall(const std::string& s) -> nall::string { return nall::string{s.c_str()}; }

auto lowerExtension(const std::string& path) -> std::string {
  auto dot = path.find_last_of('.');
  if(dot == std::string::npos) return {};
  std::string e = path.substr(dot + 1);
  for(auto& c : e) c = (char)tolower((unsigned char)c);
  return e;
}

auto isDisc(const std::string& path) -> bool {
  auto e = lowerExtension(path);
  return e == "cue" || e == "chd";
}

}

struct Session::Impl : ares::Platform {
  //--- state -------------------------------------------------------------------------
  ares::Node::System root;
  std::shared_ptr<mia::Pak> system;
  std::shared_ptr<mia::Pak> cart;   //cartridge medium (Mega Drive / Mega 32X)
  std::shared_ptr<mia::Pak> disc;   //Mega CD medium
  std::shared_ptr<vfs::directory> ramCartPak;
  RamCart ramCart;
  Machine booted = Machine::Auto;
  std::string bootedDescription;

  std::vector<ares::Node::Audio::Stream> streams;
  AudioRing audioRing;
  uint32_t sampleRate = 48000;

  std::atomic<uint32_t> buttons[2] = {0, 0};

  mutable std::mutex videoMutex;
  VideoFrame frame;

  std::mutex logMutex;
  std::function<void(const std::string&)> logger;

  void log(const std::string& line) {
    std::lock_guard<std::mutex> lock(logMutex);
    if(logger) logger(line);
    else fprintf(stderr, "%s\n", line.c_str());
  }

  //--- ares::Platform ------------------------------------------------------------------
  auto attach(ares::Node::Object node) -> void override {
    if(auto stream = node->cast<ares::Node::Audio::Stream>()) {
      streams = root->find<ares::Node::Audio::Stream>();
      stream->setResamplerFrequency(sampleRate);
    }
  }

  auto detach(ares::Node::Object node) -> void override {
    if(auto stream = node->cast<ares::Node::Audio::Stream>()) {
      streams = root->find<ares::Node::Audio::Stream>();
      std::erase(streams, stream);
    }
  }

  auto pak(ares::Node::Object node) -> std::shared_ptr<vfs::directory> override {
    auto name = node->name();
    if(name == "Mega Drive") return system ? system->pak : nullptr;
    if(name == "Mega Drive Cartridge") {
      if(ramCartPak) return ramCartPak;
      return cart ? cart->pak : nullptr;
    }
    if(name == "Mega CD Disc") return disc ? disc->pak : nullptr;
    return {};
  }

  auto event(ares::Event) -> void override {}

  auto log(ares::Node::Debugger::Tracer::Tracer node, nall::string_view message) -> void override {
    log(std::string{(const char*)message});
  }

  auto status(nall::string_view message) -> void override {
    log(std::string{(const char*)message});
  }

  auto video(ares::Node::Video::Screen node, const u32* data, u32 pitch, u32 width, u32 height) -> void override {
    std::lock_guard<std::mutex> lock(videoMutex);
    pitch >>= 2;
    frame.pixels.resize((size_t)width * height);
    for(u32 y = 0; y < height; y++) {
      memcpy(frame.pixels.data() + (size_t)y * width, data + (size_t)y * pitch, width * sizeof(u32));
    }
    frame.width = width;
    frame.height = height;
    double w = node->width() * node->scaleX() * node->aspectX() / node->aspectY();
    double h = node->height() * node->scaleY();
    frame.aspect = h > 0 ? w / h : 4.0 / 3.0;
    frame.serial++;
  }

  auto audio(ares::Node::Audio::Stream) -> void override {
    if(streams.empty()) return;
    while(true) {
      for(auto& stream : streams) if(!stream->pending()) return;
      f64 samples[2] = {0.0, 0.0};
      for(auto& stream : streams) {
        f64 buffer[2];
        u32 channels = stream->read(buffer);
        if(channels == 1) { samples[0] += buffer[0]; samples[1] += buffer[0]; }
        else { samples[0] += buffer[0]; samples[1] += buffer[1]; }
      }
      audioRing.push((float)std::clamp(samples[0], -1.0, 1.0), (float)std::clamp(samples[1], -1.0, 1.0));
    }
  }

  auto input(ares::Node::Input::Input node) -> void override {
    auto device = ares::Node::parent(node);
    if(!device) return;
    auto port = ares::Node::parent(device);
    if(!port) return;
    u32 id = port->name() == "Controller Port 2" ? 1 : 0;
    u32 mask = buttons[id].load(std::memory_order_relaxed);

    if(auto button = node->cast<ares::Node::Input::Button>()) {
      auto name = button->name();
      u32 bit = 0;
      if(name == "Up")    bit = Up;
      if(name == "Down")  bit = Down;
      if(name == "Left")  bit = Left;
      if(name == "Right") bit = Right;
      if(name == "A")     bit = A;
      if(name == "B")     bit = B;
      if(name == "C")     bit = C;
      if(name == "X")     bit = X;
      if(name == "Y")     bit = Y;
      if(name == "Z")     bit = Z;
      if(name == "Mode")  bit = Mode;
      if(name == "Start") bit = Start;
      button->setValue(bit && (mask & bit));
    }
  }

  //--- loading ---------------------------------------------------------------------------
  auto chooseRegion(std::shared_ptr<mia::Pak> pak, const std::string& prefer) -> nall::string {
    std::vector<nall::string> order;
    if(!prefer.empty()) order.push_back(toNall(prefer));
    for(auto r : {"NTSC-U", "NTSC-J", "PAL"}) order.push_back(r);
    nall::string regionList = pak->pak->attribute("region");
    auto regions = nall::split_and_strip(regionList, ",");
    for(auto& want : order) {
      if(std::ranges::find(regions, want) != regions.end()) return want;
    }
    if(!regions.empty()) return regions.front();
    return order.front();
  }

  auto bios(const Paths& paths, const nall::string& region) -> std::string {
    if(region == "NTSC-J") return paths.biosJP;
    if(region == "PAL") return paths.biosEU;
    return paths.biosUS;
  }

  auto loadMediumFrom(const char* medium, const std::string& location, std::shared_ptr<mia::Pak>& out) -> std::string {
    out = mia::Medium::create(medium);
    if(!out) return std::string{"internal error: no loader for "} + medium;
    auto result = out->load(toNall(location));
    if(result != successful) {
      out.reset();
      return std::string{"Could not load "} + location + " as " + medium;
    }
    return {};
  }

  auto makeRamCart(const RamCart& options) -> std::string {
    u32 size = options.everdrive || options.sizeMiB == 16 ? 16_MiB : options.sizeMiB == 8 ? 8_MiB : 4_MiB;
    ramCartPak = std::make_shared<vfs::directory>();
    ramCartPak->setAttribute("title", options.everdrive ? "Mega EverDrive PRO" : "RAM Cart");
    ramCartPak->setAttribute("region", "NTSC-U, NTSC-J, PAL");
    ramCartPak->setAttribute("bootable", false);  //does not assert CART_IN: the disc boots
    ramCartPak->setAttribute("mega32x", true);
    ramCartPak->setAttribute("ramcart", true);
    ramCartPak->setAttribute("size", size);
    ramCartPak->setAttribute("writeProtect", options.writeProtect);
    if(options.everdrive) {
      ramCartPak->setAttribute("everdrive", true);
      if(!options.boot.empty()) {
        auto rom = nall::file::read(toNall(options.boot));
        if(rom.size() < 0x200) return "Could not read the EverDrive launcher ROM " + options.boot;
        ramCartPak->setAttribute("bootable", true);  //the launcher boots first (CART_IN asserted)
        ramCartPak->append("program.rom", rom);
        log("EverDrive PRO: booting the launcher " + options.boot + " first");
      }
    } else if(!options.boot.empty()) {
      return "A launcher ROM (--ramcart-boot) needs the EverDrive PRO cart (--ramcart-type everdrive).";
    }

    std::vector<u8> memory(size, 0xff);
    if(options.everdrive) {
      //the EverDrive's RAM is never blank: it keeps whatever was there.  A fixed pattern, so a program
      //that reads memory it never wrote shows it (blank 0xff hid a cartridge-boot bug, 25 Oct 2026)
      for(u32 n = 0; n < size; n += 2) {
        u16 w = (u16)((n >> 1) * 0x9e37 + 0x5a5a);
        memory[n] = w >> 8; memory[n + 1] = w;
      }
    }
    if(!options.file.empty()) {
      auto existing = nall::file::read(toNall(options.file));
      if(!existing.empty()) {
        memcpy(memory.data(), existing.data(), std::min<size_t>(existing.size(), memory.size()));
        log("RAM cart: loaded " + options.file);
      } else {
        log("RAM cart: new, blank (" + std::to_string(size >> 20) + " MB) at " + options.file);
      }
    }
    ramCartPak->append("ram.cart", memory);
    ramCart = options;
    return {};
  }

  auto connectPorts(bool corpseKillerCD32X = false) -> void {
    if(auto port = root->find<ares::Node::Port>("Cartridge Slot")) {
      port->allocate();
      port->connect();
    }
    if(auto port = root->find<ares::Node::Port>("Mega CD/Disc Tray")) {
      port->allocate();
      port->connect();
    }
    if(auto port = root->find<ares::Node::Port>("Controller Port 1")) {
      port->allocate("Fighting Pad");
      port->connect();
    }
    if(auto port = root->find<ares::Node::Port>("Controller Port 2")) {
      //Corpse Killer: a pad in port 2 breaks its input polling (as in desktop ares)
      if(!corpseKillerCD32X) {
        port->allocate("Fighting Pad");
        port->connect();
      }
    }
  }

  auto load(const Paths& paths, const LoadRequest& request) -> std::string {
    unload();

    mia::setHomeLocation([home = toNall(paths.home)] { return home; });
    mia::setSaveLocation([saves = nall::string{toNall(paths.home), "saves/"}] { return saves; });
    nall::directory::create(toNall(paths.home));
    nall::directory::create({toNall(paths.home), "saves/"});

    Machine machine = request.machine;
    if(machine == Machine::Auto) machine = guessMachine(request.game, request.disc);

    std::string error;
    nall::string region;
    nall::string systemName;

    if(machine == Machine::MegaCD || machine == Machine::MegaCD32X) {
      if(!isDisc(request.game)) return "A Sega CD game must be a .cue or .chd file.";
      if((error = loadMediumFrom("Mega CD", request.game, disc)) != "") return error;
      if(disc->pak->attribute("audio").boolean()) log("Note: this disc has no Sega CD boot sector (audio CD).");
      region = chooseRegion(disc, request.region);
      systemName = machine == Machine::MegaCD32X ? "Mega CD 32X" : "Mega CD";
    } else {
      const char* medium = machine == Machine::Mega32X ? "Mega 32X" : "Mega Drive";
      if((error = loadMediumFrom(medium, request.game, cart)) != "") return error;
      region = chooseRegion(cart, request.region);
      if(!request.disc.empty()) {
        if((error = loadMediumFrom("Mega CD", request.disc, disc)) != "") return error;
      }
      if(machine == Machine::Mega32X) systemName = disc ? "Mega CD 32X" : "Mega 32X";
      else systemName = disc ? "Mega CD" : "Mega Drive";
    }

    system = mia::System::create(systemName);
    if(!system) return "internal error: no system " + std::string{(const char*)systemName};
    if(systemName.beginsWith("Mega CD")) {
      auto biosFile = bios(paths, region);
      if(biosFile.empty() || !nall::file::exists(toNall(biosFile))) {
        return std::string{"Sega CD BIOS for "} + (const char*)region + " is not set. Choose it in Settings > BIOS.";
      }
      if(system->load(toNall(biosFile)) != successful) return "Could not read the Sega CD BIOS " + biosFile;
    } else {
      if(system->load() != successful) return "Could not set up the " + std::string{(const char*)systemName};
    }

    if(machine == Machine::MegaCD32X) {
      if((error = makeRamCart(request.ramCart)) != "") return error;
    }

    ares::MegaDrive::option("Recompiler", request.recompiler ? "true" : "false");
    ares::MegaDrive::option("TMSS", "false");

    nall::string name{"[Sega] ", systemName, " (", region, ")"};
    if(!ares::MegaDrive::load(root, name)) {
      unload();
      return std::string{"The core refused "} + (const char*)name;
    }

    bool corpseKiller = disc && disc->pak->attribute("serial").beginsWith("GM T-16201F");
    connectPorts(corpseKiller);

    root->power();

    booted = machine;
    bootedDescription = std::string{(const char*)systemName} + " (" + (const char*)region + ")";
    if(ramCartPak) bootedDescription += request.ramCart.everdrive ? std::string{" + Mega EverDrive PRO (16 MB)"}
                                      : " + " + std::to_string(request.ramCart.sizeMiB) + " MB RAM cart";
    if(cart && disc) bootedDescription += " + CD";
    log("Booted " + bootedDescription);
    return {};
  }

  auto saveMemory() -> void {
    if(!root) return;
    root->save();
    if(system) system->save(system->location);
    if(cart) cart->save(cart->location);
    if(disc) disc->save(disc->location);
    if(ramCartPak && !ramCart.file.empty()) {
      if(auto fp = ramCartPak->read("ram.cart")) {
        nall::string cartFile = toNall(ramCart.file);
        auto directory = nall::Location::path(cartFile);
        if(directory) nall::directory::create(directory);
        auto temporary = toNall(ramCart.file + ".tmp");
        if(nall::file::write(temporary, {fp->data(), (u32)fp->size()})) {
          ::rename(temporary.data(), ramCart.file.c_str());
        }
      }
    }
  }

  auto unload() -> void {
    if(root) {
      saveMemory();
      root->unload();
      root.reset();
    }
    streams.clear();
    system.reset();
    cart.reset();
    disc.reset();
    ramCartPak.reset();
    booted = Machine::Auto;
    bootedDescription.clear();
  }
};

Session::Session() : self(new Impl) {
  ares::platform = self;
  mia::construct();
  self->audioRing.allocate(48000);
}

Session::~Session() {
  self->unload();
  ares::platform = nullptr;
  delete self;
}

std::string Session::load(const Paths& paths, const LoadRequest& request) { return self->load(paths, request); }
void Session::unload() { self->unload(); }
bool Session::loaded() const { return (bool)self->root; }
std::string Session::description() const { return self->bootedDescription; }
Machine Session::machine() const { return self->booted; }

void Session::runFrame() {
  if(self->root) self->root->run();
}

void Session::reset() {
  if(self->root) self->root->power(true);
}

void Session::setButtons(uint32_t port, uint32_t mask) {
  if(port < 2) self->buttons[port].store(mask, std::memory_order_relaxed);
}

bool Session::copyFrame(VideoFrame& out, uint64_t serial) const {
  std::lock_guard<std::mutex> lock(self->videoMutex);
  if(self->frame.serial == serial || self->frame.width == 0) return false;
  out.pixels = self->frame.pixels;
  out.width = self->frame.width;
  out.height = self->frame.height;
  out.aspect = self->frame.aspect;
  out.serial = self->frame.serial;
  return true;
}

void Session::setSampleRate(uint32_t sampleRate) {
  if(!sampleRate) return;
  self->sampleRate = sampleRate;
  self->audioRing.allocate(sampleRate / 2);  //half a second
  for(auto& stream : self->streams) stream->setResamplerFrequency(sampleRate);
}

uint32_t Session::readAudio(float* out, uint32_t frames) {
  uint32_t got = self->audioRing.pop(out, frames);
  for(uint32_t k = got * 2; k < frames * 2; k++) out[k] = 0.0f;  //underrun: silence
  return got;
}

uint32_t Session::bufferedAudioFrames() const { return self->audioRing.available(); }

void Session::saveMemory() { self->saveMemory(); }

bool Session::saveState(const std::string& file) {
  if(!self->root) return false;
  auto state = self->root->serialize();
  if(!state) return false;
  return nall::file::write(toNall(file), {state.data(), state.size()});
}

bool Session::loadState(const std::string& file) {
  if(!self->root) return false;
  auto memory = nall::file::read(toNall(file));
  if(memory.empty()) return false;
  serializer state{memory.data(), (u32)memory.size()};
  return self->root->unserialize(state);
}

void Session::timingStats(uint64_t out[6]) const {
  auto& st = ares::MegaDrive::m32x.stats;
  out[0] = st.cartRead; out[1] = st.fbRead; out[2] = st.fbWrite;
  out[3] = st.sdramRead; out[4] = st.sdramWrite; out[5] = st.swaps;
}

void Session::fillStats(uint64_t out[16]) const {
  for(int k = 0; k < 8; k++) out[k] = ares::MegaDrive::m32x.shm.fills[k], out[8 + k] = ares::MegaDrive::m32x.shs.fills[k];
}

uint32_t Session::sdram32(uint32_t address) const {
  auto& m = ares::MegaDrive::m32x.sdram;
  uint32_t i = (address & 0x3fffc) >> 1;
  return (uint32_t)(uint16_t)m[i] << 16 | (uint16_t)m[i + 1];
}

void Session::profileStart() {
  ares::MegaDrive::m32x.shm.profile.clear();
  ares::MegaDrive::m32x.shs.profile.clear();
  ares::SH2::profileOn = 1;
}

bool Session::profileWrite(const std::string& file) {
  FILE* fp = fopen(file.c_str(), "w");
  if(!fp) return false;
  for(auto& [a, c] : ares::MegaDrive::m32x.shm.profile) fprintf(fp, "M %08X %llu\n", a, (unsigned long long)c);
  for(auto& [a, c] : ares::MegaDrive::m32x.shs.profile) fprintf(fp, "S %08X %llu\n", a, (unsigned long long)c);
  fclose(fp);
  return true;
}

uint16_t Session::comm(uint32_t index) const {
  if(!self->root || index > 7) return 0;
  return (uint16_t)ares::MegaDrive::m32x.communication[index];
}

void Session::setLogger(std::function<void(const std::string&)> logger) {
  std::lock_guard<std::mutex> lock(self->logMutex);
  self->logger = std::move(logger);
}

Machine guessMachine(const std::string& game, const std::string& disc) {
  auto e = lowerExtension(game);
  if(e == "cue" || e == "chd") return Machine::MegaCD32X;  //CD32X with the RAM cart: the main use
  if(e == "32x") return Machine::Mega32X;
  return Machine::MegaDrive;
}

std::string machineName(Machine machine) {
  switch(machine) {
  case Machine::Auto:      return "Auto";
  case Machine::MegaDrive: return "Genesis / Mega Drive";
  case Machine::Mega32X:   return "32X";
  case Machine::MegaCD:    return "Sega CD";
  case Machine::MegaCD32X: return "Sega CD 32X + RAM cart";
  }
  return "?";
}

}
