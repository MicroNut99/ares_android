#include <md/md.hpp>
#include <set>
#include <cstdlib>

namespace ares::MegaDrive {

M32X m32x;
//console traps (DraculaX CD32X docs/175): the 32X drops an access to its frame buffer or its VDP registers made by the
//CPU that does not hold FM, and says nothing. A program that loses a write that way runs on here and stops on the
//console. ARES_32X_TRAP=1 prints each kind's first 12 and then every 500th; =2 prints all.
static auto m32xTrap(u32 kind, u32 address, u32 data) -> void {
  static int on = -1; static u32 count[8];
  if(on < 0) { auto e = getenv("ARES_32X_TRAP"); on = e ? atoi(e) : 0; }
  if(!on) return;
  static const char* name[8] = {
    "68000 reads the frame buffer, FM=1 (the SH2s hold it): open bus",
    "68000 writes the frame buffer, FM=1: dropped",
    "68000 writes a 32X VDP register, FM=1: dropped",
    "SH2 reads the frame buffer, FM=0 (the 68000 holds it): open bus",
    "SH2 writes the frame buffer, FM=0: dropped",
    "SH2 writes a 32X VDP register, FM=0: dropped",
    "68000 changes FM",
    "SH2 changes FM",
  };
  u32 n = ++count[kind & 7];
  if(on >= 2 || n <= 12 || n % 500 == 0) fprintf(stderr, "[32X TRAP] #%u %s  address %06x data %04x\n", n, name[kind & 7], address, data & 0xffff);
}
#include "sh7604.cpp"
#include "bus-internal.cpp"
#include "bus-external.cpp"
#include "io-internal.cpp"
#include "io-external.cpp"
#include "vdp.cpp"
#include "pwm.cpp"
#include "debugger.cpp"
#include "serialization.cpp"

auto M32X::load(Node::Object parent) -> void {
  node = parent->append<Node::Object>("Mega 32X");
  loadTiming();
  sdram.allocate(256_KiB >> 1);
  shm.load(node, "SHM", "sh2.boot.mrom");
  shs.load(node, "SHS", "sh2.boot.srom");
  vdp.load(node);
  pwm.load(node);
  debugger.load(node);

  if(auto fp = system.pak->read("vector.rom")) {
    vectors.allocate(fp->size() >> 1);
    for(auto address : range(vectors.size())) vectors.program(address, fp->readm(2L));
  }
}

auto M32X::unload() -> void {
  debugger = {};
  shm.unload();
  shs.unload();
  vdp.unload();
  pwm.unload(node);
  vectors.reset();
  sdram.reset();
  node.reset();
}

auto M32X::save() -> void {
}

//harness: ARES_COMM_LOG = mask of COMM words (bit n = COMMn*2): each write printed with the scanline
auto M32X::commLog(u32 index, char who) -> void {
  static int mask = -1;
  if(mask < 0) { auto e = getenv("ARES_COMM_LOG"); mask = e ? (int)strtol(e, nullptr, 0) : 0; }
  if(!(mask & 1 << index)) return;
  printf("  c%u %c line %3u %04X\n", index * 2, who, (u32)::ares::MegaDrive::vdp.vcounter(), (u32)communication[index]);
}

auto M32X::power(bool reset) -> void {
  if constexpr(SH2::Accuracy::Recompiler) {
    if(!reset) ares::Memory::FixedAllocator::get().release();
  }
  sdram.fill(0);
  shm.power(reset);
  shs.power(reset);
  vdp.power(reset);
  pwm.power(reset);
  n32 vec4 = io.vectorLevel4;
  io = {};
  if(reset) io.vectorLevel4 = vec4;
  dreq = {};
  for(auto& word : communication) word = 0;

  //connect interfaces
  shm.sci.link = shs;
  shs.sci.link = shm;
  shm.debugger.self = shm;
  shs.debugger.self = shs;
  vdp.self = *this;
}

auto M32X::vblank(bool line) -> void {
  vdp.vblank = line;
  shm.irq.vint.active = line;
  shs.irq.vint.active = line;
  if(line) vdp.selectFramebuffer(vdp.framebufferSelect);
}

auto M32X::hblank(bool line) -> void {
  if(vdp.hblank > line) {
    // TODO: VDP regs should be latched 192 MClks (~82 cycles) before end of hblank (according to official docs)
    vdp.latch.mode = vdp.mode;
    vdp.latch.lines = vdp.lines;
    vdp.latch.priority = vdp.priority;
    vdp.latch.dotshift = vdp.dotshift;
  }
  vdp.hblank = line;
  shm.irq.hint.active = 0;
  shs.irq.hint.active = 0;
  if(line && (!vdp.vblank || io.hintVblank)) {
    if(io.hcounter++ >= io.hperiod) {
      io.hcounter = 0;
      shm.irq.hint.active = 1;
      shs.irq.hint.active = 1;
    }
  }
}

}
