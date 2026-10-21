//ares-headless: boots a game with no window, for testing the frontend on a desktop host.
//
//  ares-headless --game disc.cue [--disc x.cue] [--machine cd32x|cd|32x|md]
//                --bios-us us.bin [--bios-jp jp.bin] [--bios-eu eu.bin]
//                [--ramcart cart.bin] [--ramcart-size 4|8|16] [--frames 3600]
//                [--press frame:buttons ...] [--screenshot frame:file.ppm ...]
//                [--wav out.wav] [--home dir] [--interpreter]
//                [--save-state frame:file] [--load-state file] [--stats] [--profile from:file]
//
//--stats prints, once a second, what the SH2s did in that second: 32X pictures shown (fps) and
//16-bit accesses per shown picture to the cart, the frame buffer and SDRAM. The costs ares
//charges for them come from ARES_32X_TIMING (ares/md/m32x/bus-internal.cpp).
//
//Prints the 32X COMM registers once a second (the CD32X boot stacks post their status
//in COMM12: 4B10 = game running) and how much audio came out.

#include "frontend.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace aresdroid;

static uint32_t parseButtons(const std::string& text) {
  uint32_t mask = 0;
  for(char c : text) {
    switch(c) {
    case 'U': mask |= Up; break;    case 'D': mask |= Down; break;
    case 'L': mask |= Left; break;  case 'R': mask |= Right; break;
    case 'A': mask |= A; break;     case 'B': mask |= B; break;
    case 'C': mask |= C; break;     case 'X': mask |= X; break;
    case 'Y': mask |= Y; break;     case 'Z': mask |= Z; break;
    case 'M': mask |= Mode; break;  case 'S': mask |= Start; break;
    }
  }
  return mask;
}

static void writePPM(const std::string& file, const VideoFrame& frame) {
  FILE* fp = fopen(file.c_str(), "wb");
  if(!fp) return;
  fprintf(fp, "P6\n%u %u\n255\n", frame.width, frame.height);
  for(uint32_t p : frame.pixels) {
    uint8_t rgb[3] = {(uint8_t)(p >> 16), (uint8_t)(p >> 8), (uint8_t)p};
    fwrite(rgb, 1, 3, fp);
  }
  fclose(fp);
}

struct Wav {
  FILE* fp = nullptr;
  uint32_t frames = 0;
  void open(const std::string& file) {
    fp = fopen(file.c_str(), "wb");
    if(fp) fwrite(std::string(44, '\0').data(), 1, 44, fp);
  }
  void write(const float* samples, uint32_t count) {
    if(!fp) return;
    for(uint32_t k = 0; k < count * 2; k++) {
      int16_t s = (int16_t)std::lround(std::max(-1.0f, std::min(1.0f, samples[k])) * 32767.0f);
      fwrite(&s, 2, 1, fp);
    }
    frames += count;
  }
  void close() {
    if(!fp) return;
    auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, fp); };
    auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, fp); };
    fseek(fp, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, fp); u32(36 + frames * 4); fwrite("WAVEfmt ", 1, 8, fp);
    u32(16); u16(1); u16(2); u32(48000); u32(48000 * 4); u16(4); u16(16);
    fwrite("data", 1, 4, fp); u32(frames * 4);
    fclose(fp);
    fp = nullptr;
  }
};

