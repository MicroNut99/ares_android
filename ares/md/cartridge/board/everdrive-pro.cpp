//Mega EverDrive PRO (krikzz) in its extended SSF mode: 16 MB of RAM in 32 banks of 512 KB,
//switched into the eight 512 KB windows of the 4 MB cartridge space.
//
//Checked on a console (Genesis + 32X + Mega EverDrive PRO, CD32X BootCamp edtest, 24 Oct 2026):
//  - the mapper is on for a ROM whose header says "SEGA SSF" (0x100), started from the EverDrive menu;
//  - CTRL0 = 0xa000 (P + W) and 0xe000 (P + X + W) both work: word writes by the 68000, RV = 1;
//  - CTRL1-7 put a bank (1-27 tested) into each window of cart 0x080000-0x3fffff;
//  - the 68000 writes the banks (RV = 1) and reads back what it wrote after all the other banks
//    were switched in and out; both SH2s read the same data at 0x22000000 + the window's offset.
//Checked on a console, 25 Oct 2026 (CD32X BootCamp edw0 and the cartridge boot), and modelled here:
//  - with W = 1, a 68000 write to anything but the cart (work RAM, the VDP, the Sega CD's gate array)
//    disturbs the cart while an SH2 reads it: the SH2's reads come back as zeros at that moment, and
//    words change (edw0: 32d6 read back as 32d5 after a run of 68000 writes).  With W = 0 the SH2
//    reads it perfectly through anything the 68000 does, a change of W itself does no harm, and the
//    68000's own writes with W = 1 are clean while the SH2s stay off the cart (the cartridge boot's
//    copies, with one stack push inside the W = 1 window, work).  Model (disturb(), from the 68000
//    bus): an SH2 read of the cart within 1 us of such a write returns 0 and the word takes the
//    68000's data.  A 68000 write with RV = 1 disturbs nothing here: an SH2 cart read then is garbage
//    anyway (next point).  Not seen on the console: the exact overlap, which word changes - the model is
//    deterministic, not exact.  A launcher that leaves W on fails here as on the console.
//Checked on a console, 26 Oct 2026 (CD32X BootCamp edsave3), and modelled here (answersDuringRV(),
//m32x/bus-internal.cpp):
//  - the EverDrive does NOT hold an SH2 off the cart while the 68000 holds RV = 1, as a real cartridge
//    is held (the 32X stalls its read until RV = 0): it answers, with garbage.  The probe: the slave
//    read window 4 nonstop and the master window 2 between its polls while the 68000 copied 64 x 4 KB
//    blocks with RV = 1 - 965,207 bad of 978K reads, coming back about four times faster than real
//    ones; with the 68000 idle 0 bad of 393K, with RV = 0 4.  OpenLara's passport froze on it (the
//    master SH2 ran garbage it had fetched from the cart during a save).  ares: such a read returns
//    a pseudo-random word (from the address and the SH2's clock: the same in every run) after about a
//    quarter of a cart read's time.  Off with ARES_RV_GARBAGE=0; ARES_RV_LOG=1 prints who read.
//  - the EverDrive menu wipes the PSRAM (banks 0-29) every time it runs (edsave, three runs: markers
//    in banks 0 and 7 gone at every start; bank 31, the battery RAM, kept).  ares: a launcher boot
//    (program.rom) fills banks 0-29 with the pattern before it puts the launcher in, at power-on and
//    at a reset, so every start loads everything again, as on the console.
//  - the 68000 cannot write window 0 (cart 0x000000-0x07ffff) directly with the 32X on: a copy there
//    goes through another window with window 0's bank mapped in (mega-32x.cpp drops the direct write;
//    0x000000-0x0000ff goes to the 32X, as on the console).
//  - W = 0 does write-protect the memory (the probe's pattern was never changed with W off).
//krikzz's own SSF mapper source (github.com/krikzz/MEGA-PRO, fpga/mapper/map_ssf): banks 0-15 and
//16-30 are the two 8 MB PSRAM chips, bank 31 is the backup memory the OS keeps on the SD card
//(a .srm for a ROM whose header declares save RAM); CTRL0 bit 11 #CART, 12 LED, 13 W, 14 X.
//Not checked on a console, so the choices below are guesses: banks 28-31, the C (#CART) and L (LED)
//bits, byte writes to the registers, the state after a console reset, and whether the memory keeps
//its contents across power-off.
//
//Registers (write-only, in /TIME space):
//  0xa130f0      CTRL0  P X W L C . . . . . . R R R R R  - taken only when P (bit 15) is 1
//                P protection, X 32X mode (no effect seen), W write enable, L LED,
//                C #CART: 1 = CART_IN released, the console sees no cartridge (a Mega CD then
//                boots its disc and the cart moves to 0x400000), R the bank in window 0
//  0xa130f2-fe   CTRL1-7  bits 0-4: the bank in window 1-7
//A byte write to 0xa130f1 (the old SRAM switch of Sega's mapper) carries its byte on D8-D15 as
//well, so P is that byte's bit 7: ordinary SRAM switch values are ignored.
//Banks: 31 is the PRO's save RAM (kept on its SD card), 28's first 32 KB is a save area, 30-31 hold
//the EverDrive's own software. Games keep out of 28-31.
//
//Three ways the board is used (pak attributes):
//  - cartridge (mia: "SEGA SSF" and no SRAM entry in the header): the ROM at the start of the memory,
//    booted (CART_IN asserted),
//    W = 0 until the game sets it; bank 31 is the game's save RAM ("save.ram"), the rest is blank;
//  - RAM cart for a Sega CD 32X disc (ramcart, not bootable): the state a launcher ROM leaves
//    (W = 1, C = 1, banks 0-7 in windows 0-7), so the disc boots and finds a 16 MB RAM cart;
//    all 16 MB come from and go back to "ram.cart";
//  - launcher (ramcart, bootable, "program.rom"): as the RAM cart, but the console first boots the
//    launcher ROM from bank 0 (W = 0, C = 0, as the EverDrive menu starts it); the launcher sets
//    the registers and C itself. After a reset the launcher is put back in bank 0, as starting it
//    again from the EverDrive menu would.

struct EverDrivePro : Interface {
  using Interface::Interface;
  Memory::Writable<n16> ram;     //16 MB: bank n is words n << 18 ... (n << 18) + 0x3ffff
  n5 bank[8];                    //the bank in each 512 KB window
  n1 mode32x;                    //X
  n1 writeEnable;                //W
  n1 led;                        //L
  n1 cartOff;                    //C: CART_IN released
  boolean writeProtect;          //the frontend's write-protect switch: no writes, whatever W says
  boolean ramCart;               //"ram.cart" holds all 16 MB
  boolean cdBoot;                //RAM cart use: starts as a launcher left it
  std::vector<u8> program;       //the ROM booted at power-on (cartridge and launcher use)
  //the W = 1 disturbance: when an SH2 read the cart last, and the burst of 68000 writes elsewhere
  u32 sh2Word = 0;               //the word an SH2 read last
  u64 sh2Clock = 0;              //when (Thread::clock units, the same on every thread)
  u64 distFirst = 0, distLast = 0;  //the current burst of disturbing writes (gaps under Window)
  n16 distData = 0;              //the last such write's data
  static constexpr u64 Window = Thread::Second / 1'000'000;  //1 us: a 68000 bus cycle is 0.5 us
  static auto near(u64 a, u64 b) -> bool { return (a > b ? a - b : b - a) <= Window; }

