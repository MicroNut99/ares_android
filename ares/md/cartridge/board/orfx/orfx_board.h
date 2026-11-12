#pragma once
//OutRun CD32X's FPGA renderer (MicroNut99/CannonballCD32X fpga/orfx): the core's C model (orfx.h, orfx_model.inc are
//copies of fpga/orfx/model/orfx.h and orfx.c - the definition the RTL is checked against) and its cart bus side.
//The model draws a frame at once at GO; the core's time is modelled as orfx_cycles() at 50 MHz (status bit 0 stays
//set until then), against the VDP's lines (nowUs).
//
//The emulator is not the console: ares answers every read at once and has no address-bus glitches. So the model
//also TRAPS what the real core would get wrong or what hides a fault (CannonballCD32X fpga/orfx/FINDINGS.md), and
//can INJECT the console's kinds of faults, to exercise the game's handling of them:
//
//  ARES_ORFX_TRAP  = log (default: one line on stderr each, "[orfx trap] CODE ...", and a summary at the end)
//                  | stop (the first trap that the real core would not survive ends the run: exit code 77)
//                  | off
//  ARES_ORFX_FAULT = a list, e.g. "dup=50,gap=400,pair=3000,slow=4,hang=300" (dup, gap, pair: 1 word in N at random):
//      dup=N   a command word arrives twice (the core drops it)
//      gap=N   a command word is lost (the core sees the gap, ignores the rest of the unit; the game sends it again)
//      pair=N  two words in a row are lost (A17 then looks right: the CHECK's count catches it; the unit's writes,
//              held until a right CHECK, are dropped; the game sends it again)
//      slow=K  the core takes K times as long to draw (the game's timeout and fallback)
//      hang=F  after F frames the core stays busy (the game must notice, post the core's state and draw itself)
//
//All the traps and faults of this build, in one table: README-CD32X.md, "Error traps and injected faults".
//Traps (the code, what it means, fatal = what "stop" stops on):
//  WORD_WHILE_BUSY   fatal  a command word while the core draws: the real core queues it (1024 words) and applies
//                           it after the frame; this model would apply it at once - the game must wait for idle
//  SEQ_GAP                  a word was lost on the bus (same A17, another address): the unit is dropped (counted)
//  SEQ_DUP                  a word read twice (same A17 and address: the real core drops it; counted)
//  UNIT_WRONG               a unit's CHECK wrong, or its GO refused: nothing of it written (counted)
//  NOT_ON            fatal  a command word before the key (after a console reset the core is off: words are lost)
//  BAD_OP            fatal* an unknown command, a SELECT of no memory, DATA with nothing selected (in step)
//  DATA_OVERRUN      fatal* DATA past the end of the selected memory (the real core wraps)
//                           (*not fatal with faults injected: a lost pair puts the decoder out of step on purpose)
//  SHOW_DRAWING      fatal  SHOW of the buffer being drawn
//  READ_DRAWING      fatal  the picture or its colours read from the buffer being drawn
//  SLOW                     a frame over 33 ms of core time (no 30 FPS)
//  STUCK             fatal  a frame over 500 ms (the game's watchdog would give up)
//  RUNAWAY           fatal  a sprite line read over 1024 words (garbage sprite data: seconds of drawing)
//  SLOTS                    more than 253 colours in a frame (wrong colours)
//  ROM1_READ         fatal  the console read banks 16-30 while the core is on (it takes the core's PSRAM away)
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "orfx.h"
extern "C" {
#include "orfx_model.inc"
}

