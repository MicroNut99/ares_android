//rrfx_board.h - Road Rash CD32X's renderer core (MicroNut99/RoadRash32X_CD32X fpga/rrfx) for ares's EverDrive PRO:
//the 'rrfx' configuration (a mega-pro.rbf with a mega-pro.rbf.ares naming it). The model is the core's definition,
//fpga/rrfx/model/rrfx.c (here rrfx_model.inc), which the RTL is checked against picture for picture; this file is
//the bus side as rtl/rrfx_core.sv has it: the key, the command window, SYNC, the picture, the line table, the
//status, and the frame's time (the core draws for 15-25 ms; words that arrive meanwhile wait, as in the core).
//Printed "[rrfx TRAP]": what the game must not do (the console's failure modes an emulator would let pass).
#pragma once
#include <vector>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
namespace rrfxm {
inline std::function<uint16_t(uint32_t)> word;          //the cart's memory, a word at a byte address (16 MB)
inline uint8_t mem8(uint32_t a) { uint16_t w = word(a & 0xFFFFFE); return (uint8_t)(a & 1 ? w : w >> 8); }
#define RRFX_MEM8(a) rrfxm::mem8(a)
#include "rrfx.h"
#include "rrfx_model.inc"
#undef W
#undef H
#undef MUL16
}

struct RrFx {
  rrfxm::rrfx_t* f = nullptr;
  bool on = false; int kstep = 0;
  bool busy = false; uint64_t doneAt = 0; int drawing = 0;
  uint16_t frames = 0, time20 = 0, rleLast = 0;
  struct Q { uint8_t sync, a17; uint16_t a; };
  std::vector<Q> q;                                     //words taken while the core draws: they wait
  uint64_t traps[4] = {};
  auto trap(int k, const char* what) -> void {
    uint64_t n = ++traps[k];
    if(n == 1 || (n & (n - 1)) == 0) fprintf(stderr, "[rrfx TRAP] (x%llu) %s\n", (unsigned long long)n, what);
  }
  auto reset(bool) -> void { on = false; kstep = 0; busy = false; q.clear(); frames = 0; if(f) rrfxm::rrfx_init(f, nullptr); }
  auto cost() -> uint64_t {                             //the frame's time in the core, us (from the RTL's bench)
    if(auto e = getenv("ARES_RRFX_US")) return strtoull(e, nullptr, 0);
    auto& n = f->n;
    return 2500 + ((uint64_t)n.fill_words * 6 + (uint64_t)n.picture_pixels * 6 + 28160 * 6 + (uint64_t)n.rle_words * 6 + (uint64_t)n.pieces * 900) / 50;
  }
  auto apply(const Q& w, uint64_t us) -> void {
    if(w.sync) { rrfxm::rrfx_sync(f); return; }
    int before = f->frames;
    rrfxm::rrfx_cmd(f, w.a17, w.a);
    if(f->frames != before) {
      uint64_t c = cost();
      busy = true; doneAt = us + c; time20 = (uint16_t)(c / 20); rleLast = (uint16_t)f->n.rle_words;
      drawing = f->drawn;
    }
  }
  auto advance(uint64_t us) -> void {
    if(busy && us >= doneAt) { busy = false; frames++; }
    size_t i = 0;
    while(!busy && i < q.size()) apply(q[i++], us > doneAt ? us : doneAt);
    if(i) q.erase(q.begin(), q.begin() + i);
  }
  auto take(Q w, uint64_t us) -> void {
    if(busy || !q.empty()) { if(q.size() < 1024) q.push_back(w); else trap(2, "more than 1,024 words sent while the core draws: lost"); }
    else apply(w, us);
  }
  template<typename Rd> auto read(uint32_t local, uint16_t& v, uint64_t us, Rd ram) -> bool {
    if((local & 0x3ffe00) == 0x3D3E00) {                //the key: 0x52, 0x46, 0x58 in that order
      uint32_t k = local >> 1 & 255;
      if(kstep == 0 && k == 0x52) kstep = 1;
      else if(kstep == 1 && k == 0x46) kstep = 2;
      else if(kstep == 2 && k == 0x58) {
        kstep = 0;
        if(!f) f = (rrfxm::rrfx_t*)calloc(1, sizeof(rrfxm::rrfx_t));
        rrfxm::rrfx_init(f, nullptr);
        if(!on) fprintf(stderr, "[rrfx] the key: the renderer core is on\n");
        on = true;
      }
      else kstep = k == 0x52 ? 1 : 0;
      if(!on) return false;
      v = 0; return true;
    }
    if(!on) return false;
    rrfxm::word = ram;
    advance(us);
    if(local >> 18 == 0xE) { take({0, (uint8_t)(local >> 17 & 1), (uint16_t)(local >> 1)}, us); v = 0; return true; }
    if(local >> 17 != 0x1E) return false;
    uint32_t off = local & 0x1ffff;
    v = 0;
    if(off < 0xDC00) {
      if(busy && f->shown == drawing) trap(0, "the console reads the picture buffer the core is drawing into: on the cart, a half-drawn picture");
      v = (uint16_t)(f->pic[f->shown][off & ~1u] << 8 | f->pic[f->shown][off | 1]);
    }
    else if(off >> 9 == 0x92) v = f->lt[f->shown][off >> 1 & 255];
    else if(off >> 9 == 0x9E) take({1, 0, 0}, us);
    else if(off >> 4 == 0x1300) switch(off >> 1 & 7) {
      case 0: v = 0x5252; break;
      case 1: v = 0x4658; break;
      case 2: v = (uint16_t)((f->gap ? 0x400 : 0) | (f->bad ? 0x200 : 0) | (f->shown ? 0x10 : 0) | 0x0C | ((busy || !q.empty()) ? 1 : 0)); break;
      case 3: v = frames; break;
      case 4: v = time20; break;
      case 5: v = rleLast; break;
      case 6: v = 0; break;
      default: v = f->n_chk; break;
    }
    else if(off >> 4 == 0x1301) switch(off >> 1 & 7) {
      case 2: v = (uint16_t)q.size(); break;
      case 3: v = f->n_dup; break;
      case 4: v = f->n_bad; break;
      case 5: v = f->n_taken; break;
      default: break;
    }
    return true;
  }
};