  static constexpr u32 SaveBank = 31;

  //The cart's FPGA configuration, mega-pro.rbf (7 Nov 2026, Metal Slug CD32X docs/14 and fpga/README_FOR_AI.md):
  //the EverDrive menu loads the file of that name from the folder of the ROM it starts - a CD32X launcher
  //(EDCD_*.32X) included - into the FPGA instead of its built-in mapper (krikzz/mega-ed-pub fpga/README.md;
  //console, 7 Nov 2026: Metal Slug v46 boots with krikzz's map_ssf beside its launcher).  The frontends put
  //that file in the pak as "mega-pro.rbf" (and a "mega-pro.rbf.ares" beside it, a line naming the model,
  //for a configuration of our own).  ares does not run a bitstream: it knows each configuration by its
  //SHA-256 and models what that configuration is.  Off with ARES_RBF=0 (the file is ignored).
  string rbfModel;        //"" (no file: the built-in SSF mapper), "ssf", or what it was found to be
  struct RbfKnown { const char* sha256; const char* name; const char* model; };
  static constexpr RbfKnown rbfKnown[] = {
    //krikzz/mega-ed-pub 7064f45, fpga/fpga_pro/*/output_files/mega-pro.rbf (MIT); map_ssf also built
    //byte-identical with Quartus 25.1std Lite on the owner's PC (Metal Slug docs/14)
    {"00bb72f427bcc35ba7486386a9f65cdce6ce9089981716e0f386fb8563946327", "krikzz map_ssf (the extended SSF mapper)", "ssf"},
    {"6c239eb96f07806e560868a11002076ca5124b693d0385ac1d50b943022438da", "krikzz map_smd (the plain Genesis mapper)", ""},
    {"dc856c994b6f677d92c56e0fdae26e7b3c0d9d12fb53ec744f7d2305712dfc02", "krikzz map_smd_cd (Genesis mapper, MD+)", ""},
    {"d1616d7617932b409c01605f12344af275fec8c25b0a69eca50c20aaef137b75", "krikzz map_svp (Virtua Racing's SVP)", ""},
    {"82df47ae5e547e9ab496dd64e04a072bbeb976b35369a61c79123cac8bc8b5a2", "krikzz map_mcd (the Mega-CD core)", ""},
    {"ca5c1423c347d1ef7279362ac30905f1d9cca272a0b5181a1c7416af563caa80", "krikzz SE (the simplified mapper)", ""},
    //the CD32X cart compositor (CD32X-BootCamp cartfx/quartus/RBF_SHA256.txt, v2.2 and v2.1, 13 Nov 2026; fpga-cartfx branch)
    {"147a841ae9e29571643f9dd8055c662fefa22069332d92b922ef6aac179ed3a6", "cartfx v2.2 for Hurrican CD32X", "cartfx-hur"},
    {"764d9bb646d43c7e9d223379b51e16caef579b35e6d8dc3d91dc261ca4477666", "cartfx v2.2 for Jazz CD32X REV", "cartfx-oj"},
    {"3220a5d76c4fccede5b5fd817d7d7e778ce8b20b3ba40461a63bc306aae37554", "cartfx v2.2 for SuperTux CD32X REV", "cartfx-stx"},
    {"21a53854c48359c8cc506c1e7eb8036ebb3f5dd00f4138fccfb965d5896927a9", "cartfx v2.1 for Hurrican CD32X", "cartfx-hur"},
    {"74d8b8b5e840222111b1ca96d95186912da063e1d0fa19f313ba8b66b884ac4e", "cartfx v2.1 for Jazz CD32X REV", "cartfx-oj"},
    {"768bcc63ed39b01e62f72ba4c3819b3a06f5938c350a14c3bad1c82d96156c1a", "cartfx v2.1 for SuperTux CD32X REV", "cartfx-stx"},
  };
  static constexpr const char* rbfModels[] = {"ssf", "dracx", "gotx", "cartfx-hur", "cartfx-oj", "cartfx-stx"};    //the models this board has (more as cores are added)

  auto identifyRbf() -> void {
    rbfModel = "";
    auto fp = pak->read("mega-pro.rbf");
    if(!fp) return;
    if(auto e = getenv("ARES_RBF"); e && string{e} == "0") { print("[EverDrive PRO] mega-pro.rbf ignored (ARES_RBF=0)\n"); return; }
    std::vector<u8> data(fp->size());
    fp->read({data.data(), data.size()});
    string digest = Hash::SHA256(data).digest();
    string name, model;
    for(auto& k : rbfKnown) if(digest == k.sha256) { name = k.name; model = k.model; }
    if(auto side = pak->read("mega-pro.rbf.ares")) {     //a configuration of our own: its model by name
      string line;
      for(u32 n : range(side->size())) { char c = side->read(); if(c == '\n' || c == '\r') break; line.append(c); }
      line.strip();
      if(line) { model = line; if(!name) name = {"our own configuration (", line, ")"}; }
    }
    bool known = false;
    for(auto m : rbfModels) if(model == m) known = true;
    if(!name) name = "an unknown configuration";
    if(known) {
      rbfModel = model;
      print("[EverDrive PRO] mega-pro.rbf: ", name, " (sha256 ", digest.slice(0, 16), "...): modelled as '", model, "'\n");
    } else {
      rbfModel = "ssf";
      print("[EverDrive PRO] mega-pro.rbf: ", name, " (sha256 ", digest.slice(0, 16), "...)", model ? string{" - model '", model, "'"} : string{},
            ": NOT modelled - ares runs the built-in SSF mapper; the console would run that file\n");
    }
  }

  //DraculaX CD32X (MicroNut99/DraculaX_32X docs/100, docs/101): the 'dracx' configuration - map_ssf with the PC Engine
  //CD engine beside it. Its contract with the 68000 and the SH2 is two banks: window 7's (the sector feed, the register
  //window, the CD unit's load port, the start, the pad) and window 6's (the video window). The engine is the machine
  //its FPGA was proven against (board/dracx: the cart's C models, equal to Mednafen from power-on): the cart's thread
  //runs it at the PC Engine's master clock, and an access to those banks brings it to the accessing CPU's time first.
  //Not modelled: the engine's holds of the master clock for its own memories (no time in the game's), the save state.
  bool dracx = false;                       //rbfModel == "dracx"
  struct Dx {
    bool on = false;                        //started (the read of + 0x40080)
    bool armed = false;                     //the load port's word 0xFF was 'DXON' (DraculaX docs/102): the start read may start
    u16 cfgHi = 0; u32 cfg[256] = {};       //the load port: FwState's words (cd_fw.h), 0xC0 / 0xC1 the two steps
    u32 lblHi = 0, lbl = 0;                 //the sector label, its high part, then the whole
    bool full = false; u32 have = 0;        //the spare holds sector `have`
    u8 sec[2048] = {};
    u8 bram[2048] = {}; bool bramSet = false;   //(DraculaX docs/118) the backup RAM's port: written before the start
    u8 pad = 0;                             //the pad (Mednafen's order), before and after the start
    u32 tab[3 + 6 * 101] = {};              //the disc's table for the machine, from the load port
    std::vector<u8> rom;                    //the System Card, from bank 16 at the start
  } dx;
  static constexpr u32 MasterClock = 21'477'270;   //the PC Engine's (NTSC 6 x 3.579545 MHz)

