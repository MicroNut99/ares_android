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
  string rbfParams;       //the sidecar's whole line (the model's parameters)
  string rbfModel;        //"" (no file: the built-in SSF mapper), "ssf", or what it was found to be

  //"neo68k" (Metal Slug CD32X docs/14, fpga/rtl/neo_core.sv): the Neo Geo's 68000 on the cart, running SNK's
  //program for the SH2s, which only draw.  ares's 68000 as the cart's thread at 12 MHz; around it the Neo Geo's
  //memory map as neo_core.sv has it (the port's recomp/ng.c, lockstep-checked against FBNeo), the I/O ports as the
  //32X port's ng_game.c answers them.  Its memories: work RAM, VRAM, palette, backup RAM inside (the FPGA's own
  //SRAM); the program ROM (2 MB), the BIOS (128 KB) and the boot block (make_boot.py) in the image's banks, named in
  //mega-pro.rbf.ares ("neo68k prog=<bank> bios=<bank> boot=<bank> window=<bank>").  Reset: the vectors and the
  //state from the boot block (0xE00000) until it writes 0xEFFFFE.
  //The SH2s' window (bank `window`, never 31; protocol v2, Metal Slug docs/14): reads of 0x00000-0x12fff and
  //0x13000-0x25fff are the two frame buffers (VRAM 0x11000, then the palette bank in use 0x2000), copied at each
  //vblank into the one the SH2s have not taken; 0x30000-0x3ffff the work RAM, live; 0x40000-0x7ffff commands - a
  //read of word k = (offset - 0x40000) / 2 is command k >> 12 with the value k & 0xfff, the word read back its answer:
  //  0 status: the frames run | the latest buffer in bit 15      1 pad 1 (0x300000)      2 pad 2 (0x340000)
  //  3 start / select (0x380000)      4 coins (0x320001)      5 go: the frame copied, the vblank interrupt raised
  //  6 the next sound command (0x8000 | it; 0 none)      7 control (bit 0 run, bit 1 reset)
  //  8 / 9 a work RAM address, its low 12 / high 4 bits      10 the byte written there
  //  11 take: the latest frame buffer is the SH2s' until the next take - answers its number (0 / 1) | the frame << 1
  struct Neo68k : M68000 {
    EverDrivePro& ed;
    Neo68k(EverDrivePro& e) : ed(e) {}
    u32 progBank = 20, biosBank = 24, bootBank = 25, window = 27;
    //the packed form (Metal Slug's image, docs/14): the program ROM and the BIOS as make_m3_data.py part 0 packs
    //them, read through its page table (part 1); the boot block and the window by byte offset / cart address
    bool packed = false;
    u32 romOff = 0, tableOff = 0, spillOff = 0, bootOff = ~0u, windowAddr = 0;   //(windowAddr 0: the bank `window`)
    u32 protocol = 2;                                  //3: the window is 0x38000 bytes (docs/14)
    u8  wram[0x10000], pal[2][0x2000], nv[0x10000];
    u16 vram[0x8800], snapVram[3][0x8800];
    u8  snapPal2[3][0x2000];
    u32 latest = 0, taken = 1, snapFrame[3] = {0, 0, 0}, snapSpr[3] = {0, 0, 0}, pokeAddr = 0;
    //three frame buffers (neo_cart.sv r18, 10 Nov 2026): a go copies into the one that is neither the SH2s' (taken) nor
    //the latest; the window's two slots show a physical buffer each (slotBuf), a take puts the latest into the slot the
    //SH2s are not reading and answers that slot. ARES_NEO68K_BUFFERS=2: r17's two (a copy into ~taken, the slot = it)
    u32 slot = 1, slotBuf[2] = {0, 1};
    auto buffers() -> u32 { static u32 n = 0; if(!n) { auto e = getenv("ARES_NEO68K_BUFFERS"); n = e && atoi(e) == 2 ? 2 : 3; } return n; }
    //the go's copy takes time, as neo_cart.sv's does (38,912 words, ~3 ms; console, Metal Slug's USB log 10 Nov 2026:
    //a take during it was handed the buffer still being written). The words land in order over copyLen; latest
    //changes when the copy ends; command 13 shows it (cst, bit 10). ARES_NEO68K_COPY_US=<us> (default 3000; 0: instant)
    u16 copyVram[0x8800]; u8 copyPal[0x2000];
    bool copying = false; u32 copyBuf = 0, copyDone = 0; u64 copyStart = 0, copyLen = 0;
    static constexpr u32 CopyWords = 0x8800 + 0x1000;
    auto clockNow() -> u64 {
      if(m32x.shm.active()) return m32x.shm.clock();
      if(m32x.shs.active()) return m32x.shs.clock();
      return cpu.clock();
    }
    auto copyTo(u32 n) -> void {                      //the words up to n written into the buffer
      for(; copyDone < n; copyDone++) {
        u32 w = copyDone;
        if(w < 0x8800) snapVram[copyBuf][w] = copyVram[w];
        else { u32 i = (w - 0x8800) * 2; snapPal2[copyBuf][i] = copyPal[i]; snapPal2[copyBuf][i + 1] = copyPal[i + 1]; }
      }
    }
    auto settle() -> void {                           //the copy as far as it has come by now
      if(!copying) return;
      u64 now = clockNow();
      u64 el = now > copyStart ? now - copyStart : 0;
      if(el >= copyLen) { copyTo(CopyWords); copying = false; latest = copyBuf; return; }
      copyTo((u32)(el * CopyWords / copyLen));
    }
    bool boot = true, biosVectors = true, palBank = false, sramWritable = false, shadow = false, vramBank = false;
    u16 vramPtr = 0, vramMod = 0, irqCtl = 0;
    bool run = false, irqPending = false;
    //(Metal Slug CD32X, 9 Nov 2026: as the console's core, fpga/rtl/neo_cart.sv - off until the key, command 7 with 3
    // then command 7 with 0: until then every read of the window is the cart's PSRAM, as map_ssf's; and the core's
    // state for the console diagnostic, commands 13-15)
    bool on = false, key1 = false;
    u8 dgGo = 0, dgDrop = 0;
    u32 frames = 0, vblanks = 0, sprFrame = 0, animTimer = 0;
    u8 p1 = 0xff, p2 = 0xff, status = 0xff, coin = 0x1f;
    s32 soundLast = -1;
    std::deque<u8> sound;

    auto psram(u32 bank, u32 offset) -> n16 { return ed.ram[(bank << 18) + (offset >> 1)]; }
    auto phys(u32 byteOffset) -> n16 { return ed.ram[(byteOffset >> 1) & 0x7fffff]; }
    auto packedRead(u32 a) -> n16 {                 //a 68000 address through the page table (program ROM, BIOS)
      u16 e = phys(tableOff + 2 * (a >> 16 & 0xff));
      if(e == 0xffff) return 0xffff;
      u32 base = e & 0x8000 ? spillOff : romOff;
      return phys(base + ((e & 0x7fff) + (a >> 12 & 15)) * 4096 + (a & 0xffe));
    }
    auto rom16(u32 a) -> n16 {                      //the program ROM: P1 at 0x000000, P2 at 0x200000
      if(packed) return packedRead(a);
      if(a < 0x100000) return psram(progBank, a);
      return psram(progBank, 0x100000 + (a & 0xfffff));
    }
    auto bios16(u32 a) -> n16 { return packed ? packedRead(0xc00000 | (a & 0x1ffff)) : psram(biosBank, a & 0x1ffff); }
    auto boot16(u32 a) -> n16 { return bootOff != ~0u ? phys(bootOff + (a & 0xffffe)) : psram(bootBank, a & 0x7ffff); }
    auto vecBios(u32 a) -> n16 { return a < 0x80 ? bios16(a) : rom16(a); }      //BIOS 0-0x7f, ROM 0x80-0x3ff
    auto vecBiosRom(u32 a) -> n16 { return a < 0x80 ? rom16(a) : bios16(a); }   //ROM 0-0x7f, BIOS above

    auto ioRead(u32 a) -> u8 {                       //ng_game.c ng_io_read, a byte
      switch(a & 0xff00ff) {
      case 0x300000: return p1;
      case 0x300001: return 0xff;
      case 0x300081: return 0xbf;
      case 0x320000: if(soundLast == 1) { soundLast = -1; return 1; } return 0;
      case 0x320001: return (coin & 0x3f) | ((vblanks >> 1) & 1) << 6;
      case 0x340000: return p2;
      case 0x380000: return status;
      }
      return 0xff;
    }
    auto lspcRead(u32 a) -> n16 {
      switch(a & 6) {
      case 0: case 2: { u32 i = (u32)vramBank * 0x8000 + (vramPtr >> 1); return i < 0x8800 ? vram[i] : (u16)0; }
      case 4: return (u16)(vramMod >> 1);
      default: return 0xf800 | (sprFrame & 7);
      }
    }
    auto read(n1 upper, n1 lower, n24 address, n16 data = 0) -> n16 override {
      u32 a = address;
      switch(a >> 20) {
      case 0x0: if(boot && a < 8) return boot16(a);
                if(biosVectors && a < 0x400) return vecBios(a);
                return rom16(a);
      case 0x2: return rom16(a);
      case 0x1: return wram[a & 0xffff] << 8 | wram[(a + 1) & 0xffff];
      case 0x3:
        if((a & 0xfe0000) == 0x3c0000) return lspcRead(a);
        return ioRead(a) << 8 | ioRead(a + 1);
      case 0x4: case 0x5: case 0x6: case 0x7: {
        u8* p = pal[(a & 0x3fe000) == 0 ? palBank : 0];
        return p[a & 0x1fff] << 8 | p[(a + 1) & 0x1fff];
      }
      case 0xc: if(biosVectors && (a & 0xfffff) < 0x400) return vecBiosRom(a & 0x3ff);
                return bios16(a);
      case 0xd: return nv[a & 0xffff] << 8 | nv[(a + 1) & 0xffff];
      case 0xe: return boot16(a);
      }
      return 0xffff;
    }
    auto vramWrite(u32 reg, u16 v) -> void {
      switch(reg & 14) {
      case 0: vramPtr = v << 1; vramBank = v >> 15; break;
      case 2: { u32 i = (u32)vramBank * 0x8000 + (vramPtr >> 1); if(i < 0x8800) vram[i] = v; vramPtr += vramMod; break; }
      case 4: vramMod = v << 1; break;
      case 6: irqCtl = v; break;
      }
    }
    auto ioWrite8(u32 a, u8 v) -> void {
      switch(a & 0xff0000) {
      case 0x320000:
        if(a & 1) break;
        if(sound.size() < 256) sound.push_back(v);
        soundLast = v;
        break;
      case 0x3a0000:
        switch(a & 0x1f) {
        case 0x01: shadow = 0; break;        case 0x11: shadow = 1; break;
        case 0x03: biosVectors = 1; break;   case 0x13: biosVectors = 0; break;
        case 0x0d: sramWritable = 0; break;  case 0x1d: sramWritable = 1; break;
        case 0x0f: palBank = 1; break;       case 0x1f: palBank = 0; break;
        }
        break;
      }
    }
    auto write(n1 upper, n1 lower, n24 address, n16 data) -> void override {
      u32 a = address;
      if((a & ~1u) == 0xeffffe) { boot = false; return; }
      switch(a >> 20) {
      case 0x1:
        if(upper) wram[a & 0xffff] = data.byte(1);
        if(lower) wram[(a + 1) & 0xffff] = data.byte(0);
        return;
      case 0x3:
        if((a & 0xfe0000) == 0x3c0000) {
          if(upper && lower) vramWrite(a, data);
          else if(upper) vramWrite(a, data.byte(1) * 0x101);
          return;
        }
        if(upper) ioWrite8(a, data.byte(1));
        if(lower) ioWrite8(a + 1, data.byte(0));
        return;
      case 0x4: case 0x5: case 0x6: case 0x7:
        if(upper) pal[palBank][a & 0x1fff] = data.byte(1);
        if(lower) pal[palBank][(a + 1) & 0x1fff] = data.byte(0);
        return;
      case 0xd:
        if(!sramWritable) return;
        if(upper) nv[a & 0xffff] = data.byte(1);
        if(lower) nv[(a + 1) & 0xffff] = data.byte(0);
        return;
      }
    }
    auto idle(u32 clocks) -> void override { ed.step(clocks); }
    auto wait(u32 clocks) -> void override { ed.step(clocks); }

    auto reset() -> void {
      memory::fill<u8>(wram, sizeof wram, 0); memory::fill<u8>(nv, sizeof nv, 0);
      memory::fill<u8>(&pal[0][0], sizeof pal, 0); memory::fill<u16>(vram, 0x8800, 0);
      boot = true; biosVectors = true; palBank = false; sramWritable = false; shadow = false;
      vramBank = false; vramPtr = 0; vramMod = 0; irqCtl = 0;
      irqPending = false; frames = 0; vblanks = 0; sprFrame = 0; animTimer = 0; soundLast = -1; sound.clear();
      copying = false; latest = 0;                   //(the core: the copier idle, latest 0)
      M68000::power();
      r.a[7] = read(1, 1, 0) << 16 | read(1, 1, 2);
      r.pc   = read(1, 1, 4) << 16 | read(1, 1, 6);
      prefetch(); prefetch();
    }
    bool resetPending = true;                         //(the reset runs on the cart's own thread: main())
    auto main() -> void {
      if(resetPending) { resetPending = false; reset(); dgGo = 0; dgDrop = 0; }
      if(!run) return ed.step(64);
      if(irqPending && r.i < 1) { irqPending = false; return interrupt(Vector::Level1, 1); }
      instructions++;
      instruction();
    }
    u32 instructions = 0;
    auto go() -> void {                               //the frame: copied for the SH2s, then the vblank
      dgGo++;
      if(boot || !run) { dgDrop++; return; }          //(still booting: the machine is not there yet)
      if(auto f = getenv("ARES_NEO68K_DUMP")) {        //<file>: the work RAM at the first 64 vblanks, 64 KB each
        static FILE* fp; static u32 n; if(!fp) fp = fopen(f, "wb");
        if(fp && n++ < 64) { fwrite(wram, 1, 0x10000, fp); fflush(fp); }
      }
      if(getenv("ARES_NEO_TRACE")) { static u32 n; if(n++ < 14) print("[neo68k] go ", frames, ": ", instructions, " instructions since, pc ", hex(r.pc, 6L), " i ", (u32)r.i, " stop ", (u32)r.stop, " pending ", (u32)irqPending, " clock ", ed.cartridge->clock(), "\n"); instructions = 0; }
      //ARES_NEO68K_CRCS=<game.crcs> (the port's PC build, host_game --crcs: the same start, the same answers to the
      //I/O ports): each vblank's VRAM (the part the 32X keeps) against it, in order
      if(auto f = getenv("ARES_NEO68K_CRCS")) {
        static std::vector<u8> d; static u32 v, bad, good;
        if(d.empty()) d = nall::file::read(f);
        if(8 * v + 8 <= d.size()) {
          u8 vb[0x6a40 * 2]; u32 k = 0;
          static const u32 from[3] = {0, 0x7000, 0x8000}, to[3] = {0x5f40, 0x7500, 0x8600};
          for(u32 r : range(3)) for(u32 w = from[r]; w < to[r]; w++) { vb[k++] = vram[w] >> 8; vb[k++] = vram[w]; }
          u32 cv = Hash::CRC32({vb, k}).value();
          u32 want = d[8 * v + 4] | d[8 * v + 5] << 8 | d[8 * v + 6] << 16 | (u32)d[8 * v + 7] << 24;
          if(cv == want) { if(++good % 300 == 0) print("[neo68k] vblank ", v, ": the VRAM as the PC build's (", good, " so far)\n"); }
          else if(bad++ < 10) {
            s32 at = -1;
            for(u32 j = 0; 8 * j + 8 <= d.size() && j < v + 400; j++)
              if((d[8 * j + 4] | d[8 * j + 5] << 8 | d[8 * j + 6] << 16 | (u32)d[8 * j + 7] << 24) == cv) { at = j; break; }
            print("[neo68k] vblank ", v, ": the VRAM differs from the PC build's (it is the PC's vblank ", at, ")\n");
          }
          v++;
        }
      }
      //ARES_NEO68K_CHECK=<run.lock> (ms_record --lock, the state the boot block was made from): each vblank's work
      //RAM, VRAM and palette against FBNeo's frame ends, in order (a check of the model, not of the game)
      if(auto lock = getenv("ARES_NEO68K_CHECK")) {
        static std::vector<std::array<u32, 4>> ends; static u32 next, bad;
        if(ends.empty()) {
          auto d = nall::file::read(lock);
          u32 p = 38 * 4 + 0x10000 + 0x11000 + 0x4000 + 0x10000 + 0x20000 + 0x400;
          auto w = [&](u32 i) { return (u32)d[p + 4 * i] | d[p + 4 * i + 1] << 8 | d[p + 4 * i + 2] << 16 | (u32)d[p + 4 * i + 3] << 24; };
          for(u32 i = 0; p + 4 * i < d.size(); ) {
            u32 t = w(i);
            if(t == 3) ends.push_back({w(i + 3), w(i + 4), w(i + 5), w(i + 6)});
            i += t == 3 ? 7 : 5;
          }
          print("[neo68k] check: ", ends.size(), " frame ends from ", lock, "\n");
        }
        if(next < ends.size()) {
          u8 vb[0x11000]; for(u32 i : range(0x8800)) { vb[2 * i] = vram[i] >> 8; vb[2 * i + 1] = vram[i]; }
          u32 cr = Hash::CRC32({wram, 0x10000}).value(), cv = Hash::CRC32({vb, 0x11000}).value(), cp = Hash::CRC32({pal[palBank], 0x2000}).value();
          auto& e = ends[next++];
          bool same = cr == e[1] && cv == e[2] && cp == e[3];
          if(!same && bad++ < 10) print("[neo68k] frame ", e[0], ": ", cr != e[1] ? "work RAM " : "", cv != e[2] ? "VRAM " : "", cp != e[3] ? "palette " : "", "differ\n");
          if(same && e[0] % 60 == 0) print("[neo68k] frame ", e[0], ": the same\n");
        }
      }
      settle();
      if(copying) { copyTo(CopyWords); copying = false; latest = copyBuf; }   //(a go during a copy: the core starts the
                                                                              // next when it ends - here at once)
      u32 b = taken ^ 1;                              //the buffer the SH2s are not drawing from (r17)
      if(buffers() == 3) {                            //r18: neither theirs nor the latest - nor, if two are free, the
        b = 3;                                        // one the other slot shows
        for(u32 p : range(3)) if(p != taken && p != latest && p != slotBuf[slot ^ 1]) { b = p; break; }
        if(b == 3) for(u32 p : range(3)) if(p != taken && p != latest) { b = p; break; }
      }
      static s64 us = -1;
      if(us < 0) { auto e = getenv("ARES_NEO68K_COPY_US"); us = e ? atoi(e) : 3000; }
      snapFrame[b] = frames; snapSpr[b] = sprFrame;
      if(us == 0) {
        memory::copy<u16>(snapVram[b], vram, 0x8800);
        memory::copy<u8>(snapPal2[b], pal[palBank], 0x2000);
        latest = b;
      } else {                                        //the state at the go (the core's writes wait meanwhile), landing
        memory::copy<u16>(copyVram, vram, 0x8800);    // word by word
        memory::copy<u8>(copyPal, pal[palBank], 0x2000);
        copying = true; copyBuf = b; copyDone = 0; copyStart = clockNow(); copyLen = Thread::Second / 1'000'000 * (u64)us;
      }
      if(!(irqCtl & 8) && animTimer++ >= (u32)(irqCtl >> 8)) { animTimer = 0; sprFrame++; }
      vblanks++; frames++;
      irqPending = true;
    }
    //a read of the SH2s' window (o: the offset in the bank)
    auto windowRead(u32 o) -> n16 {
      if(getenv("ARES_NEO_TRACE")) { static u32 n; if(n++ < 40) print("[neo68k] window read ", hex(o, 5L), " run ", (u32)run, " pc ", hex(r.pc, 6L), "\n"); }
      settle();
      if(o < 0x26000) {
        u32 b = o >= 0x13000, i = o - 0x13000 * b;
        b = buffers() == 3 ? slotBuf[b] : b;
        if(i < 0x11000) return snapVram[b][i >> 1];
        i -= 0x11000; return snapPal2[b][i] << 8 | snapPal2[b][i + 1];
      }
      u32 ramAt = protocol == 3 ? 0x26000 : 0x30000, cmdAt = protocol == 3 ? 0x36000 : 0x40000;
      if(o >= ramAt && o < ramAt + 0x10000) { u32 i = o - ramAt; return wram[i] << 8 | wram[(i + 1) & 0xffff]; }
      if(o >= cmdAt && o < (protocol == 3 ? 0x38000u : 0x80000u)) {
        u32 k = (o - cmdAt) >> 1, cmd = protocol == 3 ? k >> 8 : k >> 12, val = protocol == 3 ? k & 0xff : k & 0xfff;
        switch(cmd) {
        case 0: return (u16)(frames & 0x7fff) | (latest & 1) << 15;
        case 1: p1 = val; return 0;
        case 2: p2 = val; return 0;
        case 3: status = val; return 0;
        case 4: coin = val; return 0;
        case 5: go(); return (u16)frames;
        case 6: if(sound.empty()) return 0; { u8 c = sound.front(); sound.pop_front(); return 0x8000 | c; }
        case 7: if(val & 2) resetPending = true; run = val & 1; return 0;
        case 8: pokeAddr = protocol == 3 ? (pokeAddr & 0xff00) | val : (pokeAddr & 0xf000) | val; return 0;
        case 9: pokeAddr = protocol == 3 ? (pokeAddr & 0x00ff) | val << 8 : (pokeAddr & 0x0fff) | (val & 0xf) << 12; return 0;
        case 10: wram[pokeAddr & 0xffff] = val; return 0;
        case 11:
          if(buffers() == 2) { taken = latest; return (u16)(taken | (snapFrame[taken] & 0x7fff) << 1); }
          taken = latest; slot ^= 1; slotBuf[slot] = latest;
          return (u16)(slot | (snapFrame[taken] & 0x7fff) << 1);
        case 12: return (u16)(snapSpr[taken] & 0xff);   // the sprite auto-animation counter, as the taken buffer saw it
        //the console diagnostic (neo_cart.sv): 13 the state - the loader at RUN (4) once the reset ran, the copier and the
        //memory engine idle, run, boot_ok, irq pending, the boot flag, 1; 14 the gos taken / dropped since the reset;
        //15 the core's address, bits 23-8 (the model: its PC)
        case 13: return (u16)((resetPending ? 1 : 4) << 13 | (u32)copying << 10 | (u32)run << 5 | (u32)!resetPending << 4 | (u32)irqPending << 3 | (u32)boot << 2 | 1);
        case 14: return (u16)(dgGo << 8 | dgDrop);
        case 15: return (u16)(r.pc >> 8);
        }
      }
      return 0xffff;
    }
  };
  maybe<Neo68k&> neoRef;
  std::unique_ptr<Neo68k> neo;

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
  static constexpr const char* rbfModels[] = {"ssf", "neo68k"};    //the models this board has (more as cores are added)

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
      if(line) {
        auto parts = nall::split(line, " ");
        model = parts.size() ? parts[0] : line;
        rbfParams = line;
        if(!name) name = {"our own configuration (", line, ")"};
      }
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

  auto frequency() -> u32 override { return neo ? 12'000'000 : Interface::frequency(); }
  auto main() -> void override { if(neo) return neo->main(); Interface::main(); }

  auto load() -> void override {
    identifyRbf();
    neo.reset();
    if(rbfModel == "neo68k") {
      neo = std::make_unique<Neo68k>(*this);
      for(auto& kv : nall::split(rbfParams, " ")) {
        if(kv == "packed") { neo->packed = true; continue; }
        auto p = nall::split(kv, "=");
        if(p.size() != 2) continue;
        u32 v = p[1].beginsWith("0x") ? (u32)p[1].slice(2).hex() : (u32)p[1].natural();
        if(p[0] == "rom") { neo->romOff = v; continue; }
        if(p[0] == "table") { neo->tableOff = v; continue; }
        if(p[0] == "spill") { neo->spillOff = v; continue; }
        if(p[0] == "boot" && v >= 32) { neo->bootOff = v; continue; }
        if(p[0] == "window" && v >= 32) { neo->windowAddr = v; neo->protocol = 3; continue; }
        if(v > 30) { print("[EverDrive PRO] neo68k: bank ", v, " for ", p[0], " refused (banks 0-30; never 31)\n"); continue; }
        if(p[0] == "prog") neo->progBank = v;
        if(p[0] == "bios") neo->biosBank = v;
        if(p[0] == "boot") neo->bootBank = v;
        if(p[0] == "window") neo->window = v;
      }
      if(neo->packed) print("[EverDrive PRO] neo68k: the packed program ROM at ", hex(neo->romOff), ", its table at ", hex(neo->tableOff),
            ", the spill at ", hex(neo->spillOff), ", the boot block at ", hex(neo->bootOff), "\n");
      else print("[EverDrive PRO] neo68k: program bank ", neo->progBank, ", BIOS bank ", neo->biosBank, ", boot bank ", neo->bootBank, "\n");
      if(neo->windowAddr) print("[EverDrive PRO] neo68k: the SH2s' window at cart ", hex(neo->windowAddr), "-", hex(neo->windowAddr + 0x37fff), " (protocol 3)\n");
      else print("[EverDrive PRO] neo68k: the SH2s' window: bank ", neo->window, "\n");
    }
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
    if(neo) {                                         //the core's window (neo68k): a cart address range, or a bank
      u32 local = (n22)address;
      if(neo->windowAddr) {
        if(local >= neo->windowAddr && local < neo->windowAddr + 0x38000) {
          if(neo->on) return neo->windowRead(local - neo->windowAddr);
          u32 o = local - neo->windowAddr;            //off: the key, and the read itself the cart's (map_ssf's)
          if(o >= 0x36000) {
            u32 k = (o - 0x36000) >> 1, cmd = k >> 8, val = k & 0xff;
            if(neo->key1 && cmd == 7 && val == 0) { neo->on = true; neo->resetPending = true; neo->run = false; neo->key1 = false; }
            else neo->key1 = cmd == 7 && val == 3;
          }
        }
      }
      else if(word >> 18 == neo->window) return neo->windowRead(word << 1 & 0x7ffff);
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

  //krikzz's edio block at 0xa130d0 (lib_base/base_io.sv), only its USB write (edio everdrive.c ed_cmd_usb_wr:
  //'+', ~'+', 0x22, ~0x22, the length high byte first, the bytes): with ARES_EDIO_USB=<file> the bytes the MCU
  //would send to the PC go to that file, FIFOSTAT (0xa130d2) reads both FIFOs empty. Without it: as before.
  FILE* usbFile = nullptr;
  bool usbChecked = false;
  u32 usbState = 0, usbLeft = 0;
  auto usbOpen() -> bool {
    if(!usbChecked) {
      usbChecked = true;
      if(auto name = getenv("ARES_EDIO_USB")) usbFile = fopen(name, "wb");
    }
    return usbFile;
  }
  auto usbByte(u8 b) -> void {
    static const u8 head[4] = {'+', '+' ^ 0xff, 0x22, 0x22 ^ 0xff};
    if(usbState < 4) { usbState = b == head[usbState] ? usbState + 1 : (b == '+' ? 1 : 0); return; }
    if(usbState == 4) { usbLeft = b << 8; usbState = 5; return; }
    if(usbState == 5) { usbLeft |= b; usbState = usbLeft ? 6 : 0; return; }
    fputc(b, usbFile); fflush(usbFile);
    if(!--usbLeft) usbState = 0;
  }

  auto readIO(n1 upper, n1 lower, n24 address, n16 data) -> n16 override {
    if(address >> 1 == 0xa130d2 >> 1 && usbOpen()) return 0xc000;
    return data;  //the registers are write-only
  }

  auto writeIO(n1 upper, n1 lower, n24 address, n16 data) -> void override {
    if(getenv("ARES_EDIO_TRACE") && address >> 8 == 0xa130) { static u32 n; if(n++ < 60) print("[edio] write ", hex(address, 6L), " ", hex(data, 4L), " u", (u32)upper, " l", (u32)lower, "\n"); }
    if(address >> 1 == 0xa130d0 >> 1 && lower && usbOpen()) { usbByte(data.bit(0,7)); return; }
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
    if(neo) { neo->run = false; neo->resetPending = true; neo->on = false; neo->key1 = false; }   //held until the key, then run (command 7)
    if(neo) { neo->taken = 1; neo->slot = 1; neo->slotBuf[0] = 0; neo->slotBuf[1] = 1; neo->latest = 0; neo->copying = false; }
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
