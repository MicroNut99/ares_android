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

//The cart geometry engine of Jumping Flash CD32X (a custom mega-pro.rbf on the console: krikzz's SSF
//mapper plus the engine): the rbf model "jfgeo" (a mega-pro.rbf.ares sidecar saying jfgeo), or ARES_JFGEO=1.  The engine is JumpingFlash_CD32X's reference model,
//fpga/jfgeo/jfgeo.c (copied into jfgeo/; keep the two the same): window 0's fourth 512 KB, cart
//0x180000-0x1fffff, is the engine whatever bank is switched there; jfgeo.h has the register map.
//Timing: a GO makes the frame at once (into the buffer the SH2s are not reading) and the engine
//reports busy for an estimate of the FPGA's time at 50 MHz, from the Verilog in Verilator with the
//console on the cart (fpga/jfgeo/sim/tb_cart.cpp, World 1-1's start view 554k clocks): 12 clocks a
//PSRAM read, 6 an SRAM write, 40 a vertex, 8 a triangle.  Not checked on a console.
namespace JfGeo {
#include "jfgeo/jfgeo.c"
}

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
    //MicroNut99/JumpingFlash_CD32X fpga/jfgeo (map_ssf + the cart geometry engine) at 4afaa87, Quartus 25.1std
    //Lite on the owner's PC, 7 Nov 2026 (a rebuild of other sources needs its "jfgeo" sidecar)
    {"aeaddfec1c2dfa70d7ea523450d6f796c0142cecabb441a895a6d10d83ba31d0", "Jumping Flash CD32X core (SSF + geometry engine)", "jfgeo"},
  };
  static constexpr const char* rbfModels[] = {"ssf", "jfgeo"};    //the models this board has (more as cores are added)

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

  //the cart geometry engine (see JfGeo above)
  boolean geoOn;
  u16 geoParam[JFGEO_NPARAM] = {};
  u32 geoIndex = 0;
  u8 geoRam[0x40000] = {};       //the engine's SRAM as the window shows it: buffer 0 at 0, buffer 1 at 0x20000
  u64 geoBusy = 0;               //Thread::clock units of engine time left
  u64 geoLast = 0;               //the clock at the last access (the scheduler lowers all clocks now and then,
                                 //so the time is counted in steps between accesses, a step back = none)
  u8 geoFrames = 0;
  static auto geoRead(void* ctx, u32 a) -> u16 {
    auto self = (EverDrivePro*)ctx;
    return self->ram[(a >> 1) & 0x7fffff];
  }
  auto geoNow() -> u64 {
    if(!cpu.active()) return m32x.shm.active() ? m32x.shm.clock() : m32x.shs.clock();
    return cpu.clock();
  }
  auto geoTick() -> void {
    u64 now = geoNow();
    if(now > geoLast) { u64 d = now - geoLast; geoBusy = geoBusy > d ? geoBusy - d : 0; }
    geoLast = now;
  }
  auto geoStatus() -> n16 {
    return (geoBusy ? 1 : 0) | geoFrames << 8;
  }
  auto geoAccess(u32 local) -> n16 {
    geoTick();
    if(local < 0x1c0000) {
      u32 a = local - 0x180000;
      return geoRam[a] << 8 | geoRam[a + 1];
    }
    if(local < 0x1e0000) {
      if(geoIndex < JFGEO_NPARAM) geoParam[geoIndex++] = (local - 0x1c0000) >> 1;
      return geoStatus();
    }
    if(local == 0x1e0000) { geoIndex = 0; return geoStatus(); }
    if(local == 0x1e0002) {
      JfGeo::JfGeoStats st;
      u8* out = geoRam + (geoParam[15] & 1) * 0x20000;
      JfGeo::jfgeo_frame(geoRead, this, geoParam, out, &st);
      u64 clocks = (u64)st.mem_reads * 12 + (u64)st.mem_writes * 6 + (u64)st.vertices * 40 + (u64)st.triangles * 8;
      geoBusy = (u64)((u128)Thread::Second * clocks / 50'000'000);
      geoFrames++;
      if(auto v = getenv("ARES_JFGEO_LOG"); v && v[0] == '1')
        print("jfgeo: frame ", geoFrames, " objects ", st.drawn_objects, " vertices ", st.vertices,
              " written ", st.written, " clocks ", clocks, "\n");
      return geoStatus();
    }
    return geoStatus();
  }

  auto load() -> void override {
    identifyRbf();
    ram.allocate(16_MiB >> 1, 0xffff);
    //the EverDrive's RAM is not blank: it keeps whatever was there (other games, the menu).  A fixed
    //pattern instead of 0xffff, so a program that reads memory it never wrote shows it at once (an AND
    //with 0xffff hid the cartridge boot's Word RAM bug, 25 Oct 2026)
    for(u32 address : range(ram.size())) ram.write(address, (n16)(address * 0x9e37 + 0x5a5a));
    ramCart = pak->attribute("ramcart").boolean();
    //Jumping Flash's engine: on when the mega-pro.rbf beside the launcher is its core (a "jfgeo" sidecar,
    //fpga/jfgeo/quartus/build_jfgeo.bat writes it), as on the console; ARES_JFGEO=1 / 0 forces it on / off
    geoOn = rbfModel == "jfgeo";
    if(auto v = getenv("ARES_JFGEO"); v && v[0]) geoOn = v[0] == '1';
    if(geoOn) print("[EverDrive PRO] Jumping Flash cart geometry engine (jfgeo) in window 3\n");
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
    if(geoOn) { u32 local = (n22)address; if(local >= 0x180000 && local < 0x200000) return geoAccess(local & ~1u); }
    u32 word = offset(address) >> 1;
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
    s(geoParam);
    s(geoIndex);
    s(geoRam);
    s(geoBusy);
    s(geoLast);
    s(geoFrames);
    if(s.reading()) cartridge->cartOff = cartOff;
  }
};
