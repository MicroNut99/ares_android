# The EverDrive PRO's FPGA: what ares shows, what the console does (findings, 8-9 Nov 2026)

For people and for an AI working on a CD32X port that puts its own logic on the Mega EverDrive PRO's FPGA
(`mega-pro.rbf` next to the `EDCD_*.32X` launcher). It records what three such cores taught, the day Jumping
Flash CD32X's first core failed on the owner's console after passing in ares:

- **Rayman CD32X** (MicroNut99/Rayman_32X_RAM, `fpga/`): a sprite compositor. **Works on the console.**
- **U-R-R** (MicroNut99/rev32xcd, `cartridge/fpga/`; README-CD32X.md has its version pairs): an engine behind a
  window at the end of the cart, beside krikzz's `map_smd`. **Works on the console** (v111, engine v3.1).
- **Metal Slug CD32X** (MicroNut99/FBNeo_MetalSlug, `fpga/`): SNK's 68000 on the cart. **Works on the console**
  (section 5).
- **Jumping Flash CD32X** (MicroNut99/JumpingFlash_CD32X, `fpga/jfgeo/`): a geometry engine. The first core
  (7 Nov) passed ares and its Verilator check, then hung on the console; the second (8 Nov), rebuilt the
  Rayman way, **works on the console** (V23: the title, World 1-1's bonus stage at 30 fps; V24: World 1-1 at
  about 11 fps, 2-2 at 8-14).
- **Dracula X CD32X** and **Gate of Thunder CD32X** (MicroNut99/DraculaX_32X, MicroNut99/Gate-of-Thunder_CD32X): a
  whole PC Engine CD on the cart (the HuC6280, a VDC, a CD unit). **Both boot and play on the console** (11 Nov).
  Their error traps are in section 6.

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
| **The SH2's FRT as a timer.** The 32X `crt0.s` the games use sets the FRT to clear on compare match A with OCRA = 1 (it drives the interrupt line) | models the clear too (`sh7604/timer.cpp`): such a profile is wrong in both; nobody had looked at it in ares | stays at 0-1: every time read from it is garbage (Jumping Flash V24's HUD). Use the watchdog timer in interval mode (`0xFFFFFE80` = `0xA53F`: clock / 8192, 0.356 ms a tick; read `0xFFFFFE81`). The timing probe's FRT counts because its own start-up code does not set the clear |
| **The PWM's carrier.** Jumping Flash's movies ran the PWM at 11,025 Hz and left it on after the movie | nothing heard | an audible whine from the 32X for the rest of the game. Run the PWM at 22,050 Hz or more (Rayman) and switch it off when it is not used |
| **Frame rate.** Jumping Flash with the engine | 20-30 fps | 8-14 fps in World 2-2 (400-450 triangles), about 11 in 1-1 (1,600-1,700). Not measured on the console yet: the profile used the FRT |
| **A hang 7 s into World 1-1** (V21 and V22: all switches on) | none, past 9:06 on the same disc | stopped at 9:53 every time, with the first core and with the second, so not the bus. V23 (no CD audio, movies, stage sound packs or CMD 81) and V24 (CD audio and stage sound packs back) run: it came with CMD 81 (CD pause) or the movies. Open |
| **The V22 launcher without the `.rbf`** | boots to the title and plays | a black screen at once on the stock EverDrive core. Open |

## 5. A CPU on the cart: Metal Slug CD32X (9 Nov 2026)

