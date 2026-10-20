# ares CD32X

ares v148 with a **RAM cart** for Sega CD + 32X ("CD32X") games: the cart that the
Kobo / OpenLara / OpenJazz / Hurrican / SuperTux / OutRun / Sonic CD boot stacks copy
the game into. Without it those discs stop at the boot splash in every emulator.

## Use (Windows)

1. Download `ares-cd32x-windows-x64.zip` from this repository's **Releases**
   (`cd32x-latest`), unzip, run `ares.exe`.
2. **Settings → Firmware → Mega CD**: set the Sega CD BIOS (US / Japan / Europe).
   The 32X boot ROMs are built in.
3. **Settings → Options → Mega Drive Settings**: `Mega CD 32X: RAM cart` is on by
   default. Pick **4 MB** (MicroNut99 RAM cart) or **8 / 16 MB** (16MB-PCB-CD32X,
   paged) and, if wanted, *Write-protect*. Changes apply at the next load.
4. **Load → Sega → Mega CD 32X**, pick the game's `.cue`.

The cart's contents are saved (battery-backed, like the real cart) to
`Systems\RAM Cart <size> MB.ram` next to `ares.exe`, one file per size,
shared by all discs, exactly as one physical cart is. Delete the file to "wipe" it.

## Use (Android)

1. Install `ares-cd32x-android-arm64.apk` from the **`android-latest`** release
   (Android 8 or newer, 64-bit ARM). Allow installs from your browser or file manager.
2. Open it and allow **"manage all files"**: a `.cue` names its `.bin` by path, so ares reads
   your game folders directly. Its own files go in `/sdcard/ares/`.
3. **Settings → Sega CD BIOS**: pick your BIOS files. **RAM cart**: 4, 8 or 16 MB (default 16).
4. Browse to a `.cue` and tap it. Discs boot as **Sega CD 32X + RAM cart**; long-press a game to
   boot it as plain Sega CD instead, or to add a Sega CD disc to a 32X / Genesis cartridge.

The RAM cart is `/sdcard/ares/Systems/RAM Cart <size> MB.ram`, the same format and name as on
Windows: copy it between the PC's `Systems\` folder and the phone to carry a loaded cart over.
Save states go in `/sdcard/ares/States/`.

Controls: the on-screen 6-button pad, or a controller (X = A, A = B, B = C, L1 = X, Y = Y,
R1 = Z, Start, Select = Mode; R2 holds fast-forward). Back or ☰ opens the menu (save / load
state, reset, log, quit). *Show fps and COMM12 STATE* in Settings shows the boot stack's status.

## What is emulated

| | |
|---|---|
| 68000, Mega CD mode | cart at `0x400000`-`0x7FFFFF`; with the 32X on, only while RV = 1 |
| 68000, 32X windows | `0x880000` (cart 0-512 KB), `0x900000` + bank `0xA15104` |
| SH2 | `0x02000000` (cached) / `0x22000000` (cache-through), read only; stalls while RV = 1 |
| CART_IN | not asserted: the console boots the disc and the SH2 boot ROMs take the `_CD_` path |
| 8 / 16 MB paging | window 0 = cart `0x000000`-`0x1FFFFF`, window 1 = `0x200000`-`0x3FFFFF`; page register `0xA130F0` (window 0) / `0xA130F2` (window 1), even-byte write, page on D8-D10; reset: window 0 = page 0, window 1 = page 1. The 8 MB board decodes D8-D9 (pages 4-7 mirror 0-3); the 4 MB cart ignores the writes |
| Save states | include the cart's SRAM and page registers |

Open hardware questions (whether `/TIME` reaches the board with ADEN = 1, RV timing)
are **not** settled by this emulation: the page register is accepted whether or not
RV is set. Treat a pass here as "the code is right", not "the board will work".

Sega CD + 32X **cartridge** games with CD audio (Sonic 32X Plus, OpenTyrian + Sega CD)
load with **Load → Sega → Mega 32X**; when the cart's header lists the CD, ares asks for
the `.cue` too.
