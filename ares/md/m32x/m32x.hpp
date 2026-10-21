//Mega 32X

struct M32X {
  Node::Object node;
  Memory::Readable<n16> vectors;
  Memory::Writable<n16> sdram;

  struct Debugger {
    //debugger.cpp
    auto load(Node::Object) -> void;

    struct Memory {
      Node::Debugger::Memory sdram;
    } memory;
  } debugger;

  struct SH7604 : SH2, Thread {
    Node::Object node;
    Memory::Readable<n16> bootROM;

    struct Debugger {
      maybe<SH7604&> self;

      //debugger.cpp
      auto load(Node::Object) -> void;
      auto instruction(u16 opcode) -> void;
      auto interrupt(string_view) -> void;

      struct Tracer {
        Node::Debugger::Tracer::Instruction instruction;
        Node::Debugger::Tracer::Notification interrupt;
      } tracer;
    } debugger;

    //sh.cpp
    auto load(Node::Object, string name, string bootROM) -> void;
    auto unload() -> void;

    auto main() -> void;
    auto instructionPrologue(u16 instruction) -> void override;
    auto step(u32 clocks) -> void override;
    auto internalStep(u32 clocks) -> void;
    auto power(bool reset) -> void;
    auto restart() -> void;
    auto syncAll(bool force = false) -> void;
    auto syncOtherSh2(bool force = false) -> void;
    auto syncM68k(bool force = false) -> void;

    auto busReadByte(u32 address) -> u32 override;
    auto busReadWord(u32 address) -> u32 override;
    auto busReadLong(u32 address) -> u32 override;
    auto busWriteByte(u32 address, u32 data) -> void override;
    auto busWriteWord(u32 address, u32 data) -> void override;
    auto busWriteLong(u32 address, u32 data) -> void override;
    auto lineFillCost(u32 address) -> u32 override;

    //cd32x-timing: this SH2's share of the shared bus lately
    u64 uStart = 0, uBusy = 0, uCount = 0;
    u64 total = 0;        //clocks stepped since power-on (monotonic; the scheduler rebases clock())
    u32 util256 = 0;      //busy fraction x 256
    u32 tenure16 = 16;    //mean bus tenure x 16

    //serialization.cpp
    auto serialize(serializer&) -> void;

    struct IRQ {
      struct Source {
        n1 enable;
        n1 active;
      };
      Source pwm;   // 6
      Source cmd;   // 8
      Source hint;  //10
      Source vint;  //12
      Source vres;  //14
    } irq;

    s32 cyclesUntilSh2Sync = 0;
    s32 cyclesUntilM68kSync = 0;
    s32 minCyclesBetweenSh2Syncs = 0;
    s32 minCyclesBetweenM68kSyncs = 0;
  };

  struct VDP {
    maybe<M32X&> self;
    Memory::Writable<n16> dram;
    Memory::Writable<n16> cram;
    std::span<n16> fbram;  //VDP-side active DRAM bank
    std::span<n16> bbram;  //CPU-side active DRAM bank

    struct Debugger {
      //debugger.cpp
      auto load(Node::Object) -> void;

      struct Memory {
        Node::Debugger::Memory dram;
        Node::Debugger::Memory cram;
      } memory;
    } debugger;

    //vdp.cpp
    auto load(Node::Object) -> void;
    auto unload() -> void;
    auto power(bool reset) -> void;

    auto scanline(u32 pixels[1280], u32 y) -> void;
    auto scanlineMode1(u32 pixels[1280], u32 y) -> void;
    auto scanlineMode2(u32 pixels[1280], u32 y) -> void;
    auto scanlineMode3(u32 pixels[1280], u32 y) -> void;
    auto plot(u32* output, u16 color) -> void;
    auto fill() -> void;
    auto selectFramebuffer(n1 active) -> void;
    auto framebufferEngaged() -> bool;
    auto paletteEngaged() -> bool;

    //serialization.cpp
    auto serialize(serializer&) -> void;

    n2  mode;      //0 = blank, 1 = packed, 2 = direct, 3 = RLE
    n1  lines;     //0 = 224, 1 = 240
    n1  priority;  //0 = MD, 1 = 32X
    n1  dotshift;
    n8  autofillLength;
    n16 autofillAddress;
    n16 autofillData;
    n1  framebufferAccess;
    n1  framebufferActive;
    n1  framebufferSelect;
    int framebufferWait;
    n1  hblank;
    n1  vblank;

    struct Latch {
      n2 mode;
      n1 lines;
      n1 priority;
      n1 dotshift;
    } latch;
  };

  struct PWM : Thread {
    Node::Audio::Stream stream;
    nall::DSP::IIR::DCRemoval dcfilter_l, dcfilter_r;

    //pwm.cpp
    auto load(Node::Object) -> void;
    auto unload(Node::Object) -> void;
    auto main() -> void;
    auto step(u32 clocks) -> void;
    auto power(bool reset) -> void;
    auto updateFrequency() -> void;

    //serialization.cpp
    auto serialize(serializer&) -> void;