**Metal Slug CD32X** (MicroNut99/FBNeo_MetalSlug, `fpga/`, branch `m0-recorder`) runs SNK's 68000 program on the
FPGA (fx68k at 25 MHz, `fpga/rtl/neo_cart.sv` beside krikzz's `map_ssf`, Rayman's wrapper pattern); the SH2s only
draw. **Works on the console** (9 Nov 2026: the attract, the demo, Mission 1, the menu's level select). Unlike the
three cores above, it runs a CPU out of PSRAM0, the chip the 32X reads its code and data from. What that took:

| Symptom on the console (ares: fine) | Cause | Fix (`neo_cart.sv`) |
|---|---|---|
| The key missed on some boots, with the same rbf that took it on others | a command read's address taken the clock the read was first seen: the address could still be moving | the address taken once steady two clocks; a new address with CE/OE still down is a new read |
| After the game's reset the core never ran a frame: its last address stayed in the vector table, every vblank dropped | with the SH2s reading the cart back to back, the core waited two clocks after each console read before taking the chip, and never got a turn | the PSRAM the core's as soon as the console's read is answered (held), not two clocks later |
| Still dead in the vector table after that | the core's reads of PSRAM came back **wrong** under the SH2s' traffic. A compositor shrugs off a bad pixel; a CPU fetching a bad reset vector or opcode is dead | **every core read of PSRAM done twice, accepted only when both agree** (a read cut by the console dropped, the clean one kept) |

What found it was a **diagnostic on the console**, not the simulator: a build that prints, on the 32X's screen,
what the cart answers at each step of the start (the key, the loader's state, the frames, the core's last
address) and on top of every picture in the game. Three console runs of that narrowed it to the core's PSRAM
reads. Verilator only reproduced the starvation once its console was made tighter than the default
(`cart_tb` `TBCYC=10 TBSAMPLE=6 TBGAP=2`), and never the bad data.

The ares model (`neo68k-model` branch) now has the console core's key (off until command 7 with 3, then with 0;
the window is the cart's PSRAM until then) and its diagnostic commands 13-15. It still answers at once and never
corrupts a read: **for a CPU core, ares cannot show the failures above.**

Rules added to section 2 for a core that runs a CPU from a memory the console also uses:

11. **Verify every read** the CPU makes from a shared chip (read twice, compare), or give the CPU its own copy of
    what it runs in a memory the console never reads.
12. **Take the chip at once** when the console's read is done; a sync delay before every turn can starve the core.
13. **Build a console diagnostic first**, before the first console test: the key's answer, the core's state and
    address, its frame count, printed by the 32X on the screen.

## 6. Error traps for PC Engine CD cart cores (Dracula X and Gate of Thunder, 11-12 Nov 2026)

Dracula X CD32X and Gate of Thunder CD32X both run a PC Engine CD on the cart: the MiSTer HuC6280, Dracula X's
`dx_vdc` / `dx_cd`, the System Card and the game's track in the cart's PSRAM. Both boot and play on the console; both
found that ares ran green while the console failed. The traps below are what they built from those failures. They are
on main for every future PC Engine CD core: reuse them.

| Trap | Switch | Where | What it catches | On the console |
|---|---|---|---|---|
| The 32X's FM rule | `ARES_32X_TRAP=1` (`=2`: every one) | `md/m32x/m32x.cpp` `m32xTrap` | an SH2 access to the frame buffer, palette or VDP registers while FM = 0; a 68000 one while FM = 1; every FM change by the 68000 | the SH2 **waits** until FM = 1 (Sega's manual, "VDP Access Competition"). If the master is the only one that sets FM, it waits for itself for ever. **Any SH2 line printed is a stop on the console.** (Dracula X docs/175) |
| Chip 1 after the start: read | always (dracx, gotx) | `board/everdrive-pro.cpp` `coreTrap` kind 0 | a console read of PSRAM chip 1 (banks 16-30), outside the core's own windows, once the engine runs | the core gives chip 1 to the engine (the System Card is read there). The console gets the engine's bus: dracx answers `0xDEAD ^ word`, gotx `0xFFFF`. (Dracula X docs/171, Gate of Thunder docs/44-45) |
| Chip 1 after the start: write | always (dracx, gotx) | kind 1 | a console write there (dracx: not windows 6 and 7) | lost (ares drops it) |
| The CPU in the I/O page | always (gotx) | kind 2 | the HuC6280 running below `$2000` after having run above it; prints the last 64 PCs | Gate of Thunder run 1b: the CPU at PC 0008 from frame 49, only its interrupts running |
| SELECT | always (gotx) | kind 3 | the SH2 asks for SELECT | never given: RUN + SELECT is the PC Engine's soft reset, and the stack's 6-button read reports SELECT that is not pressed (Dracula X docs/132). ares masks it. |
| The backup RAM | gotx: `GOTX_BRAM_GARBAGE=1` | kind 4 | notes that ares's default (Mednafen's formatted blank one) is not the cart's | the cart's `sram` is uninitialised (reads `0x0FFF`). `GOTX_BRAM_GARBAGE=1` models it. |
| Auto-RUN key | gotx: the key `GTAR` | `gxFrame` | RUN for 16 frames in every 512, until the drive is asked | the same (Gate of Thunder docs/41) |
| The sectors fed | `DRACX_FEEDLOG=<file>` | `dxRead7` | every sector the core asks for, in order | makes a pack's order file (Dracula X `pack_game.py --order`) |

