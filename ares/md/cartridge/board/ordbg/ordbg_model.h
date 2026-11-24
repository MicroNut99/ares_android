//ordbg_model.h - OutRun CD32X's debug core (the 'ordbg' configuration of the Mega EverDrive PRO), as ares models it.
//
//The core (MicroNut99/CannonballCD32X, fpga/ordbg/rtl/ordbg_core.sv - the definition) sits beside krikzz's map_ssf
//and only records: it never drives a memory. Once its key was read (three reads of cart 0x3E9000 + 2k, k = 0x123,
//0x456, 0x789) it answers the window cart 0x3E0000-0x3EFFFF: registers at + 0x0000, the post ring at + 0x0400, the
//trail at + 0x0800, commands (reads) at + 0x8000. This model keeps the same registers, rings and commands so the
//game's side (port/32x/ordbg.h, the X panel) can be checked in ares. Like every model here it answers at once: it
//says nothing about the bus timing (the Verilator bench fpga/ordbg/sim does that), and its "time" is ares's.
//MIT (as the core).
#pragma once
#include <cstdint>

struct OrDbg {
  bool on = false; int kstep = 0;
  uint32_t nRd = 0, nWr = 0, lastWa = 0, nFrames = 0, msAtKey = 0;
  uint16_t lastWd = 0, nArea = 0, nRegs = 0, ctrl0 = 0, fLast = 0, fMin = 0xffff, fMax = 0, stalls = 0, stallMax = 0;
  uint16_t nPosts = 0, lastCode = 0, nWin[8] = {}, val[16] = {}, resets = 0;
  uint8_t postHi = 0, valHi[16] = {}, postI = 0, trailI = 0;
  bool framed = false, frozen = false, autoFrozen = false;
  uint64_t lastFrameUs = 0, lastAccUs = 0, stallStartUs = 0;
  uint64_t post[32] = {}, trail[128] = {};   //each 4 words, high first

  void reset(bool console) { bool keepRes = true; uint16_t r = resets; *this = OrDbg{}; if(keepRes) resets = r + (console ? 1 : 0x100); }

