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
  };
  static constexpr const char* rbfModels[] = {"ssf", "dracx"};    //the models this board has (more as cores are added)

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
    u8 pad = 0;                             //the pad (Mednafen's order), before and after the start
    u32 tab[3 + 6 * 101] = {};              //the disc's table for the machine, from the load port
    std::vector<u8> rom;                    //the System Card, from bank 16 at the start
  } dx;
  static constexpr u32 MasterClock = 21'477'270;   //the PC Engine's (NTSC 6 x 3.579545 MHz)

  auto frequency() -> u32 override { return dracx ? MasterClock : Interface::frequency(); }

  //the cart's thread: the machine to its own time, a few hundred master clocks at a time; while it waits for a
  //sector (dxm_waiting), its time stands still and only the thread's goes on
  auto main() -> void override {
    if(!dracx) return Interface::main();
    if(!dx.on) return step(256);
    if(getenv("DRACX_TRACE")) {   //(a line about every second of the cart's thread: the machine's state)
      static u64 n; if(++n % 83'000 == 0) print("[dracx] machine frame ", dxm_frame(), " time ", (s64)dxm_now(), " waiting ", dxm_waiting(),
        " drive next ", dxm_drive_next(), " spare ", dx.full ? (s32)dx.have : -1, " missed ", dxm_missed(), "\n");
    }
    s64 t0 = dxm_now(), t = dxm_run_until(t0 + 256);
    step(dxm_waiting() || t <= t0 ? 256 : (u32)(t - t0));
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
    if(!dxm_power(dx.rom.data(), nullptr, (const s32*)dx.tab, dxSector, this)) {
      print("[EverDrive PRO] dracx: the disc's table from the load port was not taken\n"); return;
    }
    dxm_feed(dxHave);
    dxm_pad_now(dx.pad);
    dx.on = true;
    print("[EverDrive PRO] dracx: the engine started (tracks ", dx.tab[0], "-", dx.tab[1], ", lead-out ", (s32)dx.tab[2], ")\n");
  }

  //window 7's bank (docs/100 §2): what a read there does and answers (false: the bank's own memory answers)
  auto dxRead7(u32 off, n16& value) -> bool {
    if(off < 0x10000) { dx.lbl = dx.lblHi << 12 | (off >> 4 & 0xfff); dx.full = false; return false; }   //label, low
    if(off >= 0x10000 && off < 0x12000) { dx.lblHi = off >> 4 & 0x1ff; return false; }                  //label, high
    if(off >= 0x48000 && off < 0x49000) { dx.pad = off >> 4; if(dx.on) dxm_pad_now(dx.pad); return false; }
    if(off >= 0x40000 && off < 0x40100) {                                                                //the registers
      dxSync();
      if(off >> 1 == 0x40080 >> 1 && !dx.on && dx.armed) dxStart();   //(not before: the 32X's own boot reads the cart there)
      u32 want = dx.on ? (u32)dxm_drive_next() : 0;
      u32 a = dx.on ? dxm_audio() : 0;
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
      if(lower) { dx.sec[b + 1] = data; if(b == 0x7fe) { dx.full = true; dx.have = dx.lbl; } }
    } else if(off >= 0x41000 && off < 0x41400) {                                //the load port
      if(!(off & 2)) dx.cfgHi = data;
      else if((off >> 2 & 0xff) == 0xff) dx.armed = (dx.cfgHi << 16 | data) == 0x44584f4e;   //the key, not the CD unit's
      else dx.cfg[off >> 2 & 0xff] = dx.cfgHi << 16 | data;
    }
  }
  //window 6's bank: the video window (docs/96 §5-6) - VRAM's words, then the stamps, the colours, the sprite table,
  //the frame's snapshot
  auto dxRead6(u32 off, n16& value) -> bool {
    if(off >= 0x21000) return false;            //(map_dx's vw_ce: + 0x00000-0x1ffff, + 0x20000-0x20fff)
    if(!dx.on) { value = 0; return true; }
    dxSync();
    u32 w = off >> 1;
    if(w < 0x10000) value = dxm_vram()[w];
    else if((w -= 0x10000) < 0x400) value = dxm_stamps()[w];
    else if(w < 0x600) value = dxm_colors()[w - 0x400] & 0x1ff;
    else if(w < 0x700) value = dxm_sat()[w - 0x600];
    else if(w < 0x705) value = dxm_snapshot()[w - 0x700];
    else value = 0;
    return true;
  }

  auto load() -> void override {
    identifyRbf();
    dracx = rbfModel == "dracx";
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
    if(dracx) {
      u32 b = word >> 18, off = (word & 0x3ffff) << 1; n16 v;
      if(b == bank[7] && dxRead7(off, v)) return v;
      if(b == bank[6] && dxRead6(off, v)) return v;
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
    dx = {};   //(dracx: the engine at its reset until the start read)
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
