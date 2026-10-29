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
    {
      bool second = (shm.active() ? shm.longHalf : shs.longHalf) == 2;
      busCharge(second ? timing.cartRead2 : timing.cartRead, timing.holdCart);
    }
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
    if(!(shm.active() ? shm.inFill : shs.inFill)) {
      bool second = (shm.active() ? shm.longHalf : shs.longHalf) == 2;
      busCharge(second ? timing.sdramRead2 : timing.sdramRead, timing.holdSdram);
    }
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
      busCharge(fbWriteCost(), timing.holdWrite);
      if(upper) vdp.bbram[address >> 1 & 0xffff].byte(1) = data.byte(1);
      if(lower) vdp.bbram[address >> 1 & 0xffff].byte(0) = data.byte(0);
      return;
    }

    if(address >= 0x0402'0000 && address <= 0x0403'ffff) {
      if(!vdp.framebufferAccess) return;
      if(vdp.framebufferEngaged()) { debug(unusual, "[32X FB] SH2 overwrite while FEN==1"); return; } // wait instead?
      stats.fbWrite++;
      busCharge(fbWriteCost(), timing.holdWrite);
      if(upper && data.byte(1)) vdp.bbram[address >> 1 & 0xffff].byte(1) = data.byte(1);
      if(lower && data.byte(0)) vdp.bbram[address >> 1 & 0xffff].byte(0) = data.byte(0);
      return;
    }
  }

  if(address >= 0x0600'0000 && address <= 0x0603'ffff) {
    stats.sdramWrite++;
    busCharge(timing.sdramWrite, timing.holdWrite);
    if(upper) sdram[address >> 1 & 0x1ffff].byte(1) = data.byte(1);
    if(lower) sdram[address >> 1 & 0x1ffff].byte(0) = data.byte(0);
    return;
  }
}