The board's traps print `[<core> TRAP] ...` the first time, then at the 2nd, 4th, 8th ... time. They also behave as the
cart does there, so the run goes on the way the console's would. A new core adds a name to `TrapCore` and calls
`coreTrap(core, kind, text)` with the same kinds. `scripts/cd32x-pce-trap-test.py <dracx|gotx> <out dir>` makes a
launcher (no game data) that starts the engine and then reads and writes bank 20: both chip 1 traps must print.

**What ares cannot trap** (each has cost console runs):
- **A USB read of the cart's memory during a run** (`edlink memrd` of SRAM or PSRAM). It takes the chip from the console
  (krikzz's `mem_ctrl`) and the game stops. Only the mapper's page, `0x1830000`, is safe to read while the game runs.
  Dracula X v14-v25 and Gate of Thunder runs 1-8 were spent on it (Dracula X docs/177-178, Gate of Thunder docs/49).
  ares has no MCU. The core can count MCU accesses in its status page.
- The SRAM / PSRAM pins' timing, the FPGA's timing margin, and the console bus's clock against the core's. **Never
  flash a fit that missed timing.**
- **The picture rate.** ares draws 53-60 pictures a second, where the console drew 14-18 for Gate of Thunder v11.
- **A command "not waited for".** It can starve on the console: in Gate of Thunder `eng_pad` was skipped whenever the
  68000 was on a list, which is rare in ares and nearly always on the console (docs/51).

**A new PC Engine CD core, before the console:**
1. Run it in ares with `ARES_32X_TRAP=1`, its model's `*_BRAM_GARBAGE` (or equivalent), and console timing
   (`ARES_32X_TIMING=console`). There must be no SH2 FM line and no chip 1 trap.
2. Give the core a status page at `0x1830000` (frames, PC, flags) and a PC ring. Read only that page over USB while
   the game runs.
3. Read Gate of Thunder's `CONSOLE_FINDINGS.md` and Dracula X's docs/175 and 178.

A green ares run with no trap is still not a console pass.

## 7. A drawing core that shares the SRAM with the console: OutRun CD32X's renderer (`orfx`, 11-12 Nov 2026)

OutRun CD32X (MicroNut99/CannonballCD32X, `fpga/orfx`) has the FPGA draw the whole picture (Cannonball's renderer in
RTL) into the cart's SRAM; the master SH2 runs the game and sends each frame's video state as reads of a command
window; the slave SH2 copies the finished picture to the frame buffer. It took ten console runs. Every one of them was
green in ares first. This is the report of what failed, the traps that now catch it, and what ares still cannot show.
The log, run by run: CannonballCD32X `fpga/orfx/FINDINGS.md`.

**In ares now** (model `orfx`, `board/orfx/orfx_board.h`; `mega-pro.rbf.ares` = `orfx`):

| Trap / fault | Switch | What it catches or does | On the console |
|---|---|---|---|
| WORD_WHILE_BUSY | `ARES_ORFX_TRAP=log\|stop\|off` (all the traps below) | a command word while the core draws | the core queues 1024 words and applies them after the frame; ares' model would apply them at once |
| SEQ_GAP / SEQ_DUP / UNIT_WRONG | | a word lost / read twice / a unit whose CHECK failed | run 4: a doubled or lost word put the decoder out of step - garbage in the memories, seconds of drawing. Since V6 a unit out of step writes nothing and is sent again |
| NOT_ON, BAD_OP, DATA_OVERRUN | | a word before the key; an unknown command; DATA past a memory | lost words, wrapped writes |
| SHOW_DRAWING, READ_DRAWING | | the buffer being drawn shown or read | a torn picture |
| SLOW, STUCK, RUNAWAY, SLOTS | | a frame over 33 ms / 500 ms; a sprite line over 1024 words; over 253 colours | the game's 2 s timeout, then the SH2s draw |
| ROM1_READ | | the console reads banks 16-30 while the core is on | the core's bus, not the bank |
| lost and doubled words | `ARES_ORFX_FAULT=gap=N,pair=N,dup=N` | 1 word in N lost / two in a row lost / read twice | the console's address bus strays for 40 ns now and then; what the game's resend path must survive |
| a slow or dead core | `ARES_ORFX_FAULT=slow=K,hang=F` | the core K times slower; hangs after F frames | run 4, run 9 |
| **a USB read of the cart** | `ARES_EDP_USB=period=P,len=L,at=A,chip=C` (any EverDrive model) | every P ms a console read of that PSRAM chip gets the word at the MCU's address | section 6's first "cannot trap", now injectable: OutRun freezes in ares exactly as Dracula X froze on the console |

**What the console showed that ares did not** (each cost at least one run; the first four are the report for whoever
extends ares):

1. **The pins of a shared memory (runs 9-10).** The core's SRAM pins followed the console's read strobe itself:
   address, data and WE all changed the moment the console came. A core write caught there had its address changing
   while WE was on - an SRAM writes wherever the address goes. On the console: stray tiles that stay in tile RAM,
   specks in the picture; and before that, a memory channel left idle with its answer never given (the renderer stuck
   in its 3rd frame). ares' memories are ideal and it has no pins: **it cannot trap this.** The trap lives in the
   core's Verilator bench ("TRAP SRAM": the address must not change in the clock before, during or after a clock with
   WE; kit 9's core: 77,476 in 12 frames). Rule: a memory the core *writes* gets its address and WE from registers,
   in an order; "the console wins at once from the pins" is right only for a memory the core reads.
2. **Both SH2s on one cart bus (run 10).** The master's stream took 12-23 ms a frame on the console while the slave
   copied the picture from the cart; ares showed 11 ms and 30 FPS where the console ran 15. ares gives each SH2 its
   cart reads without the other waiting. *Wanted in ares:* one cart bus for both SH2s under
   `ARES_32X_TIMING=console` (an access waits for the other CPU's access, a cache-line fill holds the bus for its 8
   words at ~340 ns each).
3. **A frame-rate trap of the game's own making.** With the loop a little over one frame, two logic ticks came due
   each picture, and two ticks plus the stream never fit: the game sat at 15 FPS until the scene got light. An
   emulator that runs a little fast hides this completely. *Wanted:* nothing in ares; in the game, print the loop's
   parts (logic, send, waits) on screen and in the debug core from the first build.
4. **Cart window 6 (run 8).** The SH2's reads of 0x300000-0x37FFFF (the stream's first home) never appeared at the
   cart on the owner's console: the debug core counted none in a whole run, while window 7 worked. The cause is not
   known. *Wanted:* a line in ares when an SH2 reads the EverDrive's window 6 (`ARES_32X_TRAP`), until someone finds
   out why.
