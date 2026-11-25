//The Mega EverDrive PRO's I/O registers at 0xa130d0 (krikzz's edio: everdrive.h / everdrive.c of mega-ed-pub) and
//the part of its MCU a game uses for files on the SD card: its FIFO commands, '+' '+'^0xff cmd cmd^0xff, then the
//command's bytes.  Modelled (Super Hang-On CD32X FX's saves, its docs/13):
//  0x10 STATUS (answers 0xA5, the last command's result), 0xC0 DISK_INIT, 0xC5 DIR_LD, 0x31 ROM_PATH, 0xC9 FOPN,
//  0xCA FRD, 0xCC FWR, 0xCE FCLOSE, 0xCF FPTR, 0xD0 FINFO, 0xD2 DIR_MK (answered, the folder not made).
//The card is a folder: ARES_EDIO_SD (default: none - every file command fails with 4, "no file", as a card without
//the file).  The ROM's path on it: ARES_EDIO_ROM (default /SHFX.32X).  ARES_EDIO_LOG=1 prints each command.
//Registers: +1 FIFODATA (byte), +2 FIFOSTAT (bit 15 = nothing for the CPU to read, bits 10-0 the bytes there),
//+4 SYSSTAT, +6 TIMER (ms).  Not modelled: the DMA commands (FRD_MEM / FWR_MEM: the halt handshake), USB, RTC.
struct EdioModel {
  std::vector<u8> rx, tx;                     //rx: CPU -> MCU; tx: MCU -> CPU
  u8 lastStatus = 0;
  FILE* file = nullptr;
  u32 wrLeft = 0, wrBlock = 0;              //a FWR in progress: bytes still to come, in this block
  std::string sdRoot, romPath;
  bool log = false, inited = false;
  u64 t0 = 0;

  auto init() -> void {
    if(inited) return;
    inited = true;
    if(auto e = getenv("ARES_EDIO_SD")) sdRoot = e;
    romPath = getenv("ARES_EDIO_ROM") ? getenv("ARES_EDIO_ROM") : "/SHFX.32X";
    log = getenv("ARES_EDIO_LOG") != nullptr;
  }
  auto host(const std::string& p) -> std::string { return sdRoot + (p.size() && p[0] == '/' ? p : "/" + p); }

  auto readIO(u32 address, n16 data) -> n16 {
    init();
    switch(address & 0xe) {
    case 0x0: { u8 b = 0; if(!tx.empty()) { b = tx.front(); tx.erase(tx.begin()); } return (n16)(b << 8 | b); }
    case 0x2: return (n16)((tx.empty() ? 0x8000 : 0) | std::min<size_t>(tx.size(), 0x7ff));
    case 0x4: return (n16)(0x55a0 | 0x01);
    case 0x6: { static u16 ms; return (n16)(ms++); }   //(the timer: a step each read)
    }
    return data;
  }
  auto writeIO(u32 address, n1 upper, n1 lower, n16 data) -> void {
    init();
    if((address & 0xe) != 0x0) return;
    rx.push_back(lower ? (u8)data : (u8)(data >> 8));
    if(getenv("ARES_EDIO_BYTES")) printf("[edio] byte %02x\n", rx.back());
    run();
  }

  //the command parser: runs when enough bytes are there
  auto have(size_t n) -> bool { return rx.size() >= n; }
  auto peek(size_t i) -> u8 { return rx[i]; }
  auto u16at(size_t i) -> u32 { return rx[i] << 8 | rx[i + 1]; }
  auto u32at(size_t i) -> u32 { return rx[i] << 24 | rx[i + 1] << 16 | rx[i + 2] << 8 | rx[i + 3]; }
  auto drop(size_t n) -> void { rx.erase(rx.begin(), rx.begin() + std::min(n, rx.size())); }
  auto put(u8 b) -> void { tx.push_back(b); }
  auto putStr(const std::string& s) -> void { put(s.size() >> 8); put(s.size()); for(char c : s) put((u8)c); }
  auto strAt(size_t i, std::string& s) -> bool {        //a string (u16 length, bytes) at i: false if not all there
    if(!have(i + 2)) return false;
    u32 n = u16at(i); if(!have(i + 2 + n)) return false;
    s.assign(rx.begin() + i + 2, rx.begin() + i + 2 + n); return true;
  }