auto M32X::loadTiming() -> void {
  timing = {};
  SH2::fetchModel = 0;
  SH2::insnExtra16 = 0;
  SH2::insnTiming = {};             //the SH-2's instruction timings (component/processor/sh2/timing.hpp): off
  SH2::insnTiming.build();
  string mode = timingMode;            //the frontend's menu (option "32X Timing"), else the environment
  if(!mode) if(auto e = getenv("ARES_32X_TIMING")) mode = e;
  const char* env = mode.data();
  if(!*env || !strcmp(env, "off")) return;
  //"console": measured on the console with the CD32X timing probe V1 (CD32X BootCamp probe/; the owner's NTSC
  //Sega CD + 32X, 28 Oct 2026; README-CD32X.md "Console timing" has every number).  The 32X's bus
  //arbitration is not fully documented: these are our own measurements.
  //  cart: 16-bit read 9.1 clocks, 32-bit 17.4, a cached line from the cart 66.4
  //  SDRAM: uncached 16- / 32-bit read 11.2, write 1.5 / 2.0, cached line 11.3, a cache hit 1.5 back to back
  //  frame buffer: read 8.2, write 1.5 alone / 4.8 back to back (the VDP's FIFO), fill 800 clocks for 256 words
  //  registers: COMM read 4.0, write 2.9, VDP status read 8.1; MUL.L back to back 3.8; BRA + NOP 3.0
  //  the shared bus: a cart read 9.1 alone, 17.4 / 20.7 / 13.0 / 13.0 while the other SH2 reads the cart /
  //  reads SDRAM / writes the frame buffer / writes SDRAM (SDRAM read: 11.2, 20.0 / 23.3 / 15.6 / 15.6;
  //  frame buffer write: 4.8, 9.0 / 10.9 / 8.1 / 6.2)
  if(!strcmp(env, "console")) env = "fetch=1,cpu=1,mulp=2,lalt=1,"
    "cart=7.6,cart2=8.3,cfill=1.8,sdr=9.7,sdr2=0,sdw=0.5,fill=10.3,"
    "fbr=6.7,fbw=0.5,fifo=4,drain=5,io=2.5,iow=1.9,vdpr=6.6,vfill=1,vfillw=3.06,"
    "cont=2,arb=2.4,hcart=6,hsdr=9.7,hwr=1.5";
  for(const char* p = env; *p;) {
    char key[16] = {};
    u32 n = 0;
    while(*p && *p != '=' && *p != ',' && n < 15) key[n++] = *p++;
    u32 v = 0, v16 = 0;               //the value, and the value x 16 (one decimal allowed: 7.6)
    if(*p == '=') {
      p++;
      while(*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
      v16 = v * 16;
      if(*p == '.') { p++; u32 f = 0, d = 1; while(*p >= '0' && *p <= '9') { if(d < 100) f = f * 10 + (*p - '0'), d *= 10; p++; } v16 += (f * 16 + d / 2) / d; }
    }
    while(*p && *p != ',') p++;
    if(*p == ',') p++;
    string k = key;
    if(k == "cart") timing.cartRead = v16, timing.cartRead2 = v16;
    if(k == "cart2") timing.cartRead2 = v16;
    if(k == "fbr") timing.fbRead = v16;
    if(k == "fbw") timing.fbWrite = v16;
    if(k == "fifo") timing.fifoDepth = v;
    if(k == "drain") timing.fifoDrain = v16;
    if(k == "sdr") timing.sdramRead = v16, timing.sdramRead2 = v16;
    if(k == "sdr2") timing.sdramRead2 = v16;
    if(k == "sdw") timing.sdramWrite = v16;
    if(k == "insn") timing.insn = v, SH2::insnExtra16 = v;
    if(k == "fetch") SH2::fetchModel = v;
    if(k == "io") timing.io = v16, timing.ioWrite = v16, timing.vdpRead = v16;
    if(k == "iow") timing.ioWrite = v16;
    if(k == "vdpr") timing.vdpRead = v16;
    if(k == "fill") timing.fill = v16;
    if(k == "cfill") timing.cfill = v16;
    if(k == "cont") timing.contention = v;
    if(k == "arb") timing.arb = v16;
    if(k == "hcart") timing.holdCart = v16;
    if(k == "hsdr") timing.holdSdram = v16;
    if(k == "hwr") timing.holdWrite = v16;
    if(k == "vfill") timing.vdpFill = v;
    if(k == "vfillw") timing.vdpFillWord = v16;
    auto& it = SH2::insnTiming;     //the SH-2's instruction timings (timing.hpp, cd32x-timing 20 Oct 2026)
    if(k == "cpu") it.enabled = v;
    if(k == "br") it.branch = v;
    if(k == "bt") it.condTaken = v;
    if(k == "bts") it.condTakenS = v;
    if(k == "mul") it.mul = v;
    if(k == "mulp") it.mulPair = v;
    if(k == "lu") it.loadUse = v;
    if(k == "mac") it.macRead = v;
    if(k == "tas") it.tas = v;
    if(k == "lalt") it.loadAlt = v;
  }
  SH2::insnTiming.build();
  auto c = [](u32 x16) -> string { return {x16 >> 4, ".", (x16 & 15) * 10 / 16}; };
  print("[32X timing] cart ", c(timing.cartRead), "/", c(timing.cartRead2), " fbr ", c(timing.fbRead), " fbw ", c(timing.fbWrite),
        " fifo ", timing.fifoDepth, "x", c(timing.fifoDrain), " sdr ", c(timing.sdramRead), "/", c(timing.sdramRead2),
        " sdw ", c(timing.sdramWrite), " io ", c(timing.io), "/", c(timing.ioWrite), " vdpr ", c(timing.vdpRead),
        " fill ", c(timing.fill), " cfill ", c(timing.cfill), " fetch ", SH2::fetchModel, " cpu ", (u32)SH2::insnTiming.enabled,
        " cont ", timing.contention, " arb ", c(timing.arb), " hold ", c(timing.holdCart), "/", c(timing.holdSdram), "/", c(timing.holdWrite),
        " vfill ", timing.vdpFill, "\n");
}

//the frontend's toggle (desktop: Settings > Real Sega 32X Hardware Timing): the new costs at once; while a game
//runs, both SH2s' recompiled blocks go too, since they carry the instruction timings
auto M32X::setTiming(string mode) -> void {
  timingMode = mode;
  loadTiming();
  if constexpr(SH2::Accuracy::Recompiler) {
    if(node) {
      for(auto sh : {&shm, &shs}) {
        sh->recompiler.allocator.release();
        sh->recompiler.reset();
      }
    }
  }
}

auto M32X::sh2Now() -> u64 {
  auto& sh = shm.active() ? shm : shs;
  u64 t = sh.total;
  if(SH2::Accuracy::Recompiler && shm.recompiler.enabled) t += sh.regs.CCR;
  return t;
}

//a frame buffer write: its own cost, plus a wait while the VDP's write FIFO is full
auto M32X::fbWriteCost() -> u32 {   //x16
  if(!timing.fifoDepth || !timing.fifoDrain) return timing.fbWrite;
  u64 now = sh2Now() * 16;
  if(timing.fifoEnd + 0x100000000ull < now || timing.fifoEnd > now + 0x100000000ull) timing.fifoEnd = now;  //clock rebased
  u64 start = max(timing.fifoEnd, now);
  timing.fifoEnd = start + timing.fifoDrain;
  u64 room = (u64)timing.fifoDepth * timing.fifoDrain;
  u32 stall = timing.fifoEnd > now + room ? u32(timing.fifoEnd - now - room) : 0;
  return timing.fbWrite + stall;
}

//this SH2 holds the bus for hold16 (x16) and spends busy16 (x16: the access and its wait) in the access.  The
//other SH2's wait comes from these: the share of the time this one is in bus accesses (util256) and how long
//it holds the bus each time (tenure16).  (cont=1, 20 Oct: both from the access's cost alone.)
auto M32X::busAccount(M32X::SH7604& self, u32 hold16, u32 busy16) -> void {
  u64 now = sh2Now();
  if(now < self.uStart || now - self.uStart > 0x1000'0000) { self.uStart = now; self.uBusy = 0; self.uCount = 0; self.uHold = 0; }
  self.uBusy += busy16;
  self.uHold += hold16;
  self.uCount++;
  if(now - self.uStart >= 2048) {
    u32 u = min<u64>(256, self.uBusy * 16 / (now - self.uStart));
    self.util256 = (self.util256 + u) / 2;
    self.tenure16 = self.uHold / self.uCount;
    self.uStart = now; self.uBusy = 0; self.uCount = 0; self.uHold = 0;
  }
}

//the expected wait (x16) for the bus while the other SH2 uses it: if it holds the bus a fraction u of
//the time in tenures of mean length t, an access waits on average u * (t / 2 + hand-over) (cont=1, 20 Oct);
//the probe (28 Oct) shows two SH2s that both keep the bus busy waiting a whole tenure each time: u * (t + hand-over)
auto M32X::busStall(M32X::SH7604& self, M32X::SH7604& other) -> u32 {
  if(!timing.contention) return 0;
  u32 u = other.util256;
  if(timing.contention == 1) return (u * (other.tenure16 / 2 + timing.arb) + 128) >> 8;
  return (u * (other.tenure16 + timing.arb) + 128) >> 8;
}

//this SH2's access costs clocks16 (x16); hold16 (x16, 0: the cost) is how long it keeps the shared bus
auto M32X::busCharge(u32 clocks16, u32 hold16) -> void {
  auto& self = shm.active() ? shm : shs;
  auto& other = shm.active() ? shs : shm;
  if(!shm.active() && !shs.active()) return;
  u32 c = clocks16;
  if(timing.contention && !self.inFill && clocks16) {
    c += busStall(self, other);
    busAccount(self, hold16 ? hold16 : clocks16, timing.contention == 1 ? (hold16 ? hold16 : clocks16) : c);
  }
  self.frac16 += c;
  u32 whole = self.frac16 >> 4;
  self.frac16 &= 15;
  if(whole) self.internalStep(whole);
}

auto M32X::SH7604::lineFillCost(u32 address) -> u32 {
  //SDRAM: one burst; other areas: the words were charged one by one, plus cfill (upstream 12).  x16 inside,
  //whole clocks out (the sixteenths left over wait in frac16)
  u32 c = (address >> 24 & 7) == 6 ? m32x.timing.fill : m32x.timing.cfill;
  u32 busy = (address >> 24 & 7) == 2 ? 4 * (m32x.timing.cartRead + m32x.timing.cartRead2) : c;
  if(m32x.timing.contention) {
    auto& other = this == &m32x.shm ? m32x.shs : m32x.shm;
    c += m32x.busStall(*this, other);
    m32x.busAccount(*this, busy, m32x.timing.contention == 1 ? busy : c);
  }
  frac16 += c;
  u32 whole = frac16 >> 4;
  frac16 &= 15;
  return whole;
}
