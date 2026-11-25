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
//                     +0x40 + 2k (read) lock picture k (0-2) for the copy, 3 none
//  0x300000-0x36BFFF  the pictures (window 6, the SH2s' 0x02300000): picture k at + k x 0x24000 - 320 x 224 bytes,
//                     then 256 BGR555 words at + 0x11800, its frame at + 0x11A00, its colours at + 0x11A02, its run
//                     words at + 0x11A04 (0: copy it plain - its runs would be no fewer words), its line table for the
//                     32X's run-length mode at + 0x11C00 (224 words, 0x100 + the line's first run), the runs at
//                     + 0x12000 (a word a run of 1-256 pixels: the length - 1, the colour)
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
  u32 lock = 3, front = 3, wbuf = 0;
  u16 frames = 0;
  bool subReset = false;
  std::vector<u8> pic[3];
  std::vector<u16> pal[3];
  u16 picFrame[3] = {0, 0, 0}, picColours[3] = {0, 0, 0}, picRuns[3] = {0, 0, 0};
  std::vector<u16> runs[3], ltab[3];
  struct Ev { std::string k; long a, b; int v; };
  std::vector<Ev> evs;
  std::vector<u8> roms;
  u32 nRd = 0, nWr = 0, nReg = 0, nVbl = 0, nRoadCtl = 0, nLock = 0, rleSum = 0, rleMax = 0, rleN = 0;   //(SHFX_TRACE: a line a second)

  auto start(const std::vector<u8>& pack) -> void {
    roms = pack;
    shb_roms r{roms.data(), roms.data() + 0x60000, roms.data() + 0xc0000, roms.data() + 0x100000, roms.data() + 0xd8000};
    shb_init(&r);
    for(auto& p : pic) p.assign(320 * 224, 0);
    for(auto& p : pal) p.assign(256, 0);
    for(auto& p : runs) p.assign(0x11800 / 2, 0);
    for(auto& p : ltab) p.assign(224, 0);
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
    shb_inputs in{0xff, 0xfe, {0x80, 0, 0, 0}};
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
    in.adc[0] = steer;
    return in;
  }

  //one line of the arcade's frame; at line 224 the picture into a buffer neither the newest nor the locked one
  auto step() -> void {
    if(!on) return;
    shb_inputs in = inputs();
    long before = shb_pictures();
    shb_line(line, &in);
    if(shb_take_sub_reset()) subReset = true;
    if(shb_pictures() != before) {
      picColours[wbuf] = (u16)shb_picture8(pic[wbuf].data(), pal[wbuf].data());
      {   //the run-length form: a word a run of <= 256 pixels; kept when it is fewer words than the plain picture
        u32 n = 0; const u32 limit = 320 * 224 / 2 - 224;
        for(u32 y : range(224)) {
          const u8* r = pic[wbuf].data() + y * 320; u32 x = 0;
          ltab[wbuf][y] = (u16)(0x100 + n);
          while(x < 320) {
            u32 e = x + 1; while(e < 320 && e - x < 256 && r[e] == r[x]) e++;
            if(n < limit) runs[wbuf][n] = (u16)((e - x - 1) << 8 | r[x]);
            n++; x = e;
          }
        }
        picRuns[wbuf] = n < limit ? (u16)n : 0;
        rleSum += n; rleN++; rleMax = max(rleMax, n);
      }
      picFrame[wbuf] = ++frames; front = wbuf;
      for(u32 k : range(3)) if(k != front && k != lock) { wbuf = k; break; }
    }
    if(++line == 262) {
      line = 0; frame++;
      if(getenv("SHFX_TRACE") && frame % 60 == 0) {
        fprintf(stderr, "[shfx] frame %ld: sub world reads %u writes %u (road control %u), register accesses %u, vblanks %u, pictures %u, shown %u, run words avg %u max %u\n",
                frame, nRd, nWr, nRoadCtl, nReg, nVbl, frames, nLock, rleN ? rleSum / rleN : 0, rleMax);
        nRd = nWr = nReg = nVbl = nRoadCtl = nLock = rleSum = rleN = rleMax = 0;
      }
      if(pad & 0x04) steer = max(0x20, steer - 8); else if(pad & 0x08) steer = min(0xe0, steer + 8);
      else steer = steer < 0x80 ? min(0x80, steer + 8) : max(0x80, steer - 8);
    }
  }

  auto read(u32 local, u16& v) -> bool {
    if(!on) return false;
    if(local >= 0x160000 && local < 0x1a0000) { nRd++; if(local >= 0x190000) nRoadCtl++; v = (u16)shb_sub_read(local - 0x100000); return true; }
    if((local & ~0xffu) == 0x1a0000) {
      u32 r = local & 0xff;
      if(r == 0x00) v = 0x5348;
      else if(r == 0x02) v = frames;
      else if(r == 0x04) v = (u16)front;
      else if(r == 0x06) { v = subReset ? 1 : 0; subReset = false; }
      else if(r == 0x08) v = (u16)line;
      else if(r == 0x0a) v = (u16)shb_sub_irq4();
      else if(r >= 0x40 && r < 0x48) { lock = (r - 0x40) >> 1; v = (u16)lock; nLock++; }
      else v = 0;
      return true;
    }
    if(local >= 0x300000 && local < 0x36c000) {
      u32 k = (local - 0x300000) / 0x24000, o = (local - 0x300000) % 0x24000;
      if(o < 0x11800) v = (u16)(pic[k][o] << 8 | pic[k][o + 1]);
      else if(o < 0x11a00) v = pal[k][(o - 0x11800) >> 1];
      else if(o == 0x11a00) v = picFrame[k];
      else if(o == 0x11a02) v = picColours[k];
      else if(o == 0x11a04) v = picRuns[k];
      else if(o >= 0x11c00 && o < 0x11c00 + 448) v = ltab[k][(o - 0x11c00) >> 1];
      else if(o >= 0x12000 && o < 0x23800) v = runs[k][(o - 0x12000) >> 1];
      else v = 0;
      return true;
    }
    return false;
  }

  auto write(u32 local, bool upper, bool lower, u16 data) -> bool {
    if(!on) return false;
    if(local >= 0x160000 && local < 0x1a0000) {
      nWr++;
      shb_sub_write(local - 0x100000, data, (upper ? 0xff00 : 0) | (lower ? 0x00ff : 0));
      return true;
    }
    if((local & ~0xffu) == 0x1a0000) {
      u32 r = local & 0xff; nReg++;
      if(r == 0x12) nVbl++;
      if(r == 0x10) pad = data;
      if(r == 0x12) {   //the Genesis' vblank: the arcade's line 223 from here (the two frames kept together)
        if(line < 223) { while(line < 223) step(); }   //(behind: the lines run now)
        else if(line > 223) line = 223;                 //(ahead: those lines again - the main CPU gets their time twice)
      }
      return true;
    }
    return false;
  }
};
