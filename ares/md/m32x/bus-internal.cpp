//An SH2 cart read while the 68000 holds RV = 1 on a board that answers it (the Mega EverDrive PRO:
//cartridge/board/everdrive-pro.cpp, console 26 Oct 2026): garbage instead of a stall.  ARES_RV_GARBAGE=0
//turns it off (held, as a real cartridge), =1 gives every board the EverDrive's rule; ARES_RV_LOG=1 (or
//ARES_RV_GARBAGE=1) prints who read: the SH2, its PC and PR, the address - the first time for each PC,
//then every 128th.
static auto rvSetting() -> int {  //-1 the board decides, 0 never, 1 always
  static int setting = -2;
  if(setting == -2) {
    auto value = getenv("ARES_RV_GARBAGE");
    setting = !value ? -1 : value[0] == '0' ? 0 : 1;
  }
  return setting;
}
static auto rvLogging() -> bool {
  static int on = -1;
  if(on < 0) on = getenv("ARES_RV_LOG") || rvSetting() == 1 ? 1 : 0;
  return on;
}
static auto rvLog(char who, u32 pc, u32 pr, u32 address) -> void {
  static std::set<u64> seen;
  static u32 count = 0;
  count++;
  u64 key = (u64)who << 40 | (u64)(pc & 0xfffffff) << 8;
  if(seen.insert(key).second || (count & 127) == 0)
    fprintf(stderr, "RV-CART %c pc=%08x pr=%08x addr=%08x (n=%u)\n", who, pc, pr, address, count);
}

auto M32X::readInternal(n1 upper, n1 lower, n32 address, n16 data) -> n16 {
  if(address >= 0x0000'0000 && address <= 0x0000'3fff) {
    if(shm.active()) shm.internalStep(1); if(shs.active()) shs.internalStep(1);
    if(shm.active()) return shm.bootROM[address >> 1];
    if(shs.active()) return shs.bootROM[address >> 1];
  }

  if(address >= 0x0000'4000 && address <= 0x0000'43ff) {
    return readInternalIO(upper, lower, address, data);
  }

  if(address >= 0x0200'0000 && address <= 0x03ff'ffff) {
    if(dreq.vram) {
      int setting = rvSetting();
      if(setting == 1 || (setting == -1 && cartridge.child && cartridge.child->answersDuringRV())) {
        auto& sh2 = shm.active() ? (SH7604&)shm : (SH7604&)shs;
        if(rvLogging()) rvLog(shm.active() ? 'M' : 'S', sh2.regs.PC, sh2.regs.PR, (u32)address);
        u64 h = (sh2.clock() ^ (u64)address << 21) * 0x9e3779b97f4a7c15ull;  //the same in every run
        if(shm.active()) shm.internalStep(2); if(shs.active()) shs.internalStep(2);
        return (n16)(h >> 48);
      }
    }
    while(dreq.vram) {
      //SH2 ROM accesses stall while RV is set
      if(shm.active()) { shm.internalStep(1); shm.syncM68k(true); }
      if(shs.active()) { shs.internalStep(1); shs.syncM68k(true); }
    }

    //TODO: SH2 ROM accesses need to stall while the m68k is on the bus
    stats.cartRead++;
    busCharge(timing.cartRead);
    return cartridge.child->read(upper, lower, address, data);
  }

  if(address >= 0x0400'0000 && address <= 0x05ff'ffff) {
    if(!vdp.framebufferAccess) return data;
    if(vdp.framebufferEngaged()) { debug(unusual, "[32X FB] SH2 read while FEN==1"); return data; } // wait instead?
    stats.fbRead++;
    busCharge(timing.fbRead);
    return vdp.bbram[address >> 1 & 0xffff];
  }

  if(address >= 0x0600'0000 && address <= 0x0603'ffff) {
    stats.sdramRead++;
    if(!(shm.active() ? shm.inFill : shs.inFill)) busCharge(timing.sdramRead);
    return sdram[address >> 1 & 0x1ffff];
  }

  return data;
}

auto M32X::writeInternal(n1 upper, n1 lower, n32 address, n16 data) -> void {
  if(address >= 0x0000'4000 && address <= 0x0000'43ff) {
    return writeInternalIO(upper, lower, address, data);
  }

  if(address >= 0x0400'0000 && address <= 0x05ff'ffff) {
    address &= 0x0403'ffff;

    if(address >= 0x0400'0000 && address <= 0x0401'ffff) {
      if(!vdp.framebufferAccess) return;
      if(vdp.framebufferEngaged()) { debug(unusual, "[32X FB] SH2 write while FEN==1"); return; } // wait instead?
      if(!data && (!upper || !lower)) return;  //8-bit 0x00 writes do not go through
      stats.fbWrite++;
      busCharge(fbWriteCost());
      if(upper) vdp.bbram[address >> 1 & 0xffff].byte(1) = data.byte(1);
      if(lower) vdp.bbram[address >> 1 & 0xffff].byte(0) = data.byte(0);
      return;
    }

    if(address >= 0x0402'0000 && address <= 0x0403'ffff) {
      if(!vdp.framebufferAccess) return;
      if(vdp.framebufferEngaged()) { debug(unusual, "[32X FB] SH2 overwrite while FEN==1"); return; } // wait instead?
      stats.fbWrite++;
      busCharge(fbWriteCost());
      if(upper && data.byte(1)) vdp.bbram[address >> 1 & 0xffff].byte(1) = data.byte(1);
      if(lower && data.byte(0)) vdp.bbram[address >> 1 & 0xffff].byte(0) = data.byte(0);
      return;
    }
  }

  if(address >= 0x0600'0000 && address <= 0x0603'ffff) {
    stats.sdramWrite++;
    busCharge(timing.sdramWrite);
    if(upper) sdram[address >> 1 & 0x1ffff].byte(1) = data.byte(1);
    if(lower) sdram[address >> 1 & 0x1ffff].byte(0) = data.byte(0);
    return;
  }
}

