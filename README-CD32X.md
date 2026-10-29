# ares CD32X

ares v148 with a **RAM cart** for Sega CD + 32X ("CD32X") games: the cart that the
Kobo / OpenLara / OpenJazz / Hurrican / SuperTux / OutRun / Sonic CD boot stacks copy
the game into. Without it those discs stop at the boot splash in every emulator.

## Use (Windows)

1. Download `ares-cd32x-windows-x64.zip` from this repository's **Releases**
   (`cd32x-latest`), unzip, run `ares.exe`.
2. Put your Sega CD BIOS files in the **`bios`** folder next to `ares.exe` as `bios_CD_U.bin`
   (US), `bios_CD_J.bin` (Japan) and `bios_CD_E.bin` (Europe): ares uses them by default.
   (Or set them in **Settings → Firmware → Mega CD**.) The 32X boot ROMs are built in.
3. **Settings → Options → Mega Drive Settings**: `Mega CD 32X: RAM cart` is on by
   default. Pick the **cart type** (the paged **16MB-PCB-CD32X**, or a **Mega EverDrive PRO**:
   see below) and which **cart (1-9)** is in the slot; **Create** makes that cart as a blank
   16 MB file (it asks before replacing one that exists). *Write-protect* if wanted. The choice
   applies at the next load.
4. **Load → Sega → Mega CD 32X**, pick the game's `.cue`.

ares keeps running when its window loses focus (Settings → Drivers → Input → *When focus is
lost: Block input*): the game and its music go on, and keys typed in other windows don't reach
it. Choose *Pause emulation* there for upstream's behaviour, or *Allow input* to keep a
controller working while another window is in front.

Each cart is saved (battery-backed, like the real cart) to `Systems\RAM Cart 16 MB-<n>.ram`
next to `ares.exe`: nine separate carts, each shared by every disc you run with it, exactly
as a physical cart is. Carts marked *(none)* in Options have no file yet: the first game that
runs with one creates it, or press **Create**. (A `RAM Cart 16 MB.ram` from the
first build becomes cart 1.) Games that know only the 4 MB cart see its layout: after reset
the 16 MB board shows pages 0 and 1, which is the 4 MB cart.

## Mega EverDrive PRO (16 MB)

The EverDrive PRO's extended SSF mapper with its write bit makes it 16 MB of RAM for a 32X
program: tested on a console with the CD32X BootCamp's `edtest` ROMs (all 27 banks, the 68000 and
both SH2s, 0 bad words). ares has it in three ways:

