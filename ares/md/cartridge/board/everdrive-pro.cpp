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
//    68000's data.  With RV = 1 the 32X holds the SH2s off the cart, so a 68000 write then (the
//    cartridge boot's copies push once inside their W = 1 window) disturbs nothing.  Not seen on the
//    console: the exact overlap, which word changes - the model is deterministic, not exact.  A
//    launcher that leaves W on fails here as on the console.
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

  auto load() -> void override {
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
      if(reset && ramCart && !program.empty()) putProgram();  //launcher: started again from the menu
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