auto M32X::loadTiming() -> void {
  timing = {};
  SH2::fetchModel = 0;
  SH2::insnExtra16 = 0;
  auto env = getenv("ARES_32X_TIMING");
  if(!env) return;
  //"console": the costs that bring OpenJazz / Hurrican CD32X closest to the console readings
  //(docs in the confidential repo, ARES_VS_CONSOLE_TIMING.md)
  if(!strcmp(env, "console")) env = "fetch=1,cart=10,fbr=6,fbw=2,fifo=4,drain=4,sdr=7,sdw=3,io=4,insn=5,cont=1,arb=2";
  for(const char* p = env; *p;) {
    char key[16] = {};
    u32 n = 0;
    while(*p && *p != '=' && *p != ',' && n < 15) key[n++] = *p++;
    u32 v = 0;
    if(*p == '=') { p++; while(*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0'); }
    while(*p && *p != ',') p++;
    if(*p == ',') p++;
    string k = key;
    if(k == "cart") timing.cartRead = v;
    if(k == "fbr") timing.fbRead = v;
    if(k == "fbw") timing.fbWrite = v;
    if(k == "fifo") timing.fifoDepth = v;
    if(k == "drain") timing.fifoDrain = v;
    if(k == "sdr") timing.sdramRead = v;
    if(k == "sdw") timing.sdramWrite = v;
    if(k == "insn") timing.insn = v, SH2::insnExtra16 = v;
    if(k == "fetch") SH2::fetchModel = v;
    if(k == "io") timing.io = v;
    if(k == "fill") timing.fill = v;
    if(k == "cfill") timing.cfill = v;
    if(k == "cont") timing.contention = v;
    if(k == "arb") timing.arb = v;
  }
  print("[32X timing] cart ", timing.cartRead, " fbr ", timing.fbRead, " fbw ", timing.fbWrite,
        " fifo ", timing.fifoDepth, "x", timing.fifoDrain, " sdr ", timing.sdramRead, " sdw ", timing.sdramWrite,
        " insn ", timing.insn, "/16 fetch ", SH2::fetchModel,
        " io ", timing.io, " fill ", timing.fill, " cont ", timing.contention, " arb ", timing.arb, "\n");
}

auto M32X::sh2Now() -> u64 {
  auto& sh = shm.active() ? shm : shs;
  u64 t = sh.total;
  if(SH2::Accuracy::Recompiler && shm.recompiler.enabled) t += sh.regs.CCR;
  return t;
}

//a frame buffer write: its own cost, plus a wait while the VDP's write FIFO is full
auto M32X::fbWriteCost() -> u32 {
  if(!timing.fifoDepth || !timing.fifoDrain) return timing.fbWrite;
  u64 now = sh2Now();
  if(timing.fifoEnd + 0x10000000ull < now || timing.fifoEnd > now + 0x10000000ull) timing.fifoEnd = now;  //clock rebased
  u64 start = max(timing.fifoEnd, now);
  timing.fifoEnd = start + timing.fifoDrain;
  u64 room = (u64)timing.fifoDepth * timing.fifoDrain;
  u32 stall = timing.fifoEnd > now + room ? u32(timing.fifoEnd - now - room) : 0;
  return timing.fbWrite + stall;
}

//this SH2 uses the bus for `clocks`
auto M32X::busAccount(M32X::SH7604& self, u32 clocks) -> void {
  u64 now = sh2Now();
  if(now < self.uStart || now - self.uStart > 0x1000'0000) { self.uStart = now; self.uBusy = 0; self.uCount = 0; }
  self.uBusy += clocks;
  self.uCount++;
  if(now - self.uStart >= 2048) {
    u32 u = min<u64>(256, self.uBusy * 256 / (now - self.uStart));
    self.util256 = (self.util256 + u) / 2;
    self.tenure16 = self.uBusy * 16 / self.uCount;
    self.uStart = now; self.uBusy = 0; self.uCount = 0;
  }
}

//the expected wait for the bus while the other SH2 uses it: if it holds the bus a fraction u of
//the time in tenures of mean length t, an access waits on average u * (t / 2 + hand-over)
auto M32X::busStall(M32X::SH7604& self, M32X::SH7604& other) -> u32 {
  if(!timing.contention) return 0;
  u32 u = other.util256;
  return (u * (other.tenure16 / 2 + timing.arb * 16) + 2048) >> 12;
}

auto M32X::busCharge(u32 clocks) -> void {
  auto& self = shm.active() ? shm : shs;
  auto& other = shm.active() ? shs : shm;
  if(!shm.active() && !shs.active()) return;
  u32 c = clocks;
  if(timing.contention && !self.inFill && clocks) {
    busAccount(self, clocks);
    c += busStall(self, other);
  }
  if(c) self.internalStep(c);
}

auto M32X::SH7604::lineFillCost(u32 address) -> u32 {
  //SDRAM: one burst; other areas: the words were charged one by one, plus cfill (upstream 12)
  u32 c = (address >> 24 & 7) == 6 ? m32x.timing.fill : m32x.timing.cfill;
  u32 busy = (address >> 24 & 7) == 2 ? 8 * m32x.timing.cartRead : c;
  if(m32x.timing.contention) {
    auto& other = this == &m32x.shm ? m32x.shs : m32x.shm;
    m32x.busAccount(*this, busy);
    c += m32x.busStall(*this, other);
  }
  return c;
}
