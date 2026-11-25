#pragma once
//Super Hang-On CD32X FX (MicroNut99/Super-Hang-On-CD32X-FX docs/08): the cart's FPGA - the arcade's main 68000, its
//315-5195 mapper, I/O and video (the repo's host/shangon_ref.cpp built as a library, sh_board.h: equal to MAME's
//pictures) - while the Genesis' 68000 runs the sub CPU's road program (docs/07) through the 32X's 0x900000 window.
//The cart bus side, by the cart's local address (A21-A1, whatever the SSF banks):
//  0x160000-0x19FFFF  the sub's world at its 0x60000-0x9FFFF (the 68000 at 0x900000 + its arcade address, 32X bank 1):
//                     its RAM (shared with the main CPU), the road RAM, the road control (a read swaps the road RAM)
//  0x1A0000-0x1A00FF  the registers (the 68000 at 0x9A0000 - the 0x900000 window, where ares passes its writes -, the SH2s at 0x021A0000):
//                     +0x00 'SH' (0x5348)  +0x02 pictures made  +0x04 the newest picture (0-2)  +0x06 bit 0: the main
//                     CPU reset the sub (cleared by the read)  +0x08 the main CPU's line  +0x0A the sub's IRQ4 line
//                     +0x10 (write) the pad: Genesis buttons, 1 = pressed (bits: U D L R B C A St = 0-7)
//                     +0x12 (write) the Genesis' vblank: the frame's line 223 starts now
//                     +0x40 + 2k (read) lock picture k (0/1) for the copy, 3 none
//  0x300000-0x323FFF  the pictures (window 6, the SH2s' 0x02300000): two buffers of 0x12000 bytes, as the core's
//                     sh_writer.sv writes them: + 0 the runs (a word a run of 1-256 pixels: the length - 1, the colour)
//                     or the plain picture (320 x 224 bytes), + 0x11800 the colours (BGR555, n at + 2n), + 0x11A00 the
//                     picture's number, + 0x11A02 its colours, + 0x11A04 its runs (0: plain), + 0x11C00 the line table
//  (registers also: + 0x0C pictures not shown, + 0x0E the last picture's runs, + 0x14 the next sound command (bit 15
//  while there was one: a packet's first byte, sh_core.sv), + 0x16 the core's version 0x0101,
//  + 0x18 / 0x1A (write) the key 'SH' 'ON' that starts the core (map_shfx.sv), + 0x18 (read) 1 once it runs)
//Kit 6 (0x0106, docs/12): + 0x48 the sound board's bytes and resets in order (bit 15 one was there, bit 9 a reset,
//bit 8 its level), + 0x4A that one's time after the one before (4 us; 4095: 16 ms or more) - shb_take_latch.
//Kit 5 (version 0x0105, docs/10): the diagnostics at + 0x1A-0x3E (sh_core.sv): the model gives what it knows - the
//sound commands queued, the pictures not shown, the frames, the console's accesses a frame, the line of the Genesis'
//vblank - and 0 for the timings it has no clock for (the copy, the drawing, the writer, the 68000's load, its memory);
//SHFX_FAKEDIAG=1 gives those fixed values to see the screen's bars. + 0x1A / + 0x1C (write) the DIP switches, the
//handlebar's speed (the options menu).
//The arcade's ROMs: the cart's pack from bank 16 (tools/sh_pack.py): + 0 the main ROM, + 0x60000 the sub ROM (the
//original: the main CPU reads it), + 0xC0000 the tiles, + 0xD8000 the road ROM, + 0x100000 the sprites.
//SHFX_INPUT=<shangon_ref events> (coin:N, start:N, gas:N-M, brake:N-M, steer:N-M=VV) drives the inputs by frame instead
//of the pad. A green run here is never a console pass.
#include "sh_board.h"
#include <vector>
#include <string>

struct ShFx {
  bool on = false;
  int line = 0;
  long frame = 0;
  u16 pad = 0;
  int steer = 0x80;
  u32 lock = 3, front = 3;
  u16 frames = 0, dropped = 0, lastRuns = 0;
  bool subReset = false, nextRuns = true;
  std::vector<u8> pic, buf[2];         //the picture (a byte a pixel), the two buffers as the SRAM holds them (sh_writer.sv)
  std::vector<u16> pal;
  struct Ev { std::string k; long a, b; int v; };
  std::vector<Ev> evs;
  std::vector<u8> roms;
  u32 nRd = 0, nWr = 0, nReg = 0, nVbl = 0, nRoadCtl = 0, nLock = 0, rleSum = 0, rleMax = 0, rleN = 0;   //(SHFX_TRACE: a line a second)
  u16 lqLast = 0;
  u16 dsw = 0xfe, steerStep = 8, sndPush = 0, frameCount = 0, conFrame = 0, conLast = 0, syncLine = 0;

