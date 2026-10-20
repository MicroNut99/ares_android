#include <md/md.hpp>

namespace ares::MegaDrive {

M32X m32x;
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

auto M32X::Timing32X::load() -> void {
  //defaults first, then ARES_32X_TIMING (comma list of name=value, or "off")
  *this = {};
  auto& t = SH2::timing;
  t = {};
  t.enabled = false;            //off by default until it is calibrated against the console
  cpu = false;
  if(auto env = getenv("ARES_32X_TIMING"); env && *env) {
    string list = env;
    if(list != "off") { cpu = true; t.enabled = true; }   //"on" or any knob list turns it on
    for(auto& item : nall::split(list, ",")) {
      auto kv = nall::split(item, "=");
      if(kv.size() != 2) continue;
      u32 v = kv[1].natural();
      if(kv[0] == "cpu") { cpu = v; t.enabled = v; }
      if(kv[0] == "cart") cart = v;
      if(kv[0] == "sdr") sdramRead = v;
      if(kv[0] == "sdw") sdramWrite = v;
      if(kv[0] == "fbr") fbRead = v;
      if(kv[0] == "fbw") fbWrite = v;
      if(kv[0] == "br") t.branch = v;
      if(kv[0] == "bt") t.condTaken = v;
      if(kv[0] == "bts") t.condTakenS = v;
      if(kv[0] == "mul") t.mul = v;
      if(kv[0] == "lu") t.loadUse = v;
      if(kv[0] == "mac") t.macRead = v;
    }
  }
  t.build();
}

auto M32X::power(bool reset) -> void {
  timing32x.load();
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