  auto run() -> void {
    for(;;) {
      if(wrLeft) {                                         //a FWR's data: a block at a time, an answer before each
        if(!have(wrBlock)) return;
        std::vector<u8> b(rx.begin(), rx.begin() + wrBlock); drop(wrBlock);
        if(file) fwrite(b.data(), 1, b.size(), file);
        wrLeft -= wrBlock;
        if(wrLeft) { wrBlock = std::min<u32>(wrLeft, 1024); put(0); }
        else { lastStatus = file ? 0 : 1; if(file) fflush(file); }
        continue;
      }
      while(!rx.empty() && rx[0] != '+') rx.erase(rx.begin());     //(flushes, noise: wait for a command's start)
      if(!have(4)) return;
      if(peek(1) != (u8)('+' ^ 0xff) || peek(3) != (u8)(peek(2) ^ 0xff)) { drop(1); continue; }
      const u8 cmd = peek(2);
      std::string s;
      switch(cmd) {
      case 0x10: drop(4); put(0xa5); put(lastStatus); if(log) printf("[edio] STATUS -> %u\n", lastStatus); break;
      case 0xc0: drop(4); lastStatus = 0; if(log) printf("[edio] DISK_INIT\n"); break;
      case 0xc5: if(!strAt(5, s)) return; drop(7 + s.size()); lastStatus = sdRoot.empty() ? 4 : 0; if(log) printf("[edio] DIR_LD %s\n", s.c_str()); break;
      case 0xd2: if(!strAt(4, s)) return; drop(6 + s.size()); lastStatus = 0; 
        if(log) printf("[edio] DIR_MK %s\n", s.c_str()); break;
      case 0x31: if(!have(5)) return; drop(5); putStr(romPath); lastStatus = 0; if(log) printf("[edio] ROM_PATH -> %s\n", romPath.c_str()); break;
      case 0xc9: {
        if(!strAt(5, s)) return;
        const u8 mode = peek(4); drop(7 + s.size());
        if(file) { fclose(file); file = nullptr; }
        if(!sdRoot.empty()) {
          const std::string h = host(s);
          if(mode & 0x02) file = fopen(h.c_str(), (mode & (0x08 | 0x04)) ? "w+b" : (mode & 0x10) ? "a+b" : "r+b");
          else file = fopen(h.c_str(), "rb");
          if(!file && (mode & 0x02) && (mode & 0x10)) file = fopen(h.c_str(), "w+b");
          if(file && (mode & 0x30) == 0x10) fseek(file, 0, SEEK_SET);
        }
        lastStatus = file ? 0 : 4;
        if(log) printf("[edio] FOPN %s mode %02x -> %u\n", s.c_str(), mode, lastStatus);
        break;
      }
      case 0xca: {
        if(!have(8)) return;
        const u32 n = u32at(4); drop(8);
        if(!file) { put(4); break; }
        std::vector<u8> b(n, 0); size_t got = fread(b.data(), 1, n, file); (void)got;
        put(0); for(u8 c : b) put(c);
        if(log) printf("[edio] FRD %u (%zu)\n", n, got);
        break;
      }
      case 0xcc: {
        if(!have(8)) return;
        const u32 n = u32at(4); drop(8);
        if(log) printf("[edio] FWR %u\n", n);
        if(!n) { lastStatus = 0; break; }
        wrLeft = n; wrBlock = std::min<u32>(n, 1024); put(file ? 0 : 4);
        if(!file) { wrLeft = 0; lastStatus = 4; }
        break;
      }
      case 0xce: drop(4); if(file) { fclose(file); file = nullptr; } lastStatus = 0; if(log) printf("[edio] FCLOSE\n"); break;
      case 0xcf: { if(!have(8)) return; const u32 a = u32at(4); drop(8); lastStatus = file && !fseek(file, a, SEEK_SET) ? 0 : 4; break; }
      case 0xd0: {
        if(!strAt(4, s)) return;
        drop(6 + s.size());
        FILE* f = sdRoot.empty() ? nullptr : fopen(host(s).c_str(), "rb");
        if(!f) { put(4); if(log) printf("[edio] FINFO %s -> none\n", s.c_str()); break; }
        fseek(f, 0, SEEK_END); u32 size = (u32)ftell(f); fclose(f);
        put(0); put(size >> 24); put(size >> 16); put(size >> 8); put(size); for(int i = 0; i < 5; i++) put(0);
        auto slash = s.find_last_of('/'); putStr(slash == std::string::npos ? s : s.substr(slash + 1));
        if(log) printf("[edio] FINFO %s -> %u\n", s.c_str(), size);
        break;
      }
      default: drop(4); lastStatus = 0; if(log) printf("[edio] command %02x: not modelled\n", cmd); break;
      }
    }
  }
};