    struct FIFO {
      auto empty() const -> bool { return fifo.empty(); }
      auto full()  const -> bool { return fifo.full();  }
      auto flush()       -> void { return fifo.flush(); }

      auto read() -> i12 { return last = fifo.read(last); }
      auto write(i12 data) -> void {
        if(fifo.full()) read(); // purge oldest sample
        fifo.write(data);
      }

      queue<i12[3]> fifo;
      i12 last;
    } lfifo, rfifo;

    n2  lmode;
    n2  rmode;
    n1  mono;
    n1  dreqIRQ;
    n4  timer;
    n12 cycle;

    n32 periods;
    n32 counter;
    i12 lsample;
    i12 rsample;
    n16 lfifoLatch;
    n16 rfifoLatch;
    n16 mfifoLatch;
  };

  //m32x.cpp
  auto load(Node::Object) -> void;
  auto unload() -> void;
  auto save() -> void;

  auto power(bool reset) -> void;

  auto vblank(bool) -> void;
  auto hblank(bool) -> void;

  //bus-internal.cpp
  auto readInternal(n1 upper, n1 lower, n32 address, n16 data = 0) -> n16;
  auto writeInternal(n1 upper, n1 lower, n32 address, n16 data) -> void;

  //bus-external.cpp
  auto readExternal(n1 upper, n1 lower, n24 address, n16 data) -> n16;
  auto writeExternal(n1 upper, n1 lower, n24 address, n16 data) -> void;

  //io-internal.cpp
  auto readInternalIO(n1 upper, n1 lower, n29 address, n16 data) -> n16;
  auto writeInternalIO(n1 upper, n1 lower, n29 address, n16 data) -> void;

  //io-external.cpp
  auto readExternalIO(n1 upper, n1 lower, n24 address, n16 data) -> n16;
  auto writeExternalIO(n1 upper, n1 lower, n24 address, n16 data) -> void;

  //serialization.cpp
  auto serialize(serializer&) -> void;

  SH7604 shm;  //master
  SH7604 shs;  //slave
  VDP vdp;
  PWM pwm;

  struct IO {
    //$000070
    n32 vectorLevel4;

    //$a15000
    n1 adapterEnable;
    n1 adapterReset = 1;
    n1 resetEnable = 1;

    //$a15004
    n2 romBank;

    //$a1511a
    n1 cartridgeMode;

    //$4004
    n8 hperiod;
    n8 hcounter;

    n1 hintVblank;
  } io;

  struct DREQ {
    n1 vram;
    n1 dma;
    n1 active;

    n24 source;
    n24 target;
    n16 length;

    queue<n16[8]> fifo;
  } dreq;

  n16 communication[8];

  //CD32X timing work (ares_android, cd32x-timing): what the SH2s do to the 32X's memories, for
  //comparing a game's frame cost in ares with the console. Counted, not serialized.
  struct Stats {
    u64 cartRead = 0;      //16-bit SH2 reads of the cart (0x02 / 0x22 area)
    u64 fbRead = 0;        //16-bit SH2 frame buffer reads
    u64 fbWrite = 0;       //16-bit SH2 frame buffer writes / overwrites
    u64 sdramRead = 0;     //16-bit SH2 SDRAM bus reads (uncached reads and cache line fills)
    u64 sdramWrite = 0;    //16-bit SH2 SDRAM writes
    u64 swaps = 0;         //frame buffer swaps actually shown (a new 32X picture)
  } stats;

  //SH2 memory costs, in SH2 clocks per 16-bit access. Upstream's values are the defaults; the
  //environment variable ARES_32X_TIMING (e.g. "cart=8,fbw=1,fifo=4,drain=3") replaces them.
  struct Timing {
    u32 cartRead = 6;
    u32 fbRead = 5;
    u32 fbWrite = 4;      //each frame buffer write, plus any FIFO stall
    u32 fifoDepth = 0;    //0: no FIFO model (upstream)
    u32 fifoDrain = 0;    //SH2 clocks the VDP takes to retire one FIFO word
    u32 sdramRead = 0;
    u32 sdramWrite = 0;
    u32 insn = 0;         //extra SH2 clocks per 16 recompiled instructions (pipeline stalls, fetches)
    u32 io = 1;           //32X register access (upstream: 1)
    u32 fill = 12;        //SDRAM cache line fill (burst), the whole line
    u32 cfill = 12;       //added to a line fill from elsewhere (cart: its 8 reads are charged too)
    u32 contention = 0;   //1: an SH2 waits while the other one holds the shared bus
    u32 arb = 0;          //clocks to hand the bus over to the other SH2
    u64 fifoEnd = 0;      //the time the FIFO is empty, in SH2 clocks
  } timing;
  auto loadTiming() -> void;
  auto sh2Now() -> u64;
  auto fbWriteCost() -> u32;
  auto busCharge(u32 clocks) -> void;
  auto busStall(M32X::SH7604& self, M32X::SH7604& other) -> u32;
  auto busAccount(M32X::SH7604& self, u32 clocks) -> void;
};

extern M32X m32x;
