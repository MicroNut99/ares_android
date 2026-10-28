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
    if(shm.active()) shm.internalStep(6); if(shs.active()) shs.internalStep(6);
    return cartridge.child->read(upper, lower, address, data);
  }

  if(address >= 0x0400'0000 && address <= 0x05ff'ffff) {
    if(!vdp.framebufferAccess) return data;
    if(vdp.framebufferEngaged()) { debug(unusual, "[32X FB] SH2 read while FEN==1"); return data; } // wait instead?
    if(shm.active()) shm.internalStep(5); if(shs.active()) shs.internalStep(5);
    return vdp.bbram[address >> 1 & 0xffff];
  }

  if(address >= 0x0600'0000 && address <= 0x0603'ffff) {
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
      if(shm.active()) shm.internalStep(4); if(shs.active()) shs.internalStep(4);
      if(upper) vdp.bbram[address >> 1 & 0xffff].byte(1) = data.byte(1);
      if(lower) vdp.bbram[address >> 1 & 0xffff].byte(0) = data.byte(0);
      return;
    }

    if(address >= 0x0402'0000 && address <= 0x0403'ffff) {
      if(!vdp.framebufferAccess) return;
      if(vdp.framebufferEngaged()) { debug(unusual, "[32X FB] SH2 overwrite while FEN==1"); return; } // wait instead?
      if(shm.active()) shm.internalStep(4); if(shs.active()) shs.internalStep(4);
      if(upper && data.byte(1)) vdp.bbram[address >> 1 & 0xffff].byte(1) = data.byte(1);
      if(lower && data.byte(0)) vdp.bbram[address >> 1 & 0xffff].byte(0) = data.byte(0);
      return;
    }
  }

  if(address >= 0x0600'0000 && address <= 0x0603'ffff) {
    if(upper) sdram[address >> 1 & 0x1ffff].byte(1) = data.byte(1);
    if(lower) sdram[address >> 1 & 0x1ffff].byte(0) = data.byte(0);
    return;
  }
}