struct OrFxTraps {
  enum : u32 { WORD_WHILE_BUSY, SEQ_GAP, SEQ_DUP, NOT_ON, BAD_OP, DATA_OVERRUN, SHOW_DRAWING, READ_DRAWING, SLOW,
               STUCK, RUNAWAY, SLOTS, ROM1_READ, UNIT_WRONG, COUNT };
  static constexpr const char* names[COUNT] = {"WORD_WHILE_BUSY", "SEQ_GAP", "SEQ_DUP", "NOT_ON", "BAD_OP",
    "DATA_OVERRUN", "SHOW_DRAWING", "READ_DRAWING", "SLOW", "STUCK", "RUNAWAY", "SLOTS", "ROM1_READ", "UNIT_WRONG"};
  static constexpr bool fatal[COUNT] = {true, false, false, true, true, true, true, true, false, true, true, false, true, false};
  u32 n[COUNT] = {};
  int mode = 1;                          //0 off, 1 log, 2 stop
  u32 fDup = 0, fGap = 0, fPair = 0, fSlow = 1, fHang = 0;
  u32 rnd = 12345;
  auto oneIn(u32 k) -> bool { if(!k) return false; rnd = rnd * 1103515245u + 12345u; return (rnd >> 8) % k == 0; }
  bool summaryAt = false;

  OrFxTraps() {
    if(auto e = getenv("ARES_ORFX_TRAP")) mode = !strcmp(e, "off") ? 0 : !strcmp(e, "stop") ? 2 : 1;
    if(auto e = getenv("ARES_ORFX_FAULT")) {
      const char* p = e;
      while(*p) {
        u32 v = 0; const char* q = strchr(p, '='); if(!q) break;
        v = (u32)atoi(q + 1);
        if(!strncmp(p, "dup", 3)) fDup = v; else if(!strncmp(p, "gap", 3)) fGap = v; else if(!strncmp(p, "pair", 4)) fPair = v;
        else if(!strncmp(p, "slow", 4)) fSlow = v ? v : 1; else if(!strncmp(p, "hang", 4)) fHang = v;
        p = strchr(q, ','); if(!p) break; p++;
      }
      fprintf(stderr, "[orfx] faults injected: dup=%u gap=%u pair=%u slow=%u hang=%u\n", fDup, fGap, fPair, fSlow, fHang);
    }
  }
  static auto self() -> OrFxTraps& { static OrFxTraps t; return t; }
  static auto summary() -> void {
    auto& t = self();
    u32 all = 0; for(u32 i = 0; i < COUNT; i++) all += t.n[i];
    fprintf(stderr, "[orfx] traps at the end: %u", all);
    for(u32 i = 0; i < COUNT; i++) if(t.n[i]) fprintf(stderr, "  %s %u", names[i], t.n[i]);
    fprintf(stderr, "\n");
  }
  auto trap(u32 code, u32 frame, u64 us, const char* fmt, ...) -> void {
    if(!summaryAt) { summaryAt = true; atexit(summary); }
    n[code]++;
    if(!mode) return;
    if(n[code] <= 20) {                  //(the first 20 of each, then only counted)
      va_list ap; va_start(ap, fmt);
      fprintf(stderr, "[orfx trap] %s frame %u t=%.3f s: ", names[code], frame, us / 1e6);
      vfprintf(stderr, fmt, ap); fprintf(stderr, "\n");
      va_end(ap);
    }
    bool faults = fDup || fGap || fPair;
    if(mode == 2 && fatal[code] && !(faults && (code == BAD_OP || code == DATA_OVERRUN))) { fprintf(stderr, "[orfx] ARES_ORFX_TRAP=stop: the run ends here\n"); summary(); exit(77); }
  }
};

struct OrFx {
  bool on = false;
  u32 keyStep = 0;
  orfx_t* f = nullptr;
  std::vector<u8> gs, gt, gr;            //the graphics, from the cart (banks 16+) at LOAD
  u32 drawing = 0;
  u64 busyUntil = 0;
  u32 lastTime20 = 0;
  u32 cmdWords = 0, lost = 0, skip = 0;
  bool hung = false;
  OrFxTraps& t = OrFxTraps::self();

  auto reset(bool) -> void { on = false; keyStep = 0; }
  auto busy(u64 us) const -> bool { return hung || us < busyUntil; }
  auto shown() const -> u32 { return f ? f->shown & 1 : 0; }

