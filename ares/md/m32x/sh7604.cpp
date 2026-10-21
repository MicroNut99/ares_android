//CD32X profiler (ares-headless --profile): every SH2 clock charged to the instruction running.
//Interpreter only (the recompiler charges whole blocks at once).
namespace sh2prof {
  bool on = false;
  u32 pc[2];                                            //instruction running, per SH2
  u64 sdram[2][0x20000];                                //0x06000000-0x0603FFFF, per halfword
  u64* cart[2];                                         //0x02000000-0x023FFFFF, per halfword
  u64 other[2], total[2];
  u64 cartFill[2], cartSingle[2];
  u32 probePC = 0, probeLast[2];                        //ARES_PROBE_PC: how often a load repeats its address
  u64 probeHits[2], probeSame[2];                       //cart word reads: in a cache line fill / single
  inline auto add(u32 which, u32 clocks) -> void {
    u32 a = pc[which] & 0x1fff'fffe;
    total[which] += clocks;
    if(a >= 0x0600'0000 && a < 0x0604'0000) sdram[which][(a - 0x0600'0000) >> 1] += clocks;
    else if(a >= 0x0200'0000 && a < 0x0240'0000) cart[which][(a - 0x0200'0000) >> 1] += clocks;
    else other[which] += clocks;
  }
}

extern "C" void ares_sh2prof_enable(bool enable) {
  for(u32 w : range(2)) if(!sh2prof::cart[w]) sh2prof::cart[w] = new u64[0x200000]();
  if(auto e = getenv("ARES_PROBE_PC")) sh2prof::probePC = strtoul(e, nullptr, 16);
  sh2prof::on = enable;
}

extern "C" void ares_sh2prof_dump(const char* file) {
  FILE* fp = fopen(file, "w");
  if(!fp) return;
  fprintf(fp, "# probe %08x: master %llu same %llu | slave %llu same %llu\n", sh2prof::probePC,
    (unsigned long long)sh2prof::probeHits[0], (unsigned long long)sh2prof::probeSame[0],
    (unsigned long long)sh2prof::probeHits[1], (unsigned long long)sh2prof::probeSame[1]);
  fprintf(fp, "# cart reads: master fill %llu single %llu | slave fill %llu single %llu\n",
    (unsigned long long)sh2prof::cartFill[0], (unsigned long long)sh2prof::cartSingle[0],
    (unsigned long long)sh2prof::cartFill[1], (unsigned long long)sh2prof::cartSingle[1]);
  fprintf(fp, "# cache master enable %d disableCode %d disableData %d twoWay %d | slave enable %d disableCode %d disableData %d twoWay %d\n",
    (int)m32x.shm.cache.enable, (int)m32x.shm.cache.disableCode, (int)m32x.shm.cache.disableData, (int)m32x.shm.cache.twoWay,
    (int)m32x.shs.cache.enable, (int)m32x.shs.cache.disableCode, (int)m32x.shs.cache.disableData, (int)m32x.shs.cache.twoWay);
  for(u32 w : range(2)) {
    fprintf(fp, "# cpu %s total %llu other %llu\n", w ? "slave" : "master",
      (unsigned long long)sh2prof::total[w], (unsigned long long)sh2prof::other[w]);
    for(u32 i : range(0x20000)) if(sh2prof::sdram[w][i])
      fprintf(fp, "%u %08x %llu\n", w, 0x0600'0000 + i * 2, (unsigned long long)sh2prof::sdram[w][i]);
    if(sh2prof::cart[w]) for(u32 i : range(0x200000)) if(sh2prof::cart[w][i])
      fprintf(fp, "%u %08x %llu\n", w, 0x0200'0000 + i * 2, (unsigned long long)sh2prof::cart[w][i]);
  }
  fclose(fp);
}

auto M32X::SH7604::load(Node::Object parent, string name, string bootFile) -> void {
  node = parent->append<Node::Object>(name);
  if(auto fp = system.pak->read(bootFile)) {
    bootROM.allocate(fp->size() >> 1);
    for(auto address : range(bootROM.size())) bootROM.program(address, fp->readm(2L));
  }
  debugger.load(node);
}

auto M32X::SH7604::unload() -> void {
  debugger = {};
  bootROM.reset();
  node.reset();
}

auto M32X::SH7604::main() -> void {
  if(!m32x.io.adapterReset) return step(1000);

  if(!regs.ID) {
    #define raise(type, level, vector, ...) \
      if(SH2::inDelaySlot()) SH2::cyclesUntilRecompilerExit = 0; \
      else { \
        debugger.interrupt(type); \
        __VA_ARGS__; \
        return regs.ET = 1, interrupt(level, vector); \
      }
    if(irq.vres.active && irq.vres.enable) {
      raise("VRES", 14, 71, irq.vres.active = 0);
    } else if(irq.vint.active && irq.vint.enable && regs.SR.I < 12) {
      raise("VINT", 12, 70);
    } else if(irq.hint.active && irq.hint.enable && regs.SR.I < 10) {
      raise("HINT", 10, 69);
    } else if(irq.cmd.active && irq.cmd.enable && regs.SR.I < 8) {
      raise("CMD", 8, 68);
    } else if(irq.pwm.active && irq.pwm.enable && regs.SR.I < 6) {
      raise("PWM", 6, 67);
    }
    #undef raise
  }

  if(sh2prof::on) sh2prof::pc[this == &m32x.shs] = regs.PC - 4;
  if(sh2prof::on && sh2prof::probePC && regs.PC - 4 == sh2prof::probePC) {
    u32 w = this == &m32x.shs, addr = regs.R[0] + regs.R[13];
    sh2prof::probeHits[w]++;
    if(addr == sh2prof::probeLast[w]) sh2prof::probeSame[w]++;
    sh2prof::probeLast[w] = addr;
  }
  SH2::instruction();
  SH2::intc.run();
  SH2::dmac.run();
  if(m32x.shm.active()) m32x.shm.dmac.dreq[1] = 0;
  if(m32x.shs.active()) m32x.shs.dmac.dreq[1] = 0;
}

auto M32X::SH7604::instructionPrologue(u16 instruction) -> void {
  debugger.instruction(instruction);
}

auto M32X::SH7604::internalStep(u32 clocks) -> void {
  if(SH2::Accuracy::Recompiler && m32x.shm.recompiler.enabled) {
    regs.CCR += clocks;
    return;
  }

  step(clocks);
}

auto M32X::SH7604::step(u32 clocks) -> void {
  if(sh2prof::on && m32x.io.adapterReset) sh2prof::add(this == &m32x.shs, clocks);
  SH2::frt.run(clocks);
  SH2::wdt.run(clocks);
  Thread::step(clocks);

  cyclesUntilSh2Sync -= clocks;
  cyclesUntilM68kSync -= clocks;

  m32x.vdp.framebufferWait -= min(clocks, m32x.vdp.framebufferWait);

  if(cyclesUntilSh2Sync <= 0) {
    cyclesUntilSh2Sync = minCyclesBetweenSh2Syncs;
    if (m32x.shm.active()) Thread::synchronize(m32x.shs);
    if (m32x.shs.active()) Thread::synchronize(m32x.shm);
  }

  if(cyclesUntilM68kSync <= 0) {
    cyclesUntilM68kSync = minCyclesBetweenM68kSyncs;
    Thread::synchronize(cpu);
  }
}

auto M32X::SH7604::power(bool reset) -> void {
  Thread::create((system.frequency() / 7.0) * 3.0, std::bind_front(&M32X::SH7604::main, this));

  //When tweaking these values, make sure to test the following problematic games:
  // Brutal  - Check for hang in attract mode
  // Chaotix - Check for hang on intro screen or level loading transitions

  SH2::recompilerStepCycles =  200; //Recompiler will force an exit after at least N cycles have passed
  minCyclesBetweenSh2Syncs  =   10; //Do not sync SH2s more than once every N cycles
  minCyclesBetweenM68kSyncs =   50; //Do not sync M68K more than once every N cycles
  SH2::power(reset);
  irq = {};
  irq.vres.enable = 1;
}

auto M32X::SH7604::restart() -> void {
  SH2::power(true);
  irq = {};
  irq.vres.enable = 1;
  Thread::restart(std::bind_front(&M32X::SH7604::main, this));
}

auto M32X::SH7604::syncAll(bool force) -> void {
  SH2::cyclesUntilRecompilerExit = 0;

  if(SH2::Accuracy::Recompiler && m32x.shm.recompiler.enabled && force) {
    cyclesUntilSh2Sync = 0;
    cyclesUntilM68kSync = 0;
    step(regs.CCR);
    regs.CCR = 0;
  }
}

auto M32X::SH7604::syncOtherSh2(bool force) -> void {
  SH2::cyclesUntilRecompilerExit = 0;

  if(SH2::Accuracy::Recompiler && m32x.shm.recompiler.enabled && force) {
    cyclesUntilSh2Sync = 0;
    step(regs.CCR);
    regs.CCR = 0;
  }
}

auto M32X::SH7604::syncM68k(bool force) -> void {
  SH2::cyclesUntilRecompilerExit = 0;

  if(SH2::Accuracy::Recompiler && m32x.shm.recompiler.enabled && force) {
    cyclesUntilM68kSync = 0;
    step(regs.CCR);
    regs.CCR = 0;
  }
}

auto M32X::SH7604::busReadByte(u32 address) -> u32 {
  if(address & 1) {
    return m32x.readInternal(0, 1, address & ~1).byte(0);
  } else {
    return m32x.readInternal(1, 0, address & ~1).byte(1);
  }
}

auto M32X::SH7604::busReadWord(u32 address) -> u32 {
  return m32x.readInternal(1, 1, address & ~1);
}

auto M32X::SH7604::busReadLong(u32 address) -> u32 {
  u32    data = m32x.readInternal(1, 1, address & ~3 | 0) << 16;
  return data | m32x.readInternal(1, 1, address & ~3 | 2) <<  0;
}

auto M32X::SH7604::busWriteByte(u32 address, u32 data) -> void {
  debugger.tracer.instruction->invalidate(address & ~1);
  if(address & 1) {
    m32x.writeInternal(0, 1, address & ~1, data << 8 | (u8)data << 0);
  } else {
    m32x.writeInternal(1, 0, address & ~1, data << 8 | (u8)data << 0);
  }
}

auto M32X::SH7604::busWriteWord(u32 address, u32 data) -> void {
  debugger.tracer.instruction->invalidate(address & ~1);
  m32x.writeInternal(1, 1, address & ~1, data);
}

auto M32X::SH7604::busWriteLong(u32 address, u32 data) -> void {
  debugger.tracer.instruction->invalidate(address & ~3 | 0);
  debugger.tracer.instruction->invalidate(address & ~3 | 2);
  m32x.writeInternal(1, 1, address & ~3 | 0, data >> 16);
  m32x.writeInternal(1, 1, address & ~3 | 2, data >>  0);
}
