# The EverDrive PRO's FPGA: what ares shows, what the console does (findings, 8 Nov 2026)

For people and for an AI working on a CD32X port that puts its own logic on the Mega EverDrive PRO's FPGA
(`mega-pro.rbf` next to the `EDCD_*.32X` launcher). It records what three such cores taught, the day Jumping
Flash CD32X's first core failed on the owner's console after passing in ares:

- **Rayman CD32X** (MicroNut99/Rayman_32X_RAM, `fpga/`): a sprite compositor. **Works on the console.**
- **U-R-R** (MicroNut99/rev32xcd, `cartridge/fpga/`; README-CD32X.md has its version pairs): an engine behind a
  window at the end of the cart, beside krikzz's `map_smd`. **Works on the console** (v111, engine v3.1).
- **Jumping Flash CD32X** (MicroNut99/JumpingFlash_CD32X, `fpga/jfgeo/`): a geometry engine. The first core
  (7 Nov) passed ares and its Verilator check, then hung on the console; the second (8 Nov), rebuilt the
  Rayman way, **works on the console** (V23: the title, World 1-1's bonus stage at 30 fps; V24: World 1-1 at
  about 11 fps, 2-2 at 8-14).

**The short version:** ares's models answer a read the instant it is made, so ares cannot show a core that
answers the console too late. Build every core the way Rayman's and U-R-R's are built (section 2), check the
console's side in simulation with an *early* sample point (section 3), and treat a green ares run as saying
nothing about the bus.

## 1. What went wrong with the first Jumping Flash core

The engine answers SH2 reads of the cart's window 3 (cart 0x180000-0x1FFFFF): its frame buffers (the
EverDrive's `sram`) and a control area. The first core:

| | first core (7 Nov) | why it fails on a console |
|---|---|---|
| a console read of the engine's SRAM | through a clocked arbiter: the read seen through two flip-flops, then the SRAM access, then a register | the data is ready 120-200 ns after the read begins; the console takes it sooner and gets **the previous word** (garbage triangles, then a hang) |
| PSRAM reads by the engine | address and OE 4 clocks (80 ns at 50 MHz) | the PSRAMs are 70 ns parts; with the FPGA's pin delays that is no margin |
| a console access during an engine access | checked with one register's delay, not after | the bus pins come through two flip-flops: an access that starts in the last two clocks is missed |
| at power-on | always on: every read of window 3 is the engine's | the cart is not the standard SSF cart; nothing checks the engine is there before it is used |

Measured afterwards in Verilator (`fpga/jfgeo/sim/tb_cart.cpp` with the console taking its data N clocks into
a read): the first core is wrong in every view at 60, 80 and 100 ns and right from 120 ns on. The second core
is right at 60 ns. ares's model (`jfgeo-rbf` branch) answers in zero time, so both looked the same there.

## 2. How the cores that work are built (the rules)

From Rayman's `map_rayfx.sv` / `rfx_core.sv` (and U-R-R's `map_urr.sv` / `urr_engine.sv`):