  //a console read of the cart while the core is on: true if the core answers (local = the cart address, 22 bits)
  template<typename RamRead> auto read(u32 local, u16& v, u64 us, RamRead ramRead) -> bool {
    static const u32 key[3] = {0x0f, 0x58, 0xe1};
    if(local >= ORFX_KEY0 && local < ORFX_KEY0 + 0x200) {
      if(local == ORFX_KEY0 + 2 * key[keyStep]) { if(++keyStep == 3) { on = true; keyStep = 0; start(); } }
      else keyStep = local == ORFX_KEY0 + 2 * key[0] ? 1 : 0;
      if(!on) return false;
    }
    if(!on) {
      if(local >= ORFX_CMD_BASE && local < ORFX_CMD_BASE + 0x40000 && f)
        t.trap(OrFxTraps::NOT_ON, f->frames, us, "a command word at 0x%06x with the core off", local);
      return false;
    }
    if(local < ORFX_CMD_BASE || local >= 0x3e0000) return false;
    if(local >= ORFX_SYNC && local < ORFX_SYNC + 0x200) {
      if(busy(us)) t.trap(OrFxTraps::WORD_WHILE_BUSY, f->frames, us, "SYNC %.1f ms before the core is idle", (busyUntil - us) / 1000.0);
      orfx_sync(f); v = 0; return true;
    }
    if(local < ORFX_PIC_BASE) {
      if(local < ORFX_CMD_BASE + 0x40000) command(local, us, ramRead);
      v = 0; return true;
    }
    if(local < ORFX_PIC_PAL + 0x200 && busy(us) && shown() == drawing)
      t.trap(OrFxTraps::READ_DRAWING, f->frames, us, "0x%06x of buffer %u, which the core is drawing", local, drawing);
    if(local < ORFX_PIC_PAL) {
      u32 o = local - ORFX_PIC_BASE;
      v = o + 1 < ORFX_W * ORFX_H ? (u16)(f->pic[shown()][o & ~1u] << 8 | f->pic[shown()][o | 1]) : 0;
      return true;
    }
    if(local < ORFX_PIC_PAL + 0x200) { v = f->slotpal[shown()][(local - ORFX_PIC_PAL) >> 1]; return true; }
    if(local >= ORFX_STATUS && local < ORFX_STATUS + 0x20) {
      switch((local - ORFX_STATUS) >> 1) {
      case 0: v = 0x4f52; break;
      case 1: v = 0x4658; break;
      case 2: v = (busy(us) ? 1 : 0) | 4 | shown() << 4 | (f->bad ? 0x200 : 0) | (f->gap ? 0x400 : 0); break;
      case 3: v = f->frames; break;
      case 4: v = lastTime20; break;
      case 5: v = f->slots[shown()]; break;
      case 6: v = f->overflow[shown()]; break;
      case 7: v = f->n_chk; break;
      case 8: v = busy(us) ? 0x2000 : 0; break;   //(the renderer's state: the model has none; "drawing" or idle)
      case 11: v = f->n_dup; break;
      case 12: v = f->n_bad; break;
      case 13: v = f->n_taken; break;
      default: v = 0; break;
      }
      return true;
    }
    v = 0;
    return true;
  }

  //a read of the command window: the faults, then the core's bus side (orfx_cmd: A17, the scramble, gaps)
  template<typename RamRead> auto command(u32 local, u64 us, RamRead ramRead) -> void {
    u32 tg = local >> 17 & 1;
    u16 a = local >> 1 & 0xffff;
    cmdWords++;
    if(skip) { skip--; lost++; return; }                              //(fault: these reads never happened)
    if(t.oneIn(t.fGap)) { lost++; return; }
    if(t.oneIn(t.fPair)) { lost++; skip = 1; return; }
    bool twice = t.oneIn(t.fDup);
    for(u32 rep = 0; rep < (twice ? 2u : 1u); rep++) word(tg, a, us, ramRead);
  }

  auto start() -> void {
    if(!f) f = new orfx_t;
    orfx_init(f, gs.data(), gt.data(), gr.data());
    busyUntil = 0; hung = false; skip = 0;
  }