  auto start(const std::vector<u8>& pack) -> void {
    roms = pack;
    shb_roms r{roms.data(), roms.data() + 0x60000, roms.data() + 0xc0000, roms.data() + 0x100000, roms.data() + 0xd8000};
    shb_init(&r);
    pic.assign(320 * 224, 0); pal.assign(256, 0);
    for(auto& b : buf) b.assign(0x12000, 0);
    on = true; line = 0; frame = 0;
    if(auto e = getenv("SHFX_INPUT")) {
      std::string s = e; size_t p = 0;
      while(p < s.size()) {
        size_t q = s.find(',', p); if(q == std::string::npos) q = s.size();
        std::string t = s.substr(p, q - p); p = q + 1;
        Ev v{"", 0, 0, 0}; size_t c = t.find(':'); v.k = t.substr(0, c);
        if(c != std::string::npos) {
          std::string r2 = t.substr(c + 1); size_t eq = r2.find('=');
          if(eq != std::string::npos) { v.v = strtol(r2.c_str() + eq + 1, nullptr, 16); r2 = r2.substr(0, eq); }
          size_t d = r2.find('-'); v.a = atol(r2.c_str()); v.b = d != std::string::npos ? atol(r2.c_str() + d + 1) : v.a;
        }
        evs.push_back(v);
      }
    }
    fprintf(stderr, "[shfx] the board runs (main ROM %02x%02x..., SHFX_INPUT %s)\n", roms[0], roms[1], getenv("SHFX_INPUT") ? "on" : "off");
  }

  auto inputs() -> shb_inputs {
    shb_inputs in{0xff, (u8)dsw, {0x80, 0, 0, 0}};
    if(!evs.empty()) {
      for(auto& e : evs) {
        if(e.k == "coin" && frame >= e.a && frame < e.a + 6) in.unknown &= ~0x01;
        if(e.k == "start" && frame >= e.a && frame < e.a + 6) in.unknown &= ~0x10;
        if(e.k == "gas" && frame >= e.a && frame <= e.b) in.adc[1] = 0xff;
        if(e.k == "brake" && frame >= e.a && frame <= e.b) in.adc[2] = 0xff;
        if(e.k == "steer" && frame >= e.a && frame <= e.b) in.adc[0] = e.v;
      }
      return in;
    }
    //the pad: Start the start button, C a coin, B the throttle, A the brake, left / right the handlebar (moving 8 a frame)
    if(pad & 0x80) in.unknown &= ~0x10;
    if(pad & 0x20) in.unknown &= ~0x01;
    if(pad & 0x10) in.adc[1] = 0xff;
    if(pad & 0x40) in.adc[2] = 0xff;
    if(pad & 0x0700) in.unknown &= ~0x20;   //(X, Y or Z: the Supercharger)
    in.adc[0] = steer;
    return in;
  }

  //the picture as sh_writer.sv writes it (docs/10): into the buffer the SH2 has not locked and that is not the newest
  //(both taken: the newest, taken back), as runs when the last picture's runs were under 28,000 words (more than 35,615:
  //not shown, the next one plain), else plain; the picture's colours numbered in the order met
  auto picture() -> void {
    u32 b;
    if(front == 3) b = lock == 0 ? 1 : 0;
    else if(lock != 3 && lock != front) { b = front; front = 3; }
    else b = front ^ 1;
    auto& o = buf[b];
    auto put = [&](u32 a, u16 v) { o[a] = v >> 8; o[a + 1] = v & 0xff; };
    u32 ncol = (u32)shb_picture8(pic.data(), pal.data());
    for(u32 n = 1; n <= ncol && n < 256; n++) put(0x11800 + 2 * n, pal[n]);
    bool runsMode = nextRuns, failed = false;
    u32 n = 0; const u32 cap = 35616;
    for(u32 y : range(224)) {
      const u8* r = pic.data() + y * 320; u32 x = 0;
      if(runsMode) put(0x11c00 + 2 * y, (u16)(0x100 + n));
      while(x < 320) {
        u32 e = x + 1; while(e < 320 && e - x < 256 && r[e] == r[x]) e++;
        if(runsMode) { if(n < cap) put(2 * n, (u16)((e - x - 1) << 8 | r[x])); else failed = true; }
        n++; x = e;
      }
      if(!runsMode) for(u32 i : range(320)) o[y * 320 + i] = r[i];
    }
    put(0x11a00, (u16)(frames + 1)); put(0x11a02, (u16)ncol); put(0x11a04, (u16)(runsMode && !failed ? n : 0));
    lastRuns = (u16)n; rleSum += n; rleN++; rleMax = max(rleMax, n);
    nextRuns = n < 28000 && !(runsMode && failed);
    if(runsMode && failed) dropped++;
    else { front = b; frames++; }
  }