1. **krikzz's mapper stays krikzz's.** Rayman (and Jumping Flash's second core) instantiate `map_ssf.sv`
   unchanged and only override its outputs (`always_comb`, `mout = mout_ssf;` then the changes); U-R-R is a
   copy of `map_smd.sv` with its few changes marked `URR`. Keep krikzz's MIT notice.
2. **Off until a key.** At power-on and after `map_rst` every output is krikzz's: the cart is the standard
   one. A fixed sequence of reads (Rayman: three command-port reads; Jumping Flash: 0x1E0000 + 2k for k =
   0x4A46, 0x4745, 0x4F4E) turns it on. U-R-R: an ID read from a window its ROM leaves 0xFF. The game reads the
   status after the key; anything unexpected means no core, and the game runs without it.
3. **The console always wins, combinationally.** Who gets a memory is decided from the bus pins (`ce_lo`,
   `oe`, `we_lo`, `we_hi`) with no clock between: the console's access goes straight to the memory.
4. **The console reads the engine's memory straight from its address pins.** For a read of the engine's SRAM:
   `sram.addr = cpu.addr`, `sram.oe = 1`, `map_do = sram_do`. No state machine, no flip-flop: as fast as the SRAM.
5. **Engine accesses are slow and watched.** PSRAM: address and OE 5 clocks, the data taken at the 5th (100 ns).
   SRAM writes: address and data a clock, WE 2, a clock off. During each access, and for **two clocks after it**,
   watch the console's use of that memory (the pins through two flip-flops, ORed with the pins now); if the
   console came in, do the access again.
6. **Commands are reads.** The SH2 cannot write the cart. A read's address bits are the command; detect the
   start of a read through two flip-flops (`ff[1:0] == 2'b01`), take the address then (it is steady during a
   read), act a clock later. Rayman: the value 0 is the status read, so every argument is `0x2000 | v`.
7. **Never lose a command.** A GO that arrives while the engine is busy waits (status bit 0 stays set).
8. **The area the engine answers is one the game never reads for data.** Rayman: the last 128 KB of each page,
   kept free by the packer. U-R-R: the last 4 KB of the cart. Jumping Flash: window 3, empty in its layout.
9. **Without the `.rbf` the same build must still run** (the key check fails; the SH2s do the work).
10. **Timing closure is read, not assumed:** the `.fit.summary` and `.sta.summary` after every Quartus run, no
    negative slack at any corner (Jumping Flash's second core: worst setup slack 2.631 ns at slow 85C).

## 3. Checking a core before the console

- **Verilator, through the console's pins**, with the console's sample point early: 60 ns into a read, not at
  the end of its cycle. Random console traffic between the engine's accesses (Jumping Flash: 30 % and 85 %), a
  PSRAM model that gives garbage for 3 clocks after its address or OE changes, and the multiplexers of your
  mapper modelled as wires. A pass at the end of the cycle proves nothing about timing.
- **ares** (the model on its own branch, never `main` without the owner) checks the game's side: the protocol,
  the picture, the frame rate. It does not check the bus. The model answers at once.
- **Quartus**: the fit and timing summaries read back from the owner's PC, the rbf's md5 noted.
- **The console** decides. Test one change at a time.

## 4. Other things ares did not show (same week)

| What | ares | the console |
|---|---|---|
| **The SH2's FRT as a timer.** The 32X `crt0.s` sets the FRT to clear on compare match A with OCRA = 1 (it drives the interrupt line) | counts freely: a profile built on it looks right | stays at 0-1: every time read from it is garbage (Jumping Flash V24's HUD). Use the watchdog timer in interval mode (`0xFFFFFE80` = `0xA53F`: clock / 8192, 0.356 ms a tick; read `0xFFFFFE81`) |
| **The PWM's carrier.** Jumping Flash's movies ran the PWM at 11,025 Hz and left it on after the movie | nothing heard | an audible whine from the 32X for the rest of the game. Run the PWM at 22,050 Hz or more (Rayman) and switch it off when it is not used |
| **Frame rate.** Jumping Flash with the engine | 20-30 fps | 8-14 fps in World 2-2 (400-450 triangles), about 11 in 1-1 (1,600-1,700). Not measured on the console yet: the profile used the FRT |
| **A hang 7 s into World 1-1** (V21 and V22: all switches on) | none, past 9:06 on the same disc | stopped at 9:53 every time, with the first core and with the second, so not the bus. V23 (no CD audio, movies, stage sound packs or CMD 81) and V24 (CD audio and stage sound packs back) run: it came with CMD 81 (CD pause) or the movies. Open |
| **The V22 launcher without the `.rbf`** | boots to the title and plays | a black screen at once on the stock EverDrive core. Open |

## 5. For the next AI

- Read Rayman's `README_FOR_AI.md` and `fpga/README.md`, and U-R-R's `cartridge/fpga` notes, before writing any RTL.
- Start from Rayman's `map_rayfx.sv` (the wrapper) and its two memory channels; put your engine behind them.
- A deterministic stop (same frame every run) is not a bus problem; a random one may be. Check with the core
  swapped or removed before rewriting RTL.
- The owner burns discs for real tests only. One change per disc, small discs where possible (no CD audio:
  about 45 MB), and say what each test will tell.
