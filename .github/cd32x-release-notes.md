ares CD32X v1.3 - Real Sega 32X Hardware Timing, a menu switch (29 Oct 2026)

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

