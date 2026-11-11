#pragma once
//OutRun CD32X's FPGA renderer (MicroNut99/CannonballCD32X fpga/orfx): the core's C model (orfx.h, orfx_model.inc are
//copies of fpga/orfx/model/orfx.h and orfx.c - the definition the RTL is checked against) and its cart bus side.
//The model draws a frame at once at GO; the core's time is modelled as orfx_cycles() at 50 MHz (status bit 0 stays
//set until then), against the VDP's lines (nowUs).
#include <string.h>
#include "orfx.h"
extern "C" {
#include "orfx_model.inc"
}

struct OrFx {
  bool on = false;
  u32 keyStep = 0;
  orfx_t* f = nullptr;
  std::vector<u8> gs, gt, gr;            //the graphics, from the cart (banks 16+) at LOAD
  u32 shown = 0;
  u64 busyUntil = 0, lastUs = 0;
  u32 lastTime20 = 0;
  u32 words = 0;

  auto reset(bool) -> void { on = false; keyStep = 0; }

  //a console read of the cart while the core is on: true if the core answers (local = the cart address, 22 bits)
  template<typename RamRead> auto read(u32 local, u16& v, u64 us, RamRead ramRead) -> bool {
    static const u32 key[3] = {0x0f, 0x58, 0xe1};
    if(local >= ORFX_KEY0 && local < ORFX_KEY0 + 0x200) {
      if(local == ORFX_KEY0 + 2 * key[keyStep]) { if(++keyStep == 3) { on = true; keyStep = 0; start(); } }
      else keyStep = local == ORFX_KEY0 + 2 * key[0] ? 1 : 0;
      if(!on) return false;
    }
    if(!on || local < ORFX_CMD_BASE || local >= 0x3e0000) return false;
    if(local < ORFX_PIC_BASE) { word(local >> 1 & 0xffff, us, ramRead); v = 0; return true; }
    if(local < ORFX_PIC_PAL) {
      u32 o = local - ORFX_PIC_BASE;
      v = o + 1 < ORFX_W * ORFX_H ? (u16)(f->pic[shown][o & ~1u] << 8 | f->pic[shown][o | 1]) : 0;
      return true;
    }
    if(local < ORFX_PIC_PAL + 0x200) { v = f->slotpal[shown][(local - ORFX_PIC_PAL) >> 1]; return true; }
    if(local >= ORFX_STATUS && local < ORFX_STATUS + 0x10) {
      switch((local - ORFX_STATUS) >> 1) {
      case 0: v = 0x4f52; break;
      case 1: v = 0x4658; break;
      case 2: v = (us < busyUntil ? 1 : 0) | 4 | shown << 4; break;
      case 3: v = f->frames; break;
      case 4: v = lastTime20; break;
      case 5: v = f->slots[shown]; break;
      case 6: v = f->overflow[shown]; break;
      default: v = 1; break;
      }
      return true;
    }
    v = 0;
    return true;
  }

  auto start() -> void {
    if(!f) f = new orfx_t;
    orfx_init(f, gs.data(), gt.data(), gr.data());
    shown = 0; busyUntil = 0;
  }

  template<typename RamRead> auto word(u16 w, u64 us, RamRead ramRead) -> void {
    words++;
    bool cmd = !f->data_left && f->sel_m >= -1;
    if(cmd && (w & 0xf000) == ORFX_OP_LOAD) {
      auto grab = [&](std::vector<u8>& v, u32 at, u32 n) {
        v.resize(n);
        for(u32 i = 0; i < n; i += 2) { u16 x = ramRead(at + i); v[i] = x >> 8; v[i + 1] = x; }
      };
      grab(gs, ORFX_GFX_SPRITES, 0x100000);
      grab(gt, ORFX_GFX_TILES, 0x40000);
      grab(gr, ORFX_GFX_ROAD, 0x10000);
      f->gfx_sprites = gs.data(); f->gfx_tiles = gt.data(); f->gfx_road = gr.data();
      busyUntil = us + 20000;            //(the RTL copies the tiles into its SRAM: about 20 ms)
      return;
    }
    if(cmd && (w & 0xf000) == ORFX_OP_SHOW) { shown = w & 1; return; }
    if(cmd && (w & 0xf000) == ORFX_OP_GO) {
      orfx_word(f, w);
      u32 c = orfx_cycles(f);
      busyUntil = us + c / 50;
      lastTime20 = c / 1000;
      return;
    }
    orfx_word(f, w);
  }
};