int main(int argc, char** argv) {
  Paths paths;
  LoadRequest request;
  uint32_t frames = 600;
  std::map<uint32_t, uint32_t> presses;      //frame -> buttons from that frame on
  std::map<uint32_t, std::string> shots;     //frame -> file
  std::string wavFile;
  std::map<uint32_t, std::string> saves;     //frame -> state file
  std::string loadFile;
  bool stats = false;
  uint32_t profFrom = ~0u;
  std::string profFile;
  paths.home = "./ares-home/";

  for(int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if(a == "--game") request.game = next();
    else if(a == "--disc") request.disc = next();
    else if(a == "--bios-us") paths.biosUS = next();
    else if(a == "--bios-jp") paths.biosJP = next();
    else if(a == "--bios-eu") paths.biosEU = next();
    else if(a == "--home") paths.home = next();
    else if(a == "--region") request.region = next();
    else if(a == "--ramcart") request.ramCart.file = next();
    else if(a == "--ramcart-size") request.ramCart.sizeMiB = (uint32_t)atoi(next().c_str());
    else if(a == "--frames") frames = (uint32_t)atoi(next().c_str());
    else if(a == "--interpreter") request.recompiler = false;
    else if(a == "--wav") wavFile = next();
    else if(a == "--load-state") loadFile = next();
    else if(a == "--stats") stats = true;
    else if(a == "--profile") {
      auto v = next(); auto c = v.find(':');
      profFrom = (uint32_t)atoi(v.substr(0, c).c_str()); profFile = v.substr(c + 1);
    }
    else if(a == "--save-state") {
      auto v = next(); auto c = v.find(':');
      saves[(uint32_t)atoi(v.substr(0, c).c_str())] = v.substr(c + 1);
    }
    else if(a == "--machine") {
      auto m = next();
      if(m == "cd32x") request.machine = Machine::MegaCD32X;
      if(m == "cd") request.machine = Machine::MegaCD;
      if(m == "32x") request.machine = Machine::Mega32X;
      if(m == "md") request.machine = Machine::MegaDrive;
    } else if(a == "--press") {
      auto v = next(); auto c = v.find(':');
      presses[(uint32_t)atoi(v.substr(0, c).c_str())] = parseButtons(v.substr(c + 1));
    } else if(a == "--screenshot") {
      auto v = next(); auto c = v.find(':');
      shots[(uint32_t)atoi(v.substr(0, c).c_str())] = v.substr(c + 1);
    } else {
      fprintf(stderr, "unknown argument %s\n", a.c_str());
      return 2;
    }
  }
  if(paths.home.back() != '/') paths.home += '/';

  Session session;
  session.setSampleRate(48000);
  auto error = session.load(paths, request);
  if(!error.empty()) {
    fprintf(stderr, "load failed: %s\n", error.c_str());
    return 1;
  }
  printf("booted: %s\n", session.description().c_str());
  if(!loadFile.empty()) {
    if(!session.loadState(loadFile)) { fprintf(stderr, "load state failed: %s\n", loadFile.c_str()); return 1; }
    printf("state loaded: %s\n", loadFile.c_str());
  }
  uint64_t last[6] = {}, lastF[16] = {};
  session.timingStats(last);
  session.fillStats(lastF);

  Wav wav;
  if(!wavFile.empty()) wav.open(wavFile);
  std::vector<float> audio(48000 * 2);
  double audioEnergy = 0.0;
  uint64_t audioFrames = 0;
  VideoFrame frame;
  uint64_t serial = 0;
  uint32_t buttons = 0;

  for(uint32_t f = 0; f < frames; f++) {
    if(auto it = presses.find(f); it != presses.end()) buttons = it->second;
    session.setButtons(0, buttons);
    if(f == profFrom) session.profileStart();
    session.runFrame();

    uint32_t got = session.readAudio(audio.data(), session.bufferedAudioFrames());
    for(uint32_t k = 0; k < got * 2; k++) audioEnergy += audio[k] * audio[k];
    audioFrames += got;
    wav.write(audio.data(), got);

    if(session.copyFrame(frame, serial)) serial = frame.serial;
    if(auto it = saves.find(f); it != saves.end()) {
      printf("frame %u: state %s %s\n", f, it->second.c_str(), session.saveState(it->second) ? "saved" : "NOT SAVED");
    }
    if(auto it = shots.find(f); it != shots.end()) {
      writePPM(it->second, frame);
      printf("frame %u: screenshot %s (%ux%u, aspect %.3f)\n", f, it->second.c_str(), frame.width, frame.height, frame.aspect);
    }

    if(f % 60 == 59) {
      double rms = audioFrames ? std::sqrt(audioEnergy / (audioFrames * 2.0)) : 0.0;
      printf("t=%3us video=%ux%u comm: %04X %04X %04X %04X %04X %04X [12]=%04X [14]=%04X audio rms=%.4f\n",
        (f + 1) / 60, frame.width, frame.height,
        session.comm(0), session.comm(1), session.comm(2), session.comm(3), session.comm(4), session.comm(5),
        session.comm(6), session.comm(7), rms);
      if(stats) {
        uint64_t now[6];
        session.timingStats(now);
        uint64_t d[6];
        for(int k = 0; k < 6; k++) d[k] = now[k] - last[k], last[k] = now[k];
        uint64_t n = d[5] ? d[5] : 1;
        printf("  stats t=%u fps=%llu per picture: cart %llu fbR %llu fbW %llu sdR %llu sdW %llu\n", (f + 1) / 60,
          (unsigned long long)d[5], (unsigned long long)(d[0] / n), (unsigned long long)(d[1] / n),
          (unsigned long long)(d[2] / n), (unsigned long long)(d[3] / n), (unsigned long long)(d[4] / n));
        uint64_t nf[16];
        session.fillStats(nf);
        printf("    line fills per picture: master cart %llu sdram %llu  slave cart %llu sdram %llu\n",
          (unsigned long long)((nf[2] - lastF[2]) / n), (unsigned long long)((nf[6] - lastF[6]) / n),
          (unsigned long long)((nf[10] - lastF[10]) / n), (unsigned long long)((nf[14] - lastF[14]) / n));
        for(int k = 0; k < 16; k++) lastF[k] = nf[k];
      }
      fflush(stdout);
      audioEnergy = 0.0;
      audioFrames = 0;
    }
  }

  if(!profFile.empty()) printf("profile %s %s\n", profFile.c_str(), session.profileWrite(profFile) ? "written" : "NOT WRITTEN");
  wav.close();
  session.unload();  //writes the RAM cart and backup RAM
  return 0;
}