5. **The debug words' packing.** A concatenation one bit too wide shifted every word above it in the report. Count
   the widths of anything that feeds fixed slots.
6. **A build of the wrong core.** Two games' kits on one bench: the build script of one, the disc of the other. Every
   kit now prints its core's md5 and the poll says which core answers (`NOT THE ORDBG CORE`).

**A core that draws or writes into a memory the console also reads, before the console:**
1. Its bench carries the memory's own rules as traps (address stable around WE; a PSRAM's settle time) and the
   console's habits as faults: reads back to back with /OE low, the address straying 40 ns, words lost and doubled,
   the whole picture copied while the core draws. No trap, every picture equal to the C model.
2. In ares: no `[orfx trap]` without faults; with `gap`, `pair`, `dup` the picture still right; with `ARES_EDP_USB`
   expect a freeze - and never read the cart's memory over USB in a run.
3. The MCU's view of the core from the first build (the status page at 0x1830000): frames, words taken, units wrong,
   the renderer's state, MCU accesses of the memory, a watchdog's bite. A protocol only the console can query is
   blind exactly when the console's reads are what fails.

## 8. For the next AI

- Read Rayman's `README_FOR_AI.md` and `fpga/README.md`, and U-R-R's `cartridge/fpga` notes, before writing any RTL.
- Start from Rayman's `map_rayfx.sv` (the wrapper) and its two memory channels; put your engine behind them.
- A deterministic stop (same frame every run) is not a bus problem; a random one may be. Check with the core
  swapped or removed before rewriting RTL.
- A memory your core writes and the console reads: section 7's rule for its pins, and its bench trap, before any
  console run.
- The owner burns discs for real tests only. One change per disc, small discs where possible (no CD audio:
  about 45 MB), and say what each test will tell.