  //every cart access outside the window (phys = {bank, A18-A0}); us = the time in microseconds
  void access(uint32_t phys, bool wr, uint16_t data, int win, uint64_t us) {
    if(wr) { nWr++; lastWa = phys; lastWd = data; } else nRd++;
    nWin[win & 7]++;
    lastAccUs = us;
    stallCheck(us);
    if(!frozen && !autoFrozen) {
      trail[trailI & 127] = (uint64_t)((wr ? 0x80 : 0) | (win & 7)) << 56 | (uint64_t)(phys & 0xffffff) << 32 | (uint32_t)(us * 50);
      trailI = (trailI + 1) & 127;
    }
  }
  void stallCheck(uint64_t us) {
    if(framed && !autoFrozen && us - lastFrameUs >= 2'000'000) { autoFrozen = true; stallStartUs = us; }
  }
  uint16_t status() const {
    return 0xa000 | kstep << 8 | (framed ? 8 : 0) | (autoFrozen ? 4 : 0) | (frozen ? 2 : 0) | (on ? 1 : 0);
  }
  uint16_t reg(int w, const uint8_t* banks, bool wrOn, uint64_t us) const {
    uint32_t ms = (uint32_t)(us / 1000);
    switch(w) {
    case 0: return 0x4f52; case 1: return 0x4442; case 2: return 1; case 3: return status();
    case 4: return ms >> 16; case 5: return ms; case 6: return nRd >> 16; case 7: return nRd;
    case 16: return ctrl0; case 17: return nRegs; case 18: return resets; case 19: return wrOn;
    case 20: return nWr >> 16; case 21: return nWr; case 22: return lastWa >> 16 & 0xff; case 23: return lastWa;
    case 24: return lastWd; case 25: return nArea; case 26: return nFrames >> 16; case 27: return nFrames;
    case 28: return fLast; case 29: return fMin; case 30: return fMax;
    case 31: { uint64_t d = (us - lastFrameUs) / 1000; return framed ? (d > 0xffff ? 0xffff : d) : 0; }
    case 32: { uint64_t d = (us - lastAccUs) / 1000; return d > 0xffff ? 0xffff : d; }
    case 33: return stalls; case 34: return stallMax; case 35: return nPosts; case 36: return lastCode;
    case 37: return postI; case 38: return trailI; case 39: return 0; case 40: return 0;
    case 41: return on ? (ms - msAtKey > 0xffff ? 0xffff : ms - msAtKey) : 0;
    }
    if(w >= 8 && w < 16) return banks[w - 8];
    if(w >= 48 && w < 56) return nWin[w - 48];
    if(w >= 64 && w < 80) return val[w - 64];
    return 0;
  }
  //a console read of the window (offset 0x0000-0xffff); false: not answered (the key not read yet)
  bool read(uint32_t o, uint16_t& v, const uint8_t* banks, bool wrOn, uint64_t us) {
    stallCheck(us);
    if(o & 0x8000) command(o, us);
    else if(on) nArea++;
    if(!on) return false;
    if(o & 0x8000) v = status();
    else if((o & 0xc00) == 0x800) v = trail[o >> 3 & 127] >> (16 * (3 - (o >> 1 & 3)));
    else if((o & 0xf00) == 0x400) v = post[o >> 3 & 31] >> (16 * (3 - (o >> 1 & 3)));
    else if((o & 0xf00) == 0) v = reg(o >> 1 & 127, banks, wrOn, us);
    else v = 0;
    return true;
  }
  void command(uint32_t o, uint64_t us) {
    if((o & 0xf000) == 0x9000) {
      uint32_t k = o >> 1 & 0x7ff;
      if(kstep == 0 && k == 0x123) kstep = 1;
      else if(kstep == 1 && k == 0x456) kstep = 2;
      else if(kstep == 2 && k == 0x789) { kstep = 0; on = true; msAtKey = (uint32_t)(us / 1000); }
      else kstep = k == 0x123;
      return;
    }
    if(!on) return;
    nArea++;
    if((o & 0xfe00) == 0xa000) postHi = o >> 1;
    else if((o & 0xfe00) == 0xa200) {
      lastCode = postHi << 8 | (uint8_t)(o >> 1); nPosts++;
      post[postI] = (uint64_t)lastCode << 48 | (uint64_t)(uint16_t)nFrames << 32 | (uint32_t)(us / 1000);
      postI = (postI + 1) & 31;
    }
    else if(o == 0xa400) {
      nFrames++;
      if(framed) {
        uint64_t u = (us - lastFrameUs) / 20; uint16_t f = u > 0xffff ? 0xffff : u;
        fLast = f; if(f < fMin) fMin = f; if(f > fMax) fMax = f;
      }
      if(autoFrozen) { autoFrozen = false; stalls++; uint64_t s = (us - stallStartUs) / 1000; if(s > stallMax) stallMax = s > 0xffff ? 0xffff : s; }
      framed = true; lastFrameUs = us;
    }
    else if(o == 0xa402) { nRd = nWr = 0; nArea = nRegs = 0; nFrames = 0; fLast = fMax = 0; fMin = 0xffff; stalls = stallMax = 0;
                           nPosts = 0; lastCode = 0; framed = autoFrozen = false; for(auto& n : nWin) n = 0; }
    else if(o == 0xa404) frozen = true;
    else if(o == 0xa406) { frozen = false; trailI = 0; }
    else if(o >= 0xb000 && o < 0xd000) valHi[(o - 0xb000) >> 9] = o >> 1;
    else if(o >= 0xd000 && o < 0xf000) { int s = (o - 0xd000) >> 9; val[s] = valHi[s] << 8 | (uint8_t)(o >> 1); }
  }
  void regWrite(int n, uint16_t data) { nRegs++; if(n == 0) ctrl0 = data; }
};