| How | What ares does |
|---|---|
| **A `SEGA SSF` ROM with no SRAM entry in its header** (Load → Mega 32X or Mega Drive) | The ROM in the EverDrive's 16 MB, as the EverDrive menu starts it. Its save RAM is bank 31 (512 KB, the `.ram` save file). `edtest` shows the console's result: ALL TESTED BANKS OK. (A `SEGA SSF` ROM *with* an SRAM entry keeps the standard board here - on the console the EverDrive's mapper is on for it too, and bank 31 is the memory the OS saves; build such a probe without the entry to run it in ares.) |
| **The RAM cart of a CD32X disc** (cart type *Mega EverDrive PRO*) | The disc boots with the EverDrive as a launcher leaves it: write on, banks 0-7 in windows 0-7, CART_IN released. Carts 1-9 are `Systems\EverDrive PRO 16 MB-<n>.ram`. (A model, not the console: see below.) |
| **The cartridge boot** (Windows: Settings → Options → *EverDrive launcher* → Browse to `EDCD.32X`, cart type *Mega EverDrive PRO*, then Load the disc; headless: `--ramcart-type everdrive --ramcart-boot EDCD.32X`) | What the console does (25 Oct 2026): it boots the launcher from the EverDrive, which starts the Sega CD itself and hands over to the disc's stack; the EverDrive stays the cartridge at `0x000000`. The disc's saves in the EverDrive's bank 31 land in the cart file. CD32X BootCamp `docs/12`. |

| Register (write-only, /TIME) | |
|---|---|
| `0xA130F0` CTRL0 | `P X W L C . . . . . . R R R R R`: taken only with P (bit 15) set. X = 32X mode (no effect), W = write enable, L = LED, C = CART_IN (no effect on the console: the EverDrive stays the cartridge), R = the bank in window 0 |
| `0xA130F2`-`FE` CTRL1-7 | the bank (0-31, bits 0-4) in each 512 KB window of cart `0x080000`-`0x3FFFFF` |

**What the console settled (25 Oct 2026, CD32X BootCamp `edw0`, `edsave`, the cartridge boot), and
ares models since this build:**
- With W = 1, a 68000 write to anything but the cart (work RAM, the VDP, the Sega CD) disturbs the cart
  while an SH2 reads it: the read comes back 0 and the word changes. ares: an SH2 cart read within 1 µs
  of such a write returns 0 and takes the 68000's data (deterministic, not exact). A launcher that leaves
  W on fails in ares as on the console; the one that turns W on only inside its copies boots.
- With the 32X on, the 68000 cannot write cart `0x000100`-`0x07FFFF` directly: ares drops it (a copy goes
  through another window with that bank mapped in).
- The C bit changes nothing on the console. The "RAM cart" use above is kept for discs booted the normal
  way with another cart.
- Bank 31 is the EverDrive's battery RAM, kept on its SD card as `bram.srm` (256 KB) when the ROM's
  header declares save RAM (ares: the `.ram` file). Banks 0-29 are PSRAM and the EverDrive menu wipes
  them at every run; ares does too when it boots a launcher (below), otherwise it keeps the 16 MB in the
  cart file.
- **An SH2 that reads the cart while the 68000 holds RV = 1 gets garbage** (26 Oct 2026, CD32X BootCamp
  `edsave3`: 965,207 bad of 978K reads, answered about four times faster than real ones). A real cartridge
  is held off by the 32X until RV = 0; the EverDrive answers. ares: such a read returns a pseudo-random
  word (the same in every run), so code or data an SH2 fetches from the cart during a 68000 copy crashes
  in ares as it does on the console. `ARES_RV_GARBAGE=0` holds the SH2 instead (a real cart's behaviour),
  `ARES_RV_GARBAGE=1` gives every cart the EverDrive's rule, `ARES_RV_LOG=1` prints which SH2 read where
  (`RV-CART M pc=... pr=... addr=...`). Use `--interpreter` for it: the recompiler keeps code it has
  already translated and only sees the reads it has not.
- **The EverDrive menu wipes banks 0-29 every time it starts a ROM** (edsave). ares: a launcher boot
  (`--ramcart-boot`, or the GUI's EverDrive launcher) fills banks 0-29 with the pattern before it puts the
  launcher in, at power-on and at a reset, so every start loads the whole game again, as on the console.
  Bank 31 (the save RAM) is kept.
- **An SH2 word or long access at an address that is not a multiple of its size stops the console** (a
  CPU address error: OpenJazz's red screen, 27 Oct 2026, from a sun drawn with an odd word read). ares let
  it through. Now the interpreter (`--interpreter`) takes the SH2's address errors as the console does
  (the core had them, compiled out); the recompiler still lets them through. `ARES_SH2_ADDRESS_ERRORS=0`
  turns them off.
- Still a choice in ares: byte writes to the registers, a console reset.

## Use (Android)

1. Install `ares-cd32x-android-arm64.apk` from the **`android-latest`** release
   (Android 8 or newer, 64-bit ARM). Allow installs from your browser or file manager.
2. Open it and allow **"manage all files"**: a `.cue` names its `.bin` by path, so ares reads
   your game folders directly. Its own files go in `/sdcard/ares/`.
3. **Settings → Sega CD BIOS**: pick your BIOS files. **RAM cart**: which 16 MB cart (1-9) is in
   the slot. During play, *Insert RAM cart…* in the menu swaps carts (the game restarts, as on
   the console); each game remembers the cart it last ran with.
4. Browse to a `.cue` and tap it. Discs boot as **Sega CD 32X + RAM cart**; long-press a game to
   boot it as plain Sega CD instead, or to add a Sega CD disc to a 32X / Genesis cartridge.

The carts are `/sdcard/ares/Systems/RAM Cart 16 MB-<n>.ram`, the same format and names as on
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

## Console timing: the 32X hardware matching mode

**What it is:** ares running CD32X games at the console's speed, so a game can be profiled and tuned
in ares instead of burning a disc for every test. **Off by default** (ares as released keeps
upstream's timing, which runs 32X games faster than the console).

**Turning it on:**
- **Windows:** the menu **Settings > Real Sega 32X Hardware Timing** (a tick). It takes effect at once,
  also in a running game, and is remembered.
- **ares-headless:** `ARES_32X_TIMING=console` in front of the command (`--stats` prints the frame rate
  and the accesses per picture once a second).

**Status, 29 Oct 2026:**
- One model in `main`: the costs of cart, frame buffer and SDRAM accesses, the VDP's 4-word write
  FIFO and fill, the two SH2s waiting for each other on the shared bus, cache line fills, access
  counters (`ares-headless --stats`, `--profile`), and the SH2's own instruction timings (branches,
  BT/BF taken, multiplies, TAS, load-use and MAC stalls) in the interpreter and the recompiler.
- "console" uses the costs the timing probe measured on the owner's console (NTSC Sega CD + 32X,
  28 Oct 2026); `ares/md/m32x/bus-internal.cpp` lists every number.
- **Not yet checked against the games' own console frame rates** (the table below): a first
  calibration. Treat its frame rates as an estimate until that check is done.

### Why the probe: the 32X's bus arbitration is not fully documented

Sega's 32X hardware manual gives ranges, not the arbitration itself: how the two SH2s, the 68000 and
the VDP share the bus and the frame buffer, and what one costs the other. Those timings come from
**our own measurements on the console, which are not published anywhere else.**

Tuning ares to games' frame rates alone did not work. Jazz's open sky and its caves make the same
memory accesses per picture in ares (counted), yet run at 10 and 18-20 fps on the console. A frame
rate mixes too many costs. The probe measures each cost by itself instead.

### The data used

**Console readings of the games** (the owner's console, NTSC Sega CD + 32X, RAM cart or EverDrive):

| Game, build | Place | Console | ares as released |
|---|---|---|---|
| Jazz Jackrabbit, R5 16 MB, profiler build | Diamondus 1, start (sky, sun) | **10 fps or less** (video, 19 Oct 2026) | 29 fps |
| Jazz, R5 | Diamondus 1, purple cave | **18-20 fps** (video, 19 Oct) | 23 fps |
| Jazz, R5 | Diamondus 1, open, foreground grass | **10 fps** (video, 19 Oct) | 19 fps |
| Jazz, R5 | Diamondus 2, level-end tally (camera still) | **10 fps** (video, 19 Oct) | - |
| Jazz, REV EverDrive | Diamondus 1, in front of the giant waterfalls | **3-5 fps** (video, 27 Oct) | - |
| Hurrican, P1 | stage 1, timing line on | **19.9 fps**: AI 6, MAP 0, CPY 3, SPR 2, OVR 1, HUD 1, WAIT 30 ms (photo) | 29.9-30 fps: HUD 12-15, WAIT 6-10 |
| OpenLara, v1.4 | Caves | **frame 64-110 ms**, drawing 43-103 ms | frame 41 ms |

The Jazz counter could not show less than 10 then, so "10" means "10 or less".

**The manuals:**
- Sega 32X hardware manual: cart reads 6-15 wait states on a 2-clock bus cycle; frame buffer reads
  5-12 waits, writes 1-3 through the VDP's 4-word FIFO (3 clocks a word, 5 when full).
- Hitachi SH7604 (SH-2) manual: the instruction timings.

**ares's own counts** for the same scenes (`cd32x-bus-jh`, `--stats`): per picture, Jazz makes
about 26,000-31,000 cart reads, 12,000-14,000 frame buffer writes and 53,000-56,000 SDRAM writes.
With the Genesis plane on, the idle slave SH2 also made about 460,000 SDRAM reads per picture.
Jazz R7 and Hurrican P2b no longer do that.

**The timing probe** (CD32X BootCamp, `probe/`): one screen of numbers measured by the master
SH2's own free-running timer. It covers:
- each instruction kind;
- cart, SDRAM, frame buffer and overwrite reads and writes;
- cache hits and line fills;
- 32X registers;
- the frame buffer in the blank and in the picture;
- the VDP fill;
- the frame and line length;
- a Sega CD command's round trip;
- the shared bus: each SH2 access while the other SH2 hammers each memory.

| Probe line (clocks an access) | ares as released | Console, RAM cart | Console, EverDrive |
|---|---|---|---|
| NOP / MUL.L / BRA+NOP | 1.0 / 1.0 / 2.0 | awaiting the photo | awaiting the photo |
| CART RD.W / RD.L / LINE | 7.0 / 13.0 / 60.1 | | |
| SDRM RD.W / WR.W / LINE | 1.0 / 1.0 / 13.1 | | |
| FB RD.W / WR.W / WR.L | 6.0 / 5.0 / 9.0 | | |
| FILL256 (clocks) | 384 / 400 | | |
| FRAME / LINE (clocks) | 384,000 / 1,465 | | |
| CD CMD (clocks) | 71,112 (EverDrive: 9,128) | | |
| cart read, slave idle / slave reading the cart | 7.0 / 7.0 (no contention) | | |

### Build commands

The probe disc (in the CD32X BootCamp folder; `EDCD=1` also makes the EverDrive launcher, and the
disc boots both ways):
```sh
cd /mnt/s/KoboPort/sega-toolchain-12.1/sega/CD32X-BootCamp
GAME=probe DISC_NAME=CD32X_PROBE_V1 EDCD=1 bash build.sh 2>&1 | tee build_log.txt
```
Burn `disc/CD32X_PROBE_V1.cue`. For the EverDrive, copy `edboot/EDCD.32X` to the SD card as
`EDCD_PROBE_V1.32X`. Boot it, wait about two seconds and photograph the screen; **A** measures
again.

The same disc in `ares-headless` (built as in "Building" below):
```sh
ares-headless --game CD32X_PROBE_V1.cue --machine cd32x --bios-us us.bin \
  --ramcart probe4.ram --ramcart-size 4 --frames 2410 --screenshot 2400:probe.ppm
ares-headless --game CD32X_PROBE_V1.cue --machine cd32x --bios-us us.bin \
  --ramcart-type everdrive --ramcart-boot EDCD_PROBE_V1.32X --frames 2410 --screenshot 2400:probe_ed.ppm
```
Once the model is merged, the same runs with `ARES_32X_TIMING=console` in front give its numbers.
The model is right when they match the console's.

## Building

Three things build from this repository. Nothing about the RAM cart is chosen at build time:
cart size, cart number and write-protect are all settings, at run time.

| What | Built by | Output |
|---|---|---|
| Windows ares (`ares.exe`) | upstream ares' CMake, `desktop-ui` | `build/desktop-ui/rundir/` |
| Android app (APK) | Gradle, with `android/native` (CMake + NDK) inside it | `android/app/build/outputs/apk/release/app-release.apk` |
| `ares-headless` test tool (Linux) | CMake, `android/native` | `<build dir>/ares-headless` |

GitHub builds the first two on every push to `main` (`.github/workflows/cd32x-windows.yml`,
`.github/workflows/android.yml`) and publishes them as the `cd32x-latest` and `android-latest`
pre-releases. Both workflows can also be started by hand: **Actions → (workflow) → Run workflow**.
Upstream's full build matrix (`push.yml`) only runs by hand in this fork.

### Windows (`ares.exe`)

What GitHub uses, and what works on a Windows PC:

1. Install [MSYS2](https://www.msys2.org/) and open the **MSYS2 CLANG64** shell.
2. Install the tools:
   ```sh
   pacman -S --needed make git cmake ccache ninja mingw-w64-clang-x86_64-clang
   ```
3. Get the source and build:
   ```sh
   git clone https://github.com/MicroNut99/ares_android
   cd ares_android
   cmake --preset windows-ci-mingw-native
   cmake --build build --config RelWithDebInfo
   ```
   The first configure downloads ares' prebuilt Windows dependency bundle (`ares-deps`:
   SDL, librashader, slang shaders, ...), as pinned in `deps.json`.
4. `ares.exe` and its DLLs are in `build/desktop-ui/rundir/`. To make it like the release zip,
   add the folders `Systems\` and `bios\` next to `ares.exe` and put your Sega CD BIOS in
   `bios\` as `bios_CD_U.bin`, `bios_CD_J.bin`, `bios_CD_E.bin`.

CMake switches (`-D<NAME>=<value>` on the configure line; the preset sets the ones marked *):

| Switch | Default | What it does |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `RelWithDebInfo`* | `Release`, `RelWithDebInfo`, `Debug` or `MinSizeRel` |
| `ARES_CORES` | all systems | Which emulators to build, e.g. `-DARES_CORES="md"` for only the Mega Drive family (Genesis, Sega CD, 32X, Sega CD 32X): a much faster build |
| `ARES_BUILD_LOCAL` | `ON` (`OFF`*) | Optimise for the CPU of the PC that builds it (faster there, may not run on older PCs). The release is built with `OFF` |
| `ARES_UNITY_CORES` | `OFF` (`ON`*) | Build each core as one file: faster from scratch, slightly faster emulation, slower rebuilds |
| `ARES_PRECOMPILE_HEADERS` | `OFF` (`ON`*) | Precompile `ares/ares.hpp` |
| `ARES_PROFILE_ACCURACY` | `OFF` | Prefer accuracy over speed in a few places |
| `ARES_ENABLE_CHD` | `ON` | `.chd` disc images (libchdr) |
| `ARES_ENABLE_LIBRASHADER` | `ON` | Video shaders (librashader, slang shaders) |
| `ARES_BUILD_OPTIONAL_TARGETS` | `OFF` (`ON`*) | Also build test programs and tools (`mia`, `mame2bml`, CPU tests) |
| `ARES_BUILD_OFFICIAL` | `OFF` | Official version strings (from the environment variable of the same name) |
| `ARES_SKIP_DEPS` | `OFF` | Don't download the prebuilt dependencies (use your own) |
| `ARES_DEBUG_DEPENDENCIES` | `OFF` | Download the dependencies' source too, for debugging into them |
| `ARES_MINGW_USE_DWARF_SYMBOLS` | `OFF` | DWARF debug symbols for gdb / lldb instead of CodeView |
| `ENABLE_IPO` | `ON` | Link-time optimisation |
| `ENABLE_CCACHE` | `ON` | Use ccache when installed |
| `ARES_COMPILE_DEPRECATION_AS_WARNING` | `OFF` | Deprecation errors become warnings |

Other presets in `CMakePresets.json`: `windows` (Visual Studio 2022 + ClangCL, in `build_vsclang`),
`windows-msvc` (Visual Studio 2022 + MSVC), and the CI variants `windows-ci-cl-native`,
`windows-ci-msvc-native` and the ARM64 cross builds `windows-ci-cl-cross` / `windows-ci-msvc-cross`.

`ares.exe` command-line switches (`ares.exe --help` lists them):

| Switch | What it does |
|---|---|
| `--system "Mega CD 32X" game.cue` | Load a game as a given system, without asking (`"Mega CD"`, `"Mega 32X"`, `"Mega Drive"`, ...) |
| `--setting name=value` | Set a setting for this run only, e.g. `--setting MegaDrive/RAMCartSlot=3` (repeatable; `--dump-all-settings` lists the names) |
| `--settings-file path` | Use another `settings.bml` |
| `--save-state 1`…`9` | Load that save-state slot after the game starts |
| `--fullscreen` / `--pseudofullscreen` | Start full screen / in a borderless window |
| `--kiosk` | Minimal user interface (implies `--no-file-prompt`) |
| `--no-file-prompt` | Don't ask for optional extra files |
| `--shader name` | Video shader to use |
| `--terminal` | Open a console window for log output |
| `--dump-all-settings` | List every setting name and exit |
| `--version`, `--help` | Version string / this list |

The CD32X settings, for `--setting` or `settings.bml`:

| Setting | Values | |
|---|---|---|
| `MegaDrive/RAMCart` | `true` / `false` | RAM cart in the slot for Mega CD 32X games (default `true`) |
| `MegaDrive/RAMCartSlot` | `1`…`9` | Which 16 MB cart: `Systems\RAM Cart 16 MB-<n>.ram` |
| `MegaDrive/RAMCartWriteProtect` | `true` / `false` | The cart's write-protect switch |
| `Input/Defocus` | `Block` / `Allow` / `Pause` | When ares' window loses focus (default `Block`: keeps running) |

Example: start OpenLara on cart 3, full screen:
```bat
ares.exe --system "Mega CD 32X" --setting MegaDrive/RAMCartSlot=3 --fullscreen "C:\Games\OpenLara\CDROMPlayer_cart16_music.cue"
```

### Android (APK)

Needs: JDK 17, the Android SDK (platform 34), NDK **27.2.12479018**, CMake 3.28 or newer, Ninja
and Gradle 8.9 (the repository has no Gradle wrapper: install Gradle, or open `android/` in
Android Studio).

1. Tell Gradle where the SDK and CMake are, in `android/local.properties`:
   ```properties
   sdk.dir=/path/to/Android/Sdk
   cmake.dir=/path/to/cmake        # the folder that holds bin/cmake
   ```
2. Build:
   ```sh
   cd android
   gradle assembleRelease
   ```
3. The APK: `android/app/build/outputs/apk/release/app-release.apk`. Install it with
   `adb install -r app-release.apk`, or copy it to the phone.

Build settings (in `android/app/build.gradle.kts`):

| Setting | Value | |
|---|---|---|
| `applicationId` | `com.micronut99.ares` | |
| `minSdk` / `targetSdk` / `compileSdk` | 26 / 34 / 34 | Android 8.0 or newer (AAudio) |
| `ndkVersion` | `27.2.12479018` | |
| `abiFilters` | `arm64-v8a` | 64-bit ARM phones only. Add `"x86_64"` for the emulator / Chromebooks |
| CMake arguments | `-DANDROID_STL=c++_static -DCMAKE_BUILD_TYPE=Release`, `-O3` | the native core is always an optimised build, also in `assembleDebug` |
| `versionCode` / `versionName` | 1 / `148-cd32x-1` | raise `versionCode` for builds that must install over older ones from stores |
| signing | `app/ci-signing.jks` (store and key password `ares-cd32x`, alias `ares`) | a fixed key in the repository, so every build installs over the last one. Use your own key for builds you hand out widely |

Switches of the native build (`android/native/CMakeLists.txt`):

| Switch | Value | |
|---|---|---|
| `ARES_CORES` | `md` (fixed) | only the Mega Drive family is built |
| `ARES_UNITY_CORES` | `ON` (fixed) | |
| `ARES_ENABLE_CHD` | `ON` (fixed) | `.chd` discs |
| `ARES_ANDROID_HARNESS` | `OFF` | `ON` builds `ares-headless` instead (desktop hosts, next section) |

The native build replaces ares' `sourcery` resource tool with `android/native/cmake/sourcery.cmake`
(it runs at configure time, so nothing has to run on the build machine), and builds without
ares' desktop user interface (`hiro`) and drivers (`ruby`).

### `ares-headless` (Linux test tool)

The Android app's core, with no window: it boots a game, presses buttons on a schedule, saves
screenshots and the sound, and prints the 32X COMM registers every second (COMM12 is the CD32X
boot stacks' status: `4B10` = game running). Used to test CD32X discs without a phone.

Needs clang (C++20), CMake 3.22+ and Ninja. No desktop libraries.
```sh
cmake -S android/native -B build-headless -G Ninja -DARES_ANDROID_HARNESS=ON \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
ninja -C build-headless ares-headless
```

Example: OpenLara on a blank 16 MB cart, Start pressed at frame 200, screenshots at 30 s and 2 min:
```sh
build-headless/ares-headless --game CDROMPlayer_cart16_music.cue --bios-us bios_CD_U.bin \
  --ramcart cart16.ram --ramcart-size 16 --frames 7200 --press 200:S --press 210: \
  --screenshot 1800:shot30.ppm --screenshot 7199:shot120.ppm
```

| Switch | What it does |
|---|---|
| `--game file` | The game: `.cue` / `.chd` disc, or `.32x` / `.md` / `.bin` / `.gen` / `.smd` / `.zip` cartridge |
| `--disc file` | A Sega CD disc in the drive as well as a cartridge (Sonic 32X Plus, OpenTyrian + Sega CD) |
| `--machine cd32x\|cd\|32x\|md` | Sega CD 32X + RAM cart / Sega CD / 32X / Genesis. Default: discs are `cd32x`, `.32x` is `32x`, the rest `md` |
| `--bios-us`, `--bios-jp`, `--bios-eu file` | Sega CD BIOS per region |
| `--region NTSC-U\|NTSC-J\|PAL` | Force a region (default: from the game, preferring NTSC-U) |
| `--ramcart file` | The RAM cart's file (created blank if missing, written back at the end) |
| `--ramcart-size 4\|8\|16` | 4 MB cart, or the paged 8 / 16 MB board (default 4) |
| `--ramcart-type pcb\|everdrive` | The MicroNut99 carts (default), or a Mega EverDrive PRO (16 MB) as EDBOOT leaves it |
| `--ramcart-boot EDBOOT.BIN` | EverDrive only: boot this launcher ROM from the EverDrive first; it releases CART_IN and starts the Sega CD BIOS |
| `--save-state frame:file`, `--load-state file` | Save a state at that frame (repeatable) / start from a state |
| `--frames n` | How many video frames to run (60 = 1 second; default 600) |
| `--press frame:buttons` | From that frame on, hold these buttons: `U D L R A B C X Y Z M S` (M = Mode, S = Start); `--press 210:` releases all. Repeatable |
| `--screenshot frame:file.ppm` | Save the picture at that frame. Repeatable |
| `--wav file.wav` | Record the sound (48 kHz stereo) |
| `--home dir` | Where the core keeps its files (Sega CD backup RAM); default `./ares-home/` |
| `--interpreter` | SH-2 interpreter instead of the recompiler (slower) |