  template<typename RamRead> auto word(u32 tg, u16 a, u64 us, RamRead ramRead) -> void {
    bool dup = tg == (u32)f->q_tog && a == f->q_last;   //(a read seen twice: the core drops it before its queue)
    if(busy(us) && !dup) t.trap(OrFxTraps::WORD_WHILE_BUSY, f->frames, us, "a word %.1f ms before the core is idle", (busyUntil - us) / 1000.0);
    //in step (the A17 right, no gap, no CHECK words pending): the word, decoded, checked before the model takes it
    if(tg != (u32)f->q_tog && !f->poison && !f->chk_pend) {
      u16 w = a ^ (u16)(f->q_key + ORFX_KEY_STEP);
      if(!f->data_left && f->sel_m >= -1) {
        u32 op = w >> 12;
        if(op > 6) t.trap(OrFxTraps::BAD_OP, f->frames, us, "command %04x", w);
        if(op == 1 && (w & 7) > 5) t.trap(OrFxTraps::BAD_OP, f->frames, us, "SELECT of memory %u", w & 7);
        if(op == 2) {
          static const u32 size[6] = {0x8000, 0x800, 0x400, 0x800, 0x1000, ORFX_R_COUNT};
          if(f->sel_m < 0 || f->sel_m > 5) t.trap(OrFxTraps::BAD_OP, f->frames, us, "DATA %u with nothing selected", w & 0xfff);
          else if(f->sel_a + (w & 0xfff) > size[f->sel_m])
            t.trap(OrFxTraps::DATA_OVERRUN, f->frames, us, "memory %u: %u words from 0x%x, %u there", f->sel_m, w & 0xfff, f->sel_a, size[f->sel_m]);
        }
        if(op == 3) drawing = w & 1;
        if(op == 4 && f->fresh && busy(us) && (w & 1) == drawing)
          t.trap(OrFxTraps::SHOW_DRAWING, f->frames, us, "SHOW %u while drawing it", w & 1);
      }
    }
    u32 fr0 = f->frames, bad0 = f->n_bad;
    int r = orfx_cmd(f, tg, a);
    if(r == 0) t.trap(OrFxTraps::SEQ_DUP, f->frames, us, "a word (A17 %u, 0x%04x) read twice", tg, a);
    if(r == 2) t.trap(OrFxTraps::SEQ_GAP, f->frames, us, "a word lost before 0x%04x: the unit is dropped", a);
    else if(f->n_bad != bad0) t.trap(OrFxTraps::UNIT_WRONG, f->frames, us, "a unit's CHECK wrong or its GO refused: nothing of it written");
    if(f->load_req) {
      f->load_req = 0;
      auto grab = [&](std::vector<u8>& v, u32 at, u32 n) {
        v.resize(n);
        for(u32 i = 0; i < n; i += 2) { u16 x = ramRead(at + i); v[i] = x >> 8; v[i + 1] = x; }
      };
      grab(gs, ORFX_GFX_SPRITES, 0x100000);
      grab(gt, ORFX_GFX_TILES, 0x40000);
      grab(gr, ORFX_GFX_ROAD, 0x10000);
      f->gfx_sprites = gs.data(); f->gfx_tiles = gt.data(); f->gfx_road = gr.data();
      busyUntil = us + 20000;            //(the RTL copies the tiles into its SRAM: about 20 ms)
    }
    if(f->frames != fr0) {               //a GO drew a frame
      u32 c = orfx_cycles(f) * t.fSlow;
      busyUntil = us + c / 50;
      lastTime20 = c / 1000;
      if(c > 50000u * 500) t.trap(OrFxTraps::STUCK, f->frames, us, "%.0f ms to draw", c / 50000.0);
      else if(c > 50000u * 33) t.trap(OrFxTraps::SLOW, f->frames, us, "%.1f ms to draw", c / 50000.0);
      if(f->n.spr_line_max > 1024) t.trap(OrFxTraps::RUNAWAY, f->frames, us, "a sprite line read %u words", f->n.spr_line_max);
      if(f->overflow[drawing]) t.trap(OrFxTraps::SLOTS, f->frames, us, "%d pixels without a slot", f->overflow[drawing]);
      if(t.fHang && f->frames >= t.fHang && !hung) { hung = true; fprintf(stderr, "[orfx] fault: the core hangs at frame %u\n", f->frames); }
    }
  }
};
