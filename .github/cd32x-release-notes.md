ares CD32X v1.9 - PC Engine CD cart cores and their error traps (12 Nov 2026)

- v1.9: **Dracula X CD32X's and Gate of Thunder CD32X's cart cores, modelled, with error traps.** Both ports run a whole PC Engine CD on the Mega EverDrive PRO's FPGA and both boot and play on the console. Their models (`dracx` with its backup RAM port, key and console diagnostic words; `gotx`, new) are chosen by a `mega-pro.rbf.ares` beside the launcher. The traps print what the console's cart would not serve: a console read or write of PSRAM chip 1 once the engine runs (`[dracx TRAP]`, `[gotx TRAP]`), the PC Engine CPU fallen into the I/O page, SELECT asked, the backup RAM unlike the cart's (`GOTX_BRAM_GARBAGE=1`); `ARES_32X_TRAP=1` prints every SH2 access to the frame buffer, palette or VDP registers without FM (the console makes the SH2 wait there) and the 68000's with FM. `DRACX_FEEDLOG=<file>` logs the sectors a core asks for. Nothing changes for any other cart or ROM. README-FPGA-FINDINGS.md section 6; `scripts/cd32x-pce-trap-test.py`.

ares CD32X v1.8 - U-R-R's round rings (8 Nov 2026)

- v1.8: U-R-R's cart FPGA engine model updated to its version 3 (U-R-R v110 on): the shockwave rings are round (16 directions, half-pixel offsets). U-R-R v110 needs this (with v1.7 it falls back to its 32X effects); v109 needs v1.7.

ares CD32X v1.7 - U-R-R's cart FPGA engine (7 Nov 2026)

- v1.7: **U-R-R's own Mega EverDrive PRO core, modelled.** U-R-R (MicroNut99/rev32xcd, a plain 32X cartridge ROM) ships a `mega-pro.rbf` that is krikzz's `map_smd` plus an engine of its own in the cart's last 32 KB (`0x3F8000-0x3FFFFF`): it draws the game's effects for the Genesis VDP - shockwave rings, a glow with a ripple, the STAGE n / GAME OVER / YOU WIN lettering turning and zooming - and the 32X shows them through its background black. ares runs it with the game's own C++ reference model (the Verilog matches it bit for bit in simulation), **on whenever U-R-R's ROM is loaded** (from U-R-R v109 on), as on the console with the `.rbf` beside the ROM. `URR_FPGA=0` turns it off, `URR_FPGA=1` forces it on. Any other ROM: unchanged. README-CD32X.md: "The cart's FPGA configuration".

ares CD32X v1.6 - the Mega EverDrive PRO's mega-pro.rbf (7 Nov 2026)

- v1.6: **the cart's FPGA configuration, `mega-pro.rbf`**. The EverDrive menu loads a file of that name from the folder of the ROM it starts - a CD32X launcher (`EDCD_*.32X`) too - into the FPGA instead of its built-in mapper (console, 7 Nov 2026: Metal Slug CD32X v46 boots with krikzz's `map_ssf` beside its launcher). ares now finds it by itself next to the launcher (Settings > Options > EverDrive launcher; headless `--ramcart-boot`; or the folder of a `SEGA SSF` ROM loaded first) and identifies it by SHA-256: krikzz's `map_ssf` is the 16 MB SSF mapper ares already has (the same run, picture for picture); krikzz's other mappers (SVP, Mega-CD, SMD, SE) are named in the log and run as SSF with a warning; a port's own core is named by a one-line `mega-pro.rbf.ares` beside it, for the model a later ares adds. No file: as before. `--no-rbf` / `ARES_RBF=0` ignore it. README-CD32X.md: "The cart's FPGA configuration".

ares CD32X v1.5 - Real Sega 32X Hardware Timing calibrated against the games (29 Oct 2026)

- v1.5: **Settings > Real Sega 32X Hardware Timing** recalibrated against the games' own console readings, not only the timing probe. v1.3/v1.4 ran the games about 30 % slower than the console; now Hurrican's B1 benchmark lands inside the console's ranges (effects 34.4-35.3 ms against 31.6-36.2, the three backgrounds 85.4-86.1 against 84.6-85.1) and OpenLara v1.4r1 matches the console in the same place (Caves' start, frame 59-65 ms against 58-66), with the probe's numbers kept. The change: how the two SH2s share the bus (they take turns only when both hammer it, as in the probe; otherwise an access waits much less), and the SH-2's division unit takes its 39 clocks. Off (the default): unchanged. Every source and number: README-CD32X.md, "Calibration against the games" and "Where the console data comes from".
- ares-headless: the same with ARES_32X_TIMING=console; new keys cont=3, cgap, cmul, div.

- v1.4, **bug fixed** (in v1.2 and v1.3): loading a 32X+CD cartridge ROM first (the `.32x`, then the disc) ignored the Mega EverDrive PRO chosen in Settings > Options - Options' cart was only used when the disc was loaded on its own. The ROM ran as a read-only cartridge, so the 32X+CD Plutonia cartridge (DOOM32XCD_V2, which writes its own memory, as on the PRO) ran at 1-2 fps with broken graphics. Now a `SEGA SSF` ROM that needs the Mega CD runs from the EverDrive PRO chosen in Options, as on the PRO: tested in ares-headless, Plutonia's demo at 20 fps with correct graphics (before the fix: broken walls, then 0 fps). Its memory and save are the cart's file in `Systems`. Any other ROM, or another cart type in Options: as before.
- v1.4: Settings > Firmware: the file picker starts in the `bios` folder next to ares.exe. The BIOS defaults (bios\bios_CD_U/J/E.bin) are unchanged.
- v1.4 keeps v1.3's Real Sega 32X Hardware Timing switch (off by default).

- v1.3: **Settings > Real Sega 32X Hardware Timing**: the 32X as the console runs it - its memory and instruction costs as the CD32X timing probe measured them on an NTSC Sega CD + 32X. Off: ares's own (faster) timing, as before. Takes effect at once, also in a running game, and is remembered. A first calibration: not yet checked against every game's console frame rate.
- ares-headless: the same with ARES_32X_TIMING=console; --stats (frame rate and accesses per picture), --dump-sdram frame:file.

- v1.2: Settings → Options → **EverDrive launcher**: Browse to `EDCD.32X` (cart type Mega EverDrive PRO), load the disc - the cartridge boot as the console does it, in the window, with the pad. The disc's saves (its bank 31) land in the EverDrive cart file.
- With the EverDrive's write-enable bit on, a 68000 write to anything but the cart disturbs an SH2 read of the cart (the read returns 0, the word changes). A launcher that leaves the bit on fails here as on the console; the CD32X BootCamp's EDCD.32X (bit on only inside its copies) boots.
- v1.1: a 68000 write made with RV = 1 disturbs nothing (the 32X holds the SH2s off the cart then); v1.0 was too strict there and froze OpenLara CD32X at its title.
- With the 32X on, the 68000 cannot write the cart's first 512 KB directly: such a write is dropped.
- Bank 31 is the EverDrive's battery RAM (the .ram save file here; bram.srm on the real card).
- Nine 16 MB EverDrive carts and nine 16MB-PCB carts, Systems folder next to ares.exe, Sega CD BIOS in bios/.

Cartridge boot (headless): ares-headless --game disc.cue --machine cd32x --bios-us bios.bin --ramcart-type everdrive --ramcart-boot EDCD.32X
Details: CD32X BootCamp docs/11 and docs/12; README-CD32X.md in the zip.