  //Gate of Thunder CD32X (MicroNut99/Gate-of-Thunder_CD32X docs/36, docs/37): the 'gotx' configuration - map_ssf (krikzz's,
  //unchanged) with GoT's PC Engine CD engine beside it. Its whole contract is bank 26 (docs/36 §2, docs/38: page 6's window 6):
  //+0x00000-0x213FF the video window (VRAM, stamps, colours, sprite table, snapshot, line table), +0x40000 the register window
  //(+0x40080 the start), +0x41000 the CD unit's load port (word 0xFF the key 'GTON'), +0x48000 the pad. No sector feed: the
  //data track is in PSRAM chip 1 from bank 16 (track 2, LBA 3590 on, 2 KB a sector, 64 KB skipped at sector 992: docs/38),
  //the System Card at bank 27. The machine is gotm.c (board/gotx: Dracula X's dxm.c with GoT's timer, equal to Mednafen from
  //power-on, docs/35), in namespace gotx. Not modelled, as dracx: the engine's own master-clock holds, the save state.
  bool gotx = false;                        //rbfModel == "gotx"
  inline static u64 gxPadReads = 0;
  struct Gx {
    bool on = false, armed = false;         //started (the read of + 0x40080); the key taken
    u16 cfgHi = 0; u32 cfg[256] = {};       //the load port: FwState's words (cd_fw.h)
    u8 pad = 0;
    u32 tab[3 + 6 * 101] = {};
    std::vector<u8> rom;                    //the System Card, from bank 27 at the start
    bool autorun = false, asked = false, live = false, fell = false; s32 want0 = 0;   //(GoT docs/41, 45: the key 'GTAR', the traps)
  } gx;
  //Error traps of the PC Engine CD cart cores (Dracula X CD32X docs/175-178, Gate of Thunder CD32X docs/45 and its
  //CONSOLE_FINDINGS.md; README-CD32X "Error traps"): what the console's cart does differently from a plain emulation,
  //modelled as the core does it and printed - "[<core> TRAP] ..." the first time, then at the 2nd, 4th, 8th ... time - so an
  //ares run shows the console's failure modes.  The PC Engine CD cores share them (both give PSRAM chip 1 to the engine
  //once it runs); a new core gets a row in TrapCore and uses the same kinds.
  //  kind 0/1: the console reads / writes PSRAM chip 1 (banks 16-30) after the start, outside the core's own windows
  //  kind 2: the PC Engine CPU runs in the I/O page (below $2000) after having run above it (gotx: the last 64 PCs)
  //  kind 3: SELECT asked by the SH2 (the cores never give it: RUN + SELECT is the PC Engine's soft reset)
  //  kind 4: the backup RAM differs from the cart's (the cart's sram is uninitialised)
  enum TrapCore : u32 { TrapDracx, TrapGotx, TrapCartfx, TrapCores };
  inline static u64 trapN[TrapCores][8] = {};
  static auto coreTrap(u32 core, u32 kind, const string& what) -> void {
    static const char* names[TrapCores] = {"dracx", "gotx", "cartfx"};
    u64 n = ++trapN[core][kind & 7];
    if(n == 1) print("[", names[core], " TRAP] ", what, "\n");
    else if((n & (n - 1)) == 0) print("[", names[core], " TRAP] (x", n, ") ", what, "\n");
  }
  auto gxTrap(u32 kind, const string& what) -> void { coreTrap(TrapGotx, kind, what); }
  auto dxTrap(u32 kind, const string& what) -> void { coreTrap(TrapDracx, kind, what); }
  static constexpr u32 GxBank = 26, GxDataBank = 16, GxScBank = 27, GxLba0 = 3590, GxSectors = 1882, GxPageSectors = 992;

  auto frequency() -> u32 override { return dracx || gotx ? MasterClock : Interface::frequency(); }