  //one line of the arcade's frame; at line 224 the picture into a buffer neither the newest nor the locked one
  auto step() -> void {
    if(!on) return;
    shb_inputs in = inputs();
    long before = shb_pictures();
    shb_line(line, &in);
    if(shb_take_sub_reset()) subReset = true;
    if(shb_pictures() != before) picture();
    if(line == 223) { frameCount++; conLast = conFrame; conFrame = 0; }
    if(++line == 262) {
      line = 0; frame++;
      if(getenv("SHFX_TRACE") && frame % 60 == 0) {
        fprintf(stderr, "[shfx] frame %ld: sub world reads %u writes %u (road control %u), register accesses %u, vblanks %u, pictures %u, shown %u, run words avg %u max %u\n",
                frame, nRd, nWr, nRoadCtl, nReg, nVbl, frames, nLock, rleN ? rleSum / rleN : 0, rleMax);
        nRd = nWr = nReg = nVbl = nRoadCtl = nLock = rleSum = rleN = rleMax = 0;
      }
      const int k = steerStep;
      if(pad & 0x04) steer = max(0x20, steer - k); else if(pad & 0x08) steer = min(0xe0, steer + k);
      else steer = steer < 0x80 ? min(0x80, steer + k) : max(0x80, steer - k);
    }
  }

  auto read(u32 local, u16& v) -> bool {
    if(!on) return false;
    if(local >= 0x160000 && local < 0x1a0100) conFrame++;
    if(local >= 0x160000 && local < 0x1a0000) { nRd++; if(local >= 0x190000) nRoadCtl++; v = (u16)shb_sub_read(local - 0x100000); return true; }
    if((local & ~0xffu) == 0x1a0000) {
      u32 r = local & 0xff;
      if(r == 0x00) v = 0x5348;
      else if(r == 0x02) v = frames;
      else if(r == 0x04) v = (u16)front;
      else if(r == 0x06) { v = subReset ? 1 : 0; subReset = false; }
      else if(r == 0x08) v = (u16)line;
      else if(r == 0x0a) v = (u16)shb_sub_irq4();
      else if(r == 0x0c) v = dropped;
      else if(r == 0x0e) v = lastRuns;
      else if(r == 0x14) { int c = shb_take_sound(); v = c < 0 ? 0 : (u16)(0x8000 | c); if(c >= 0) sndPush++; }   //(the next sound command: MD+)
      else if(r == 0x16) v = 0x0106;
      else if(r == 0x48) { int e = shb_take_latch(); if(e < 0) v = 0; else { v = (u16)(0x8000 | (e & 0x3ff)); lqLast = (u16)((e >> 16) & 0xfff); }   //(the latch FIFO: docs/12)
        static FILE* lql = nullptr; static int lqo = -1;   //ARES_SHFX_LQLOG=file: each entry taken "e delta", "-" when empty (a tick's end)
        if(lqo < 0) { const char* p = getenv("ARES_SHFX_LQLOG"); lqo = p ? 1 : 0; if(p) lql = fopen(p, "w"); }
        if(lql) { if(e < 0) fputs("-\n", lql); else fprintf(lql, "%03x %u\n", e & 0x3ff, (e >> 16) & 0xfff); }
      }
      else if(r == 0x4a) v = lqLast;
      else if(r == 0x1e) v = sndPush & 0xff;
      else if(r == 0x22) v = dropped;
      else if(r == 0x32) v = conLast;
      else if(r == 0x3a) v = syncLine;
      else if(r == 0x3e) v = (u16)frame;
      else if(r >= 0x24 && r <= 0x30 && getenv("SHFX_FAKEDIAG")) {
        static const u16 fake[7] = {7, 121, 123, 30, 29000, 0x20f8, 0x560f};   //(copy, drawing, writer lines; misses,
        v = fake[(r - 0x24) >> 1];                                             // accesses, memory, busy: kit 5 in the bench)
      }
      else if(r == 0x18) v = 1;   //(the core runs: the model needs no key)
      else if(r >= 0x40 && r < 0x48) { lock = (r - 0x40) >> 1; v = (u16)lock; nLock++; }
      else v = 0;
      return true;
    }
    if(local >= 0x300000 && local < 0x324000) {
      u32 k = (local - 0x300000) / 0x12000, o = (local - 0x300000) % 0x12000;
      v = (u16)(buf[k][o] << 8 | buf[k][o + 1]);
      return true;
    }
    return false;
  }

  auto write(u32 local, bool upper, bool lower, u16 data) -> bool {
    if(!on) return false;
    if(local >= 0x160000 && local < 0x1a0100) conFrame++;
    if(local >= 0x160000 && local < 0x1a0000) {
      nWr++;
      shb_sub_write(local - 0x100000, data, (upper ? 0xff00 : 0) | (lower ? 0x00ff : 0));
      return true;
    }
    if((local & ~0xffu) == 0x1a0000) {
      u32 r = local & 0xff; nReg++;
      if(r == 0x12) nVbl++;
      if(r == 0x10) pad = data;
      if(r == 0x1a) dsw = data & 0xff;
      if(r == 0x1c) steerStep = (data & 15) ? (data & 15) : 1;
      if(r == 0x12) {   //the Genesis' vblank: the arcade's line 223 from here (the two frames kept together)
        syncLine = (u16)line;
        if(line < 223) { while(line < 223) step(); }   //(behind: the lines run now)
        else if(line > 223) line = 223;                 //(ahead: those lines again - the main CPU gets their time twice)
      }
      return true;
    }
    return false;
  }
};
