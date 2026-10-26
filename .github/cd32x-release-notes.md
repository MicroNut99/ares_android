ares CD32X v1.1 - the Mega EverDrive PRO as the console runs it (25 Oct 2026)

- With the EverDrive's write-enable bit on, a 68000 write to anything but the cart disturbs an SH2 read of the cart (the read returns 0, the word changes). A launcher that leaves the bit on fails here as on the console; the CD32X BootCamp's EDCD.32X (bit on only inside its copies) boots.
- v1.1: a 68000 write made with RV = 1 disturbs nothing (the 32X holds the SH2s off the cart then); v1.0 was too strict there and froze OpenLara CD32X at its title.
- With the 32X on, the 68000 cannot write the cart's first 512 KB directly: such a write is dropped.
- Bank 31 is the EverDrive's battery RAM (the .ram save file here; bram.srm on the real card).
- Nine 16 MB EverDrive carts and nine 16MB-PCB carts, Systems folder next to ares.exe, Sega CD BIOS in bios/.

Cartridge boot (headless): ares-headless --game disc.cue --machine cd32x --bios-us bios.bin --ramcart-type everdrive --ramcart-boot EDCD.32X
Details: CD32X BootCamp docs/11 and docs/12; README-CD32X.md in the zip.