  //the cart's thread: the machine to its own time, a few hundred master clocks at a time; while it waits for a
  //sector (dxm_waiting), its time stands still and only the thread's goes on
  auto main() -> void override {
    if(gotx) {
      if(!gx.on) return step(256);
      if(getenv("GOTX_TRACE")) { static u64 n; if(++n % 83'000 == 0) print("[gotx] machine frame ", gotm_frame(), " time ", (s64)gotm_now(), " drive next ", gotm_drive_next(), " missed ", gotm_missed(), " pad reads ", gxPadReads, " pad ", hex(gx.pad, 2L), "\n"); }
      s64 t0 = gotm_now(), t = gotm_run_until(t0 + 256);
      {   //(docs/45: the CPU in the I/O page - the cart's ring stands there; not the reset's own first PCs)
        unsigned nx; const u16* pcs = gotm_pcs(&nx); u16 pc = pcs[(nx + 511) & 511];
        if(pc >= 0x2000) gx.live = true;
        else if(gx.live && !gx.fell) {
          gx.fell = true; string l = {"the PC Engine CPU runs at PC ", hex(pc, 4L), " (the I/O page) at machine frame ", gotm_frame(), "; the last 64 PCs:"};
          for(u32 i : range(64)) l.append(" ", hex(pcs[(nx + 448 + i) & 511], 4L));
          gxTrap(2, l);
        }
      }
      return step(t <= t0 ? 256 : (u32)(t - t0));
    }
    if(!dracx) return Interface::main();
    if(!dx.on) return step(256);
    if(getenv("DRACX_TRACE")) {   //(a line about every second of the cart's thread: the machine's state)
      static u64 n; if(++n % 83'000 == 0) print("[dracx] machine frame ", dxm_frame(), " time ", (s64)dxm_now(), " waiting ", dxm_waiting(),
        " drive next ", dxm_drive_next(), " spare ", dx.full ? (s32)dx.have : -1, " missed ", dxm_missed(), "\n");
    }
    s64 t0 = dxm_now(), t = dxm_run_until(t0 + 256);
    step(dxm_waiting() || t <= t0 ? 256 : (u32)(t - t0));
  }

  //DRACX_MASKS=<file> (DraculaX docs/104): the pad from a recording instead of the SH2's - a u16 libretro mask a frame
  //from the machine's power-on, each taken at the frame's end as tools/m2host/dxm_pc.c does, so the run is the
  //recording's (Mednafen's, frame for frame). DRACX_HASHES=<file>: each frame's dxm_hash, to set against dxm_pc's.
  inline static std::vector<u16> dxMasks;
  inline static FILE* dxHashes = nullptr;
  static auto dxFrame(void*, long k) -> void {
    if(dxHashes) { fprintf(dxHashes, "frame %ld hash %08x\n", k, dxm_hash(0)); fflush(dxHashes); }
    if(!dxMasks.empty()) dxm_pad(k + 1 < (long)dxMasks.size() ? dxMasks[k + 1] : 0);
  }
  auto dxReplay() -> void {
    dxMasks.clear();
    if(auto p = getenv("DRACX_MASKS")) if(FILE* f = fopen(p, "rb")) {
      u16 m; while(fread(&m, 2, 1, f) == 1) dxMasks.push_back(m); fclose(f);
      print("[EverDrive PRO] dracx: the pad from ", p, " (", dxMasks.size(), " frames)\n");
    }
    if(auto p = getenv("DRACX_HASHES")) dxHashes = fopen(p, "w");
    if(!dxMasks.empty() || dxHashes) dxm_on_frame(dxFrame, nullptr);
  }

  //an access by the 68000 or an SH2: the machine brought to its time first
  auto dxSync() -> void {
    if(cpu.active()) cpu.synchronize(*cartridge);
    else if(m32x.shm.active()) m32x.shm.synchronize(*cartridge);
    else if(m32x.shs.active()) m32x.shs.synchronize(*cartridge);
  }

  static auto dxHave(void* user, s32 lba) -> int {
    auto& d = ((EverDrivePro*)user)->dx; return d.full && (s32)d.have == lba;
  }
  static auto dxSector(void* user, s32 lba, u8* out) -> int {
    auto& d = ((EverDrivePro*)user)->dx;
    if(!d.full || (s32)d.have != lba) return 0;
    ::memcpy(out, d.sec, 2048); d.full = false;   //the CD unit takes the spare
    return 1;
  }

  //the start: the System Card (bank 16's first 256 KB, as the 68000 copied it), the disc's table (FwState's words:
  //first, last, leadout, leadout_control at 12-15, then lba, sectors, pregap, control a track for tracks 0-32)
  auto dxStart() -> void {
    dx.rom.resize(256_KiB);
    for(u32 n : range(128_KiB)) { n16 w = ram[16 << 18 | n]; dx.rom[2 * n] = w >> 8; dx.rom[2 * n + 1] = w; }
    ::memset(dx.tab, 0, sizeof dx.tab);
    dx.tab[0] = dx.cfg[12]; dx.tab[1] = dx.cfg[13]; dx.tab[2] = dx.cfg[14];
    for(u32 t : range(33)) {
      u32* x = dx.tab + 3 + 6 * t; const u32* c = dx.cfg + 16 + 4 * t;
      x[0] = c[0]; x[1] = c[1]; x[2] = c[2]; x[3] = c[3]; x[4] = (c[3] & 4) ? 1 : 0; x[5] = (c[3] & 4) ? 2048 : 2352;
    }
    std::vector<u8> bram;   //DRACX_BRAM=<file> (2 KB): the backup RAM at power-on, for a recording's replay (the cart: zeros)
    if(auto p = getenv("DRACX_BRAM")) if(FILE* f = fopen(p, "rb")) { bram.resize(2048); if(fread(bram.data(), 1, 2048, f) != 2048) bram.clear(); fclose(f); }
    if(bram.empty() && dx.bramSet) bram.assign(dx.bram, dx.bram + 2048);   //(docs/118: what the 68000 wrote to the port)
    if(!dxm_power(dx.rom.data(), bram.empty() ? nullptr : bram.data(), (const s32*)dx.tab, dxSector, this)) {
      print("[EverDrive PRO] dracx: the disc's table from the load port was not taken\n"); return;
    }
    dxm_feed(dxHave);
    dxReplay();
    if(dxMasks.empty()) dxm_pad_now(dx.pad);
    dx.on = true;
    print("[EverDrive PRO] dracx: the engine started (tracks ", dx.tab[0], "-", dx.tab[1], ", lead-out ", (s32)dx.tab[2], ")\n");
  }

  //window 7's bank (docs/100 §2): what a read there does and answers (false: the bank's own memory answers)
  auto dxRead7(u32 off, n16& value) -> bool {
    if(off < 0x10000) { dx.lbl = dx.lblHi << 12 | (off >> 4 & 0xfff); dx.full = false; return false; }   //label, low
    if(off >= 0x10000 && off < 0x12000) { dx.lblHi = off >> 4 & 0x1ff; return false; }                  //label, high
    if(off >= 0x48000 && off < 0x49000) { dx.pad = off >> 4; if(dx.on && dxMasks.empty()) dxm_pad_now(dx.pad); return false; }
    if(off >= 0x40000 && off < 0x40100) {                                                                //the registers
      dxSync();
      if(off >> 1 == 0x40080 >> 1 && !dx.on && dx.armed) dxStart();   //(not before: the 32X's own boot reads the cart there)
      u32 want = dx.on ? (u32)dxm_drive_next() : 0;
      u32 a = dx.on ? dxm_audio() : 0;
      if(off & 0x10) {   //(DraculaX docs/125, step 22: the console diagnostic's words - the model has no PC or edge count: 0)
        u32 k = off >> 1 & 7;
        value = k == 4 ? (n16)(dx.on ? dxm_frame() : 0) : k == 5 ? (n16)want : k == 6 ? (n16)((dx.armed ? 0x8000 : 0) | (dx.on ? 0x4000 : 0))
              : k == 7 ? (n16)0xd122 : (n16)0;
        return true;
      }
      if(off & 8) value = (n16)a;                                               //word 4: CD-DA's play's start, 15-0
      else if(off & 4) value = off & 2 ? (a >> 31 ? 0x8000 : 0) | (a >> 24 & 3) << 13 | (a >> 16 & 0x1f)   //word 3
                                       : 0;                                     //word 2: the load port's busy bit: never
      else if(off & 2) value = (dx.full && dx.have == want ? 0x8000 : 0) | (want >> 16 & 0x1f);
      else value = (n16)want;
      return true;
    }
    return false;
  }
  auto dxWrite7(u32 off, n1 upper, n1 lower, n16 data) -> void {
    if(off >= 0x60000 && off < 0x60800) {                                       //the sector's 2 KB (command 91)
      u32 b = off & 0x7fe;
      if(upper) dx.sec[b] = data >> 8;
      if(lower) { dx.sec[b + 1] = data; if(b == 0x7fe) { dx.full = true; dx.have = dx.lbl;
        if(auto p = getenv("DRACX_FEEDLOG")) { static FILE* f = fopen(p, "w"); if(f) { fprintf(f, "%u\n", (unsigned)dx.lbl); fflush(f); } } } }   //(the sectors fed, in order: DraculaX docs/171)
    } else if(off >= 0x50000 && off < 0x50800) {                                //the backup RAM (docs/118): bytes 2k, 2k + 1
      u8* b = dx.on ? dxm_bram() : dx.bram; u32 k = off & 0x7fe;
      if(upper) b[k] = data >> 8;
      if(lower) b[k + 1] = data;
      if(!dx.on) dx.bramSet = true;
    } else if(off >= 0x41000 && off < 0x41400) {                                //the load port
      if(!(off & 2)) dx.cfgHi = data;
      else if((off >> 2 & 0xff) == 0xff) dx.armed = (dx.cfgHi << 16 | data) == 0x44584f4e;   //the key, not the CD unit's
      else dx.cfg[off >> 2 & 0xff] = dx.cfgHi << 16 | data;
    }
  }
  //window 6's bank: the video window (docs/96 §5-6) - VRAM's words, then the stamps, the colours, the sprite table,
  //the frame's snapshot
  auto dxRead6(u32 off, n16& value) -> bool {
    if(off >= 0x22000) return false;            //(map_dx's vw_ce: + 0x00000-0x1ffff, + 0x20000-0x21fff)
    if(off >= 0x21800) {                        //(docs/118: the backup RAM - word k: bytes 2k high, 2k + 1 low)
      if(dx.on) dxSync();
      const u8* b = dx.on ? dxm_bram() : dx.bram; u32 k = off & 0x7fe;
      value = b[k] << 8 | b[k + 1]; return true;
    }
    if(!dx.on) { value = 0; return true; }
    dxSync();
    u32 w = off >> 1;
    if(w < 0x10000) value = dxm_vram()[w];
    else if((w -= 0x10000) < 0x400) value = dxm_stamps()[w];
    else if(w < 0x600) value = dxm_colors()[w - 0x400] & 0x1ff;
    else if(w < 0x700) value = dxm_sat()[w - 0x600];
    else if(w < 0x705) value = dxm_snapshot()[w - 0x700];
    else if(w >= 0x800) value = dxm_lines()[(w & 0xff) * 2 + (w >> 8 & 1)] & (w & 0x100 ? 0x1ff : 0x3ff);   //(docs/105: X, then Y, a line)
    else value = 0;
    return true;
  }

  //GOTX_MASKS / GOTX_HASHES / GOTX_BRAM: as DRACX_* (a recording's pad from power-on, each frame's gotm_hash, the backup RAM)
  inline static std::vector<u16> gxMasks;
  inline static FILE* gxHashes = nullptr;
  static auto gxFrame(void* user, long k) -> void {
    if(gxHashes) { fprintf(gxHashes, "frame %ld hash %08x\n", k, gotm_hash(0)); fflush(gxHashes); }
    if(auto* e = (EverDrivePro*)user; e && e->gx.autorun && gxMasks.empty()) {   //(GoT docs/41: RUN 16 frames in every 512 until the drive is asked)
      if(gotm_drive_next() != e->gx.want0) e->gx.asked = true;
      gotm_pad_now(e->gx.pad | (!e->gx.asked && ((k + 1) & 0x1f0) == 0x180 ? 0x08 : 0x00));
    }
    if(!gxMasks.empty()) gotm_pad(k + 1 < (long)gxMasks.size() ? gxMasks[k + 1] : 0);
    if(auto p = getenv("GOTX_POKE")) { unsigned a, v; if(sscanf(p, "%x,%x", &a, &v) == 2) gotm_base_ram()[a & 0x1fff] = (u8)v; }   //(the recordings' lives poke)
  }
  //a sector from PSRAM chip 1 (the data track from bank 16, the even byte the word's high half: docs/33)
  static auto gxSector(void* user, s32 lba, u8* out) -> int {
    auto* e = (EverDrivePro*)user;
    if(lba < (s32)GxLba0 || lba >= (s32)(GxLba0 + GxSectors)) return 0;
    u32 s = (u32)(lba - GxLba0);   //(docs/38: from sector 992, 64 KB on - the save area's hole at page 4's top)
    u32 w = (GxDataBank << 18) + s * 1024 + (s >= GxPageSectors ? 0x8000 : 0);
    for(u32 n : range(1024)) { n16 v = e->ram[w + n]; out[2 * n] = v >> 8; out[2 * n + 1] = v; }
    return 1;
  }
  auto gxStart() -> void {
    gx.rom.resize(256_KiB);
    for(u32 n : range(128_KiB)) { n16 w = ram[GxScBank << 18 | n]; gx.rom[2 * n] = w >> 8; gx.rom[2 * n + 1] = w; }
    ::memset(gx.tab, 0, sizeof gx.tab);
    gx.tab[0] = gx.cfg[12]; gx.tab[1] = gx.cfg[13]; gx.tab[2] = gx.cfg[14];
    for(u32 t : range(33)) {
      u32* x = gx.tab + 3 + 6 * t; const u32* c = gx.cfg + 16 + 4 * t;
      x[0] = c[0]; x[1] = c[1]; x[2] = c[2]; x[3] = c[3]; x[4] = (c[3] & 4) ? 1 : 0; x[5] = (c[3] & 4) ? 2048 : 2352;
    }
    std::vector<u8> bram;   //(the cart: Mednafen's blank one - gotm_power(NULL); its port is an open contract item, docs/36)
    if(auto p = getenv("GOTX_BRAM")) if(FILE* f = fopen(p, "rb")) { bram.resize(2048); if(fread(bram.data(), 1, 2048, f) != 2048) bram.clear(); fclose(f); }
    if(getenv("GOTX_BRAM_GARBAGE")) { bram.resize(2048); for(u32 n : range(2048)) bram[n] = n & 1 ? 0x0f : 0xff;   //(docs/45: the console's read 0x0FFF)
      gxTrap(4, "backup RAM uninitialised (GOTX_BRAM_GARBAGE), as the cart's sram"); }
    else if(bram.empty()) gxTrap(4, "backup RAM: Mednafen's formatted blank one - the cart's is uninitialised sram (GOTX_BRAM_GARBAGE=1 models it)");
    if(!gotm_power(gx.rom.data(), bram.empty() ? nullptr : bram.data(), (const s32*)gx.tab, gxSector, this)) {
      print("[EverDrive PRO] gotx: the disc's table from the load port was not taken\n"); return;
    }
    gxMasks.clear();
    if(auto p = getenv("GOTX_MASKS")) if(FILE* f = fopen(p, "rb")) { u16 m; while(fread(&m, 2, 1, f) == 1) gxMasks.push_back(m); fclose(f);
      print("[EverDrive PRO] gotx: the pad from ", p, " (", gxMasks.size(), " frames)\n"); }
    if(auto p = getenv("GOTX_HASHES")) gxHashes = fopen(p, "w");
    gotm_on_frame(gxFrame, this);
    gx.want0 = gotm_drive_next();
    if(gxMasks.empty()) gotm_pad_now(gx.pad);
    gx.on = true;
    print("[EverDrive PRO] gotx: the engine started (tracks ", gx.tab[0], "-", gx.tab[1], ", lead-out ", (s32)gx.tab[2], ")\n");
  }
  //bank 26 (docs/36 §2, docs/38): a read; false = map_ssf answers (the bank's memory) - before the key everything is his
  auto gxRead(u32 off, n16& value) -> bool {
    if(off >= 0x48000 && off < 0x49000) {   //(map_got: 0 until the start)
      gxPadReads++;
      if(gx.on) { u8 p = off >> 4;
        if(p & 0x04) { gxTrap(3, "SELECT asked by the SH2: the cart never gives it (RUN + SELECT is the PC Engine's reset; step 16+)"); p &= 0xfb; }
        if(p != gx.pad && getenv("GOTX_TRACE")) print("[gotx] pad ", hex(p, 2L), " at machine frame ", gotm_frame(), "\n");
        gx.pad = p; if(gxMasks.empty()) gotm_pad_now(gx.pad); }
      return false;
    }
    if(!gx.armed) return false;
    if(off >= 0x40000 && off < 0x40100) {
      dxSync();
      if(off >> 1 == 0x40080 >> 1 && !gx.on) gxStart();
      u32 want = gx.on ? (u32)gotm_drive_next() : 0, a = gx.on ? gotm_audio() : 0;
      if(off & 8) value = (n16)a;
      else if(off & 4) value = off & 2 ? (a >> 31 ? 0x8000 : 0) | (a >> 24 & 3) << 13 | (a >> 16 & 0x1f) : 0;   //(word 2: the load port's busy bit: never)
      else if(off & 2) value = want >> 16 & 0x1f;
      else value = (n16)want;
      return true;
    }
    if(off >= 0x22000) return false;            //(map_got's vw_ce: + 0x00000-0x1ffff, + 0x20000-0x21fff)
    if(!gx.on) { value = 0; return true; }
    dxSync();
    u32 w = off >> 1;
    if(w < 0x10000) value = gotm_vram()[w];
    else if((w -= 0x10000) < 0x400) value = gotm_stamps()[w];
    else if(w < 0x600) value = gotm_colors()[w - 0x400] & 0x1ff;
    else if(w < 0x700) value = gotm_sat()[w - 0x600];
    else if(w < 0x705) value = gotm_snapshot()[w - 0x700];
    else if(w >= 0x800) value = gotm_lines()[(w & 0xff) * 2 + (w >> 8 & 1)] & (w & 0x100 ? 0x1ff : 0x3ff);
    else value = 0;
    return true;
  }
  auto gxWrite(u32 off, n1 upper, n1 lower, n16 data) -> void {
    if(off < 0x41000 || off >= 0x41400) return;   //the load port (the bank's memory written too, as on the cart)
    if(!(off & 2)) gx.cfgHi = data;
    else if((off >> 2 & 0xff) == 0xff) {   //'GTON', or 'GTAR' (GoT docs/41: the core presses RUN itself)
      gx.armed = (gx.cfgHi << 16 | data) == 0x47544f4e || (gx.cfgHi << 16 | data) == 0x47544152;
      gx.autorun = (gx.cfgHi << 16 | data) == 0x47544152;
    }
    else gx.cfg[off >> 2 & 0xff] = gx.cfgHi << 16 | data;
  }

  //The CD32X cart compositor (CD32X-BootCamp cartfx/; this fpga-cartfx branch, not main): the configurations 'cartfx-hur',
  //'cartfx-oj', 'cartfx-stx' - Hurrican, Jazz REV, SuperTux REV (the .rbf's SHA-256 above, or a mega-pro.rbf.ares
  //naming one). krikzz's map_ssf until the key; then SH2 reads of cart 0x3E0000-0x3FFFFF are the engine's, answered by
  //its reference model (board/cartfx/cartfx.c: the C the engine was checked against in Verilator, word for word).
  //Its time: a GO keeps the engine busy for the model's estimate of cfx_core.sv (cfx_t est) x CARTFX_SPEED percent
  //(120 by default: the console's own cart reads slow it), and the words sent meanwhile wait in the cart's queue
  //(1,024 words, FULL from 768), so SHOW takes effect when the cart's would. Traps ("[cartfx TRAP]"): words lost to a
  //full queue; the 68000 writing the cart while the engine composes (it reads the PSRAM then). Its SRAM starts with a
  //pattern, as the cart's starts with garbage. CARTFX_TRACE=1 prints each GO. Not modelled: the save state, the
  //engine's accesses done again (counters 5 and 6 stay 0). A green run here is never a console pass.
  bool cfxOn = false;
  cfx_t cfx = {};
  std::vector<uint16_t> cfxSram;
  u16 cfxQ[1024] = {};           //the cart's queue while the engine is busy
  u32 cfxQr = 0, cfxQn = 0;
  u64 cfxBusy = 0;               //the engine is busy until this time (cfxNow(): nanoseconds)
  u64 cfxLost = 0;
  bool cfxSaid = false;
  auto cfxTrap(u32 kind, const string& what) -> void { coreTrap(TrapCartfx, kind, what); }
  auto cfxNow() -> u64 {   //nanoseconds since power-on
    u64 c = cpu.active() ? cpu.clock() : m32x.shm.active() ? m32x.shm.clock() : m32x.shs.active() ? m32x.shs.clock() : cpu.clock();
    return c / Scheduler::NanoUnits + scheduler.reducedNs();
  }
  static auto cfxPsram(void* ctx, uint32_t a) -> uint8_t {   //the engine's PSRAM: the cart's 16 MB, byte address
    auto& self = *(EverDrivePro*)ctx;
    n16 w = self.ram[(a & 0xffffff) >> 1];
    return a & 1 ? (u8)w.byte(0) : (u8)w.byte(1);
  }
  static auto cfxPercent() -> u64 {
    static u64 p = [] { auto e = getenv("CARTFX_SPEED"); return e && atoi(e) > 0 ? (u64)atoi(e) : (u64)120; }();
    return p;
  }
  auto cfxReset() -> void {
    u16 key1 = rbfModel == "cartfx-oj" ? CFX_KEY1_OJ : rbfModel == "cartfx-stx" ? CFX_KEY1_STX : CFX_KEY1_HUR;
    u8 id = rbfModel == "cartfx-oj" ? CFX_ID_OJ : rbfModel == "cartfx-stx" ? CFX_ID_STX : CFX_ID_HUR;
    cfx.psram = &EverDrivePro::cfxPsram; cfx.sram = cfxSram.data(); cfx.ctx = this;
    cfx_reset(&cfx, key1, id);
    cfxQr = cfxQn = 0; cfxBusy = 0; cfxLost = 0; cfxSaid = false;
  }
  //a word into the engine, at the time it takes it: a GO keeps it busy for its estimate from then
  auto cfxFeed(u16 w, u64 start) -> void {
    u16 frames = cfx.cnt[1];
    uint16_t v;
    cfx_read(&cfx, CFX_CMD_BASE + 2u * w, &v);
    if(cfx.cnt[1] != frames) {
      u64 clocks = (u64)cfx.est * cfxPercent() / 100;
      cfx.cnt[4] = (u16)min<u64>(clocks / 64, 0xffff);
      cfxBusy = start + clocks * 20;   //(50 MHz: 20 ns a clock)
      if(getenv("CARTFX_TRACE")) print("[cartfx] GO ", cfx.cnt[1], ": ", cfx.cnt[2], " cells, ", cfx.cnt[3], " sprites, busy ",
        (u32)(clocks / 50), " us from ", start / 1000, " us (now ", cfxNow() / 1000, ")\n");
    }
  }
  //the words the queue holds go in once the engine is free, each when the one before it is done
  auto cfxDrain(u64 now) -> void {
    while(cfxQn && cfxBusy <= now && cfx.on) {
      u16 w = cfxQ[cfxQr]; cfxQr = (cfxQr + 1) & 1023; cfxQn--;
      cfxFeed(w, cfxBusy);
    }
    if(!cfx.on) cfxQn = 0;
  }
  //an SH2 read of cart 0x3E0000-0x3FFFFF: true when the engine answers it
  auto cfxRead(u32 a, n16& value) -> bool {
    a &= 0x3ffffe;
    uint16_t v;
    if(!cfx.on) {   //off: the mapper answers; the engine watches for its key
      if(!cfx_read(&cfx, a, &v)) return false;
      cfxQr = cfxQn = 0; cfxBusy = 0;
      if(!cfxSaid) { cfxSaid = true; print("[EverDrive PRO] cartfx: the key - the engine is on (ID ", hex(cfx.id, 2L), ")\n"); }
      value = v; return true;
    }
    u64 now = cfxNow();
    cfxDrain(now);
    if(!cfx.on) return false;
    if(a < CFX_CMD_BASE) { cfx_read(&cfx, a, &v); value = v; return true; }   //the shown picture
    u16 w = (a - CFX_CMD_BASE) >> 1;
    if(w >= 0x2000) {
      if(!cfxQn && cfxBusy <= now) cfxFeed(w, now);
      else if(cfxQn == 1024) { cfx.cnt[7] = (u16)++cfxLost; cfxTrap(0, {"a word lost: the cart's queue was full (1,024 words) - wait while the status says FULL (bit 1)"}); }
      else { cfxQ[(cfxQr + cfxQn) & 1023] = w; cfxQn++; }
      if(!cfx.on) { print("[EverDrive PRO] cartfx: OFF - the mapper's again\n"); cfxSaid = false; value = (u16)(cfx.id << 8); return true; }
    }
    if(w >= 1 && w < 16) { cfx.cnt[8] = CFX_KEY2; value = cfx.cnt[w]; return true; }
    v = (uint16_t)(cfx.id << 8 | (cfx.shown ? CFX_ST_SHOWN : 0) | (cfx.err ? CFX_ST_ERR : 0));
    if(cfxQn || cfxBusy > now) v |= CFX_ST_BUSY;
    if(getenv("CARTFX_TRACE") && (v & CFX_ST_BUSY) && cfxBusy > now + 100'000'000) print("[cartfx] busy for more than 100 ms\n");
    if(cfxQn >= 768) v |= CFX_ST_FULL;
    value = v;
    return true;
  }

  auto load() -> void override {
    identifyRbf();
    cfxOn = rbfModel.beginsWith("cartfx-");
    if(cfxOn) {
      cfxSram.resize(0x40000);
      for(u32 n : range(0x40000)) cfxSram[n] = (u16)(n * 0x9e37 + 0x5a5a);
      cfxReset();
    }
    dracx = rbfModel == "dracx";
    gotx = rbfModel == "gotx";
    ram.allocate(16_MiB >> 1, 0xffff);
    //the EverDrive's RAM is not blank: it keeps whatever was there (other games, the menu).  A fixed
    //pattern instead of 0xffff, so a program that reads memory it never wrote shows it at once (an AND
    //with 0xffff hid the cartridge boot's Word RAM bug, 25 Oct 2026)
    for(u32 address : range(ram.size())) ram.write(address, (n16)(address * 0x9e37 + 0x5a5a));
    ramCart = pak->attribute("ramcart").boolean();
    cdBoot  = ramCart && !pak->attribute("bootable").boolean();
    writeProtect = pak->attribute("writeProtect").boolean();

    if(ramCart) {
      if(auto fp = pak->read("ram.cart")) {
        u32 words = min((u32)(fp->size() >> 1), ram.size());
        for(u32 address : range(words)) ram.write(address, fp->readm(2L));
      }
    }

    program.clear();
    if(auto fp = pak->read("program.rom")) {
      program.resize(min((u32)fp->size(), (u32)16_MiB));
      fp->read({program.data(), program.size()});
      if(ramCart) wipePSRAM();  //a launcher: the EverDrive menu ran before it
      putProgram();
    }

    if(!ramCart) {
      if(auto fp = pak->read("save.ram")) {
        u32 words = min((u32)(fp->size() >> 1), 0x40000u);
        for(u32 address : range(words)) ram.write(SaveBank << 18 | address, fp->readm(2L));
      }
    }
  }

  auto save() -> void override {
    if(ramCart) {
      if(auto fp = pak->write("ram.cart")) {
        fp->seek(0);
        u32 words = min((u32)(fp->size() >> 1), ram.size());
        for(u32 address : range(words)) fp->writem(ram[address], 2L);
      }
    } else if(auto fp = pak->write("save.ram")) {
      fp->seek(0);
      u32 words = min((u32)(fp->size() >> 1), 0x40000u);
      for(u32 address : range(words)) fp->writem(ram[SaveBank << 18 | address], 2L);
    }
  }

  auto answersDuringRV() -> bool override { return true; }  //garbage, not held off (see the top)

  //the EverDrive menu wipes the PSRAM (banks 0-29) before it starts a ROM (see the top): the pattern
  auto wipePSRAM() -> void {
    for(u32 address : range(30u << 18)) ram.write(address, (n16)(address * 0x9e37 + 0x5a5a));
  }

  //the ROM into the start of the memory, as the EverDrive loads a game from its SD card
  auto putProgram() -> void {
    for(u32 address = 0; address + 1 < program.size(); address += 2) {
      ram.write(address >> 1, program[address] << 8 | program[address + 1]);
    }
  }

  //A1-A21: the cart sees the same 4 MB whether it is reached at 0x000000 (cartridge boot),
  //0x400000 (a Mega CD booted its disc) or through the 32X (0x880000, 0x900000, the SH2's 0x02000000)
  auto offset(n24 address) const -> u32 {
    n22 local = address;
    return (u32)bank[local >> 19] << 19 | (u32)(n19)local;
  }

  auto read(n1 upper, n1 lower, n24 address, n16 data) -> n16 override {
    u32 word = offset(address) >> 1;
    if(gotx) {
      u32 off = (word & 0x3ffff) << 1; n16 v;
      if(word >> 18 == GxBank && gxRead(off, v)) return v;
      if(gx.on && word >> 18 >= 16 && word >> 18 <= 30 && word >> 18 != GxBank) {   //(docs/44-45: chip 1 the engine's once started)
        gxTrap(0, {"the console read PSRAM chip 1 (bank ", word >> 18, ", +", hex((word & 0x3ffff) << 1, 5L), ") after the start: on the cart the engine owns it - read as 0xFFFF"});
        return 0xffff;
      }
    }
    if(dracx && dx.armed) {   //(DraculaX docs/124, the core's step 21: map_ssf's until the key, as the cores that work on the console)
      u32 b = word >> 18, off = (word & 0x3ffff) << 1; n16 v;
      if(b == bank[7] && dxRead7(off, v)) return v;
      if(b == bank[6] && dxRead6(off, v)) return v;
      //(DraculaX docs/171: map_ssf_dx gives chip 1 - banks 16-30 - to the engine once it is started: the System
      //Card is read there.  The console's reads of those banks then get whatever the engine is reading: not theirs)
      if(dx.on && b >= 16 && b <= 30) {
        dxTrap(0, {"the console read PSRAM chip 1 (bank ", b, ", +", hex(off, 5L), ") after the start: on the cart the engine owns it - read as the engine's bus (0xDEAD ^ word here)"});
        return (n16)(0xdead ^ word);
      }
    }
    if(cfxOn && !cpu.active() && (u32)(n22)address >= 0x3e0000) {   //(fpga-cartfx: the compositor's window, once keyed)
      n16 v;
      if(cfxRead((u32)(n22)address, v)) return v;
    }
    if(!cpu.active()) {  //an SH2 (the 68000 reads through Bus / the 32X wrapper on its own thread)
      u64 now = m32x.shm.active() ? m32x.shm.clock() : m32x.shs.clock();
      sh2Word = word;
      sh2Clock = now;
      if(distLast && now + Window >= distFirst && now <= distLast + Window) {  //inside a burst
        corrupt(word, distData);
        return 0;
      }
    }
    return ram[word];
  }

  auto write(n1 upper, n1 lower, n24 address, n16 data) -> void override {
    if(gotx && offset(address) >> 19 == GxBank) gxWrite((offset(address) & 0x7ffff), upper, lower, data);   //(map_got takes any write there, docs/36)
    if(gotx && gx.on && offset(address) >> 19 >= 16 && offset(address) >> 19 <= 30 && offset(address) >> 19 != GxBank) {
      gxTrap(1, {"the console wrote PSRAM chip 1 (bank ", offset(address) >> 19, ") after the start: on the cart the engine owns it - write lost"});
      return;
    }
    if(dracx && dx.on) {   //(DraculaX docs/171: chip 1 the engine's once started; its windows 6 and 7 still the core's)
      u32 b = offset(address) >> 19;
      if(b >= 16 && b <= 30 && b != bank[6] && b != bank[7]) {
        dxTrap(1, {"the console wrote PSRAM chip 1 (bank ", b, ") after the start: on the cart the engine owns it - write lost"});
        return;
      }
    }
    if(cfxOn && cfx.on && cpu.active()) {   //(fpga-cartfx: the engine reads the PSRAM while it composes)
      u64 now = cfxNow();
      cfxDrain(now);
      if(cfxQn || cfxBusy > now)
        cfxTrap(1, {"the 68000 wrote the cart (bank ", offset(address) >> 19, ") while the engine composes - it reads the PSRAM then: no GO before a load ends"});
    }
    if(!writeEnable || writeProtect) return;
    u32 word = offset(address) >> 1;
    if(dracx && word >> 18 == bank[7]) dxWrite7((word & 0x3ffff) << 1, upper, lower, data);   //(the bank's memory too)
    if(upper) ram[word].byte(1) = data.byte(1);
    if(lower) ram[word].byte(0) = data.byte(0);
    u32 local = (n22)address;
    invalidate(local & ~1u, 2);
  }

  auto corrupt(u32 word, n16 data) -> void {
    if(ram[word] == data) return;
    ram[word] = data;
    for(u32 n : range(8)) if(bank[n] == word >> 18) invalidate(n << 19 | (word << 1 & 0x7ffff), 2);
  }

  //a 68000 write that went elsewhere (RAM, the VDP, the Mega CD, the 32X) while W = 1: see the top
  auto disturb(n24 address, n16 data) -> void override {
    if(!writeEnable || writeProtect) return;
    if(!Mega32X() || !m32x.io.adapterEnable) return;  //seen with the 32X on; without it: not measured
    if(m32x.dreq.vram) return;  //RV = 1: the 32X holds the SH2s off the cart, so nothing is disturbed
    u64 now = cpu.clock();
    if(!distLast || !near(now, distLast)) distFirst = now;
    distLast = now;
    distData = data;
    if(sh2Clock && near(now, sh2Clock)) corrupt(sh2Word, data);  //the SH2 was on the cart just now
  }

  auto readIO(n1 upper, n1 lower, n24 address, n16 data) -> n16 override {
    return data;  //the registers are write-only
  }

  auto writeIO(n1 upper, n1 lower, n24 address, n16 data) -> void override {
    if(address < 0xa130f0 || address > 0xa130ff) return;
    u32 n = address >> 1 & 7;
    if(n == 0) {
      if(!data.bit(15)) return;  //P must be set
      mode32x     = data.bit(14);
      writeEnable = data.bit(13);
      led         = data.bit(12);
      setCart(data.bit(11));
      setBank(0, data.bit(0,4));
      return;
    }
    //the bank is on D0-D4: a word write, or a byte write to the odd address (an even-byte write
    //carries the same byte on D0-D7, but whether the EverDrive takes it is not known)
    if(!lower) return;
    setBank(n, data.bit(0,4));
  }

  auto setBank(u32 n, n5 value) -> void {
    if(bank[n] == value) return;
    bank[n] = value;
    invalidate(n << 19, 0x80000);
  }

  auto setCart(n1 value) -> void {
    cartOff = value;
    cartridge->cartOff = value;  //Cartridge::bootable() follows it at once
  }

  //code the SH2 recompiler built from this window is stale now (the cache-through mirror is never
  //purged by a game, so it has to be dropped here; the cached mirror too, to be safe)
  auto invalidate(u32 local, u32 size) -> void {
    if constexpr(SH2::Accuracy::Recompiler) {
      if(!Mega32X()) return;
      for(u32 address = local & ~0xff; address < local + size; address += 0x100) {
        u32 first = max(address, local), last = min(address + 0x100, local + size);
        u8 bytes = min(last - first, 0xffu);  //one 256-byte pool at a time; 255 covers all of it
        for(u32 mirror : {0x0200'0000u, 0x2200'0000u}) {
          m32x.shm.recompiler.invalidate(mirror | first, bytes);
          m32x.shs.recompiler.invalidate(mirror | first, bytes);
        }
      }
    }
  }

  auto power(bool reset) -> void override {
    if(cfxOn) cfxReset();   //(fpga-cartfx: off until the key)
    dx = {};   //(dracx: the engine at its reset until the start read)
    gx = {};   //(gotx: the same; off at the console's reset too, docs/36)
    for(u32 n : range(8)) bank[n] = n;
    mode32x = 0;
    led = 0;
    if(cdBoot) {
      writeEnable = 1;
      setCart(1);
    } else {
      writeEnable = 0;
      setCart(0);
      if(reset && ramCart && !program.empty()) { wipePSRAM(); putProgram(); }  //launcher: started again from the menu
    }
  }

  auto serialize(serializer& s) -> void override {
    s(ram);
    s(bank);
    s(mode32x);
    s(writeEnable);
    s(led);
    s(cartOff);
    s(sh2Word);
    s(sh2Clock);
    s(distFirst);
    s(distLast);
    s(distData);
    if(s.reading()) cartridge->cartOff = cartOff;
  }
};
