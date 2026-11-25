// host/shangon_ref.cpp - Super Hang-On (MAME `shangon`, unprotected) on the PC: the reference pictures.
//
// The board as MAME has it (ref/mame, tools/fetch_ref.sh): two 68000s at 10 MHz (Musashi), the 315-5195
// memory mapper, the System 16B "alt" tilemaps, the Out Run road, the System 16B sprites (315-5196), the
// palette with its shadow / hilight, the Super Hang-On custom I/O, the interrupt lines on their scanlines.
// No sound CPU yet (docs/02): the main 68000's writes to the sound latch go nowhere.
//
// The parts ported from MAME keep MAME's licence:
//   license:BSD-3-Clause  copyright-holders:Aaron Giles   (segaorun.cpp, segaic16.cpp, segaic16_road.cpp,
//                                                          sega16sp.cpp, 315_5195.cpp, tilemap.cpp)
//   license:BSD-3-Clause  copyright-holders:Couriersud    (resnet.cpp: compute_resistor_weights)
// Each is marked "MAME:" below with the function it follows. The rest is ours.
//
//   shangon_ref <romdir> <frames> <outdir> [events]
//     events, comma-separated:  shot:N  (a PPM of frame N; shot:N-M/S every S frames)  coin:N  start:N
//                               gas:N-M  brake:N-M  steer:N-M=VV  (VV hex, 0x80 centre)  dsw:XX  log  stats  nopartial
//                               vdump:N-M/S  (vNNNNN.bin: the video's memories before the draw + MAME's bitmap)
//                               wdump:N-M/S  (wNNNNN.bin: the palette at line 224, MAME's bitmap, the picture's colours)
//                               sprdump:N-M/S  (sNNNNN.bin: sprite RAM before the draw + the 224x320 sprite buffer)
//   Writes <outdir>/fNNNNN.ppm (320x224) and prints a line a second of emulated time.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include "sh_sound.h"
extern "C" {
#include "snd.h"                       // (cart/src: the slave SH2's sound, compared here)
}

extern "C" {
#include "m68k.h"
}

typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t s32; typedef int16_t s16;

// ------------------------------------------------------------------------------------------------ ROMs
static u8  rom_main[0x60000], rom_sub[0x60000];
static u8  rom_subcpu[0x60000];
static u32 sub_base;                  // SH_SUBRELOC: where the relocated sub program's world is (0 = not relocated)      // what the sub 68000 runs: rom_sub, or the relocated image (SH_SUBRELOC)
static u8  gfx1[0x18000];
static u16 sprrom[0x80000];          // ROM_REGION16_BE "sprites", 1 MB
static u8  roadrom[0x10000];         // "segaic16road", 0x8000 loaded, the rest 0
static u8  soundrom[0x10000];
static u8  pcmrom[0x40000];           // the SegaPCM's samples (host/sh_sound.cpp)

static bool load(const std::string& dir, const char* name, u8* dst, size_t len, int step = 1) {
    std::string p = dir + "/" + name;
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) { fprintf(stderr, "missing %s\n", p.c_str()); return false; }
    std::vector<u8> b(len);
    size_t n = fread(b.data(), 1, len, f); fclose(f);
    if (n != len) { fprintf(stderr, "%s: %zu bytes, want %zu\n", p.c_str(), n, len); return false; }
    for (size_t i = 0; i < len; i++) dst[i * step] = b[i];
    return true;
}

static bool load_roms(const std::string& d) {
    bool ok = true;
    // MAME: ROM_START( shangon ) - ROM_LOAD16_BYTE: the even file is the 68000's high byte
    ok &= load(d, "epr-10886.133", rom_main + 0x00000, 0x10000, 2);
    ok &= load(d, "epr-10884.118", rom_main + 0x00001, 0x10000, 2);
    ok &= load(d, "epr-10887.132", rom_main + 0x20000, 0x10000, 2);
    ok &= load(d, "epr-10885.117", rom_main + 0x20001, 0x10000, 2);
    ok &= load(d, "epr-10792.76",  rom_sub + 0x00000, 0x10000, 2);
    ok &= load(d, "epr-10790.58",  rom_sub + 0x00001, 0x10000, 2);
    ok &= load(d, "epr-10793.75",  rom_sub + 0x20000, 0x10000, 2);
    ok &= load(d, "epr-10791.57",  rom_sub + 0x20001, 0x10000, 2);
    ok &= load(d, "epr-10652.54", gfx1 + 0x00000, 0x8000);
    ok &= load(d, "epr-10651.55", gfx1 + 0x08000, 0x8000);
    ok &= load(d, "epr-10650.56", gfx1 + 0x10000, 0x8000);
    static u8 spr[0x100000];
    const char* sn[8] = { "mpr-10798.16", "mpr-10794.8", "mpr-10799.14", "mpr-10795.6",
                          "mpr-10800.12", "mpr-10796.4", "mpr-10801.10", "mpr-10797.2" };
    for (int i = 0; i < 4; i++) {
        ok &= load(d, sn[2 * i],     spr + 0x40000 * i + 0, 0x20000, 2);
        ok &= load(d, sn[2 * i + 1], spr + 0x40000 * i + 1, 0x20000, 2);
    }
    for (int i = 0; i < 0x80000; i++) sprrom[i] = (spr[2 * i] << 8) | spr[2 * i + 1];
    ok &= load(d, "epr-10642.47", roadrom, 0x8000);
    ok &= load(d, "epr-10649c.88", soundrom, 0x8000);
    // the SegaPCM's samples (MAME's "pcm" region: each 32 KB ROM at a 64 KB step, twice)
    const char* pn[4] = { "epr-10643.66", "epr-10644.67", "epr-10645.68", "epr-10646.69" };
    for (int i = 0; i < 4; i++) { ok &= load(d, pn[i], pcmrom + 0x10000 * i, 0x8000); memcpy(pcmrom + 0x10000 * i + 0x8000, pcmrom + 0x10000 * i, 0x8000); }
    return ok;
}

// ------------------------------------------------------------------------------------------------ RAM
static u16 workram[0x4000], cpu1ram[0x4000], roadram[0x800], roadbuf[0x800];
static u16 spriteram[0x800], paletteram[0x1000], tileram[0x8000], textram[0x800];

// ------------------------------------------------------------------------------------------------ board state
static u8  mregs[0x20];              // 315-5195
static u8  to_sound, from_sound;
static int irq2_state, vblank_state, hold_irq;   // main CPU lines; hold_irq: the mapper's reg 4 (HOLD_LINE)
static int sub_irq4;
static int road_control;
static int adc_select;
static int display_enable;
static u16 latched_page[4], latched_ys[4], latched_xs[4];
static u8  in_service = 0xff, in_unknown = 0xff, in_coinage = 0xff, in_dsw = 0xfe;   // (SH_COIN=<hex>: the coin switches)
static u8  adc[4] = { 0x80, 0x00, 0x00, 0x00 };
static int cur_cpu;                  // 0 main, 1 sub
static int sub_reset_pending, main_halted, main_paused;   // (main_paused: the options menu stops the main CPU, kit 7)
static bool log_io, snd_log;
static int cur_line;                  // the line being run (run_line)
static long frame_no;

// ------------------------------------------------------------------------------------------------ the mapper
// MAME: sega_315_5195_mapper_device::compute_region / update_mapping, segaorun_state::memory_mapper
enum { K_RAM, K_ROM, K_SUBROM, K_ROADCTL, K_MISCIO };
struct MapEnt { int kind; u32 start, end, mirror; u16* ram; u32 words; u32 romend; };
static std::vector<MapEnt> mapents;  // searched from the back: later installs win

static void map_add(int index, int kind, u32 offset, u32 length, u32 mirror, u16* ram = nullptr, u32 words = 0) {
    static const u32 region_size_map[4] = { 0x00ffff, 0x01ffff, 0x07ffff, 0x1fffff };
    u32 size_mask = region_size_map[mregs[0x10 + 2 * index] & 3];
    u32 base = (mregs[0x11 + 2 * index] << 16) & ~size_mask;
    MapEnt e;
    e.kind = kind; e.mirror = mirror & size_mask;
    e.start = base + (offset & size_mask);
    e.end = e.start + std::min<u32>(length - 1, size_mask);
    e.ram = ram; e.words = words;
    e.romend = e.end;
    if (kind == K_ROM) {                        // map_as_rom: clipped to the ROM's size
        const u32 romsize = 0x60000;
        if (0 + e.romend + 1 - e.start >= romsize) e.romend = romsize - 1 + e.start;
    }
    e.start &= 0xffffff; e.end &= 0xffffff; e.romend &= 0xffffff;
    mapents.push_back(e);
}

static void update_mapping() {
    mapents.clear();
    for (int index = 7; index >= 0; index--) {
        switch (index) {
            case 5:
                map_add(5, K_ROADCTL, 0x90000, 0x10000, 0xf00000);
                map_add(5, K_RAM, 0x80000, 0x01000, 0xf0f000, roadram, 0x800);
                map_add(5, K_RAM, 0x60000, 0x08000, 0xf18000, cpu1ram, 0x4000);
                map_add(5, K_SUBROM, 0x00000, 0x60000, 0xf00000);
                break;
            case 4: map_add(4, K_MISCIO, 0x90000, 0x10000, 0xf00000); break;
            case 3: map_add(3, K_RAM, 0x00000, 0x01000, 0xfff000, spriteram, 0x800); break;
            case 2: map_add(2, K_RAM, 0x00000, 0x02000, 0xffe000, paletteram, 0x1000); break;
            case 1:
                map_add(1, K_RAM, 0x00000, 0x10000, 0xfe0000, tileram, 0x8000);
                map_add(1, K_RAM, 0x10000, 0x01000, 0xfef000, textram, 0x800);
                break;
            case 0:
                map_add(0, K_RAM, 0x60000, 0x08000, 0xf98000, workram, 0x4000);
                map_add(0, K_ROM, 0x00000, 0x60000, 0xf80000);
                break;
        }
    }
}

// a match: the entry and the offset (bytes) from its start; reads also check the ROM's end
static const MapEnt* map_find(u32 a, u32& off, bool write) {
    for (int i = (int)mapents.size() - 1; i >= 0; i--) {
        const MapEnt& e = mapents[i];
        u32 m = a & ~e.mirror;
        u32 end = (e.kind == K_ROM && !write) ? e.romend : e.end;
        if (m >= e.start && m <= end) { off = m - e.start; return &e; }
    }
    return nullptr;
}

// SH_SHARED=1: which words of the sub's RAM the main CPU reads / writes, how often (shared.txt at the end)
static bool shared_log; static long shared_cnt[2][0x4000]; static long shared_main_cnt[2];
static void shared_note(int w, u32 i) { shared_cnt[w][i]++; shared_main_cnt[w]++; }
static u16 main_read16(u32 a);
static void request_partial();
static void main_write16(u32 a, u16 d, u16 mask);

static bool open_bus_busy;
static u16 open_bus_r() {               // MAME: open_bus_r - the word at the main 68000's PC
    if (open_bus_busy) return 0xffff;
    open_bus_busy = true;
    u16 r = main_read16(m68k_get_reg(nullptr, M68K_REG_PPC) & 0xfffffe);
    open_bus_busy = false;
    return r;
}

static void main_set_irq();

static u8 mapper_read(u32 offset) {     // MAME: sega_315_5195_mapper_device::read
    offset &= 0x1f;
    switch (offset) {
        case 0x00: case 0x01: return mregs[offset];
        case 0x02: return (mregs[0x02] & 3) == 3 ? 0x00 : 0x0f;
        case 0x03: return from_sound;
    }
    return open_bus_r() & 0xff;
}

// the sound commands (docs/04): the game sends 8-byte packets through the latch, bytes <= 80 us apart, packets >= 16 ms;
// a packet's first byte (the first after 0.5 ms of quiet) is its command, 0x80 none - the core keeps them for the
// Genesis' 68000 (MD+): sh_core.sv's register 0x14, shb_take_sound here
static double now_cycles();
static std::vector<int> snd_q; static double snd_last_t = -1e9;
static std::vector<int> latch_bytes;     // every byte to the sound board and its resets (sh_core.sv's latch FIFO, register
                                         // 0x48's bits 9-0) with its time after the one before (0x4A: 4 us, bits 27-16)
static double latch_last_t = -1e12;
static double main_done;                 // the main CPU's cycles run (whole slices)
static int latch_entry(int v) {
    double t = main_done + m68k_cycles_run(); double d = (t - latch_last_t) / 40.0; latch_last_t = t;
    int dd = d >= 4095 ? 4095 : d < 0 ? 0 : (int)d;
    return dd << 16 | v;
}
static bool sndc_on;
static void lq_log(int e)                   // SH_LQLOG=<file>: snd.c's entries as ares' ARES_SHFX_LQLOG has them ("-": a tick)
{
    static FILE* f = nullptr; static int on = -1;
    if (on < 0) { const char* p = getenv("SH_LQLOG"); on = p != nullptr; if (p) f = fopen(p, "w"); }
    if (!f) return;
    if (e < 0) fputs("-\n", f); else fprintf(f, "%03x %u\n", e & 0x3ff, (e >> 16) & 0xfff);
}                     // SH_SNDC=<wav>: cart/src/snd.c (the slave SH2's sound) beside the reference
static void sound_cmd(u8 d) {
    double t = now_cycles();
    if (t - snd_last_t > 5000 && d != 0x80 && d != 0x00 && snd_q.size() < 8) snd_q.push_back(d);
    snd_last_t = t;
}
static void mapper_write(u32 offset, u8 data) {   // MAME: sega_315_5195_mapper_device::write
    offset &= 0x1f;
    u8 oldval = mregs[offset];
    mregs[offset] = data;
    if (getenv("SH_MAPLOG") && offset != 3) fprintf(stderr, "f%ld mapper w %02x = %02x pc %06x\n", frame_no, offset, data, m68k_get_reg(nullptr, M68K_REG_PPC));
    switch (offset) {
        case 0x02:
            if ((oldval ^ data) & 3) {
                main_halted = (data & 3) == 3;
                if (log_io) fprintf(stderr, "f%ld mapper reg2 = %02x (main reset %s)\n", frame_no, data, main_halted ? "on" : "off");
                if (main_halted) m68k_end_timeslice();
            }
            break;
        case 0x03: { to_sound = data; sound_cmd(data); int e = latch_entry(data); latch_bytes.push_back(e); if (sndc_on) { snd_event(e & 0x3ff, e >> 16); lq_log(e); }
            static const bool filt = getenv("SH_SNDFILT") != nullptr;   // (the comparison: the reference without the tunes too)
            sh_sound_latch(filt && (e >> 16) >= 250 && snd_is_music(data) ? 0x80 : data, (long)(m68k_cycles_run() * 0.4)); } if (snd_log) printf("SND f%ld %02x pc %06x line %d cyc %d\n", frame_no, data, m68k_get_reg(nullptr, M68K_REG_PPC), cur_line, m68k_cycles_run()); break;
        case 0x04:
            if ((data & 7) != 7) { hold_irq = ~data & 7; main_set_irq(); }
            break;
        case 0x05:
            if (data == 0x01) {
                u32 addr = (mregs[0x0a] << 17) | (mregs[0x0b] << 9) | (mregs[0x0c] << 1);
                main_write16(addr & 0xffffff, (mregs[0x00] << 8) | mregs[0x01], 0xffff);
            } else if (data == 0x02) {
                u32 addr = (mregs[0x07] << 17) | (mregs[0x08] << 9) | (mregs[0x09] << 1);
                u16 r = main_read16(addr & 0xffffff);
                mregs[0x00] = r >> 8; mregs[0x01] = r;
            }
            break;
        default:
            if (offset >= 0x10 && oldval != data) update_mapping();
            break;
    }
}

// MAME: segaic16_road_device::segaic16_road_control_0_r / _w
static u16 road_control_r() {
    for (int i = 0; i < 0x800; i++) std::swap(roadram[i], roadbuf[i]);
    return 0xffff;
}
static void road_control_w(u16 d, u16 mask) { if (mask & 0x00ff) road_control = d & 3; }

// MAME: segaorun_state::shangon_custom_io_r / _w
static u16 misc_io_r(u32 off) {
    off = (off >> 1) & (0x303f / 2);
    switch (off) {
        case 0x1000 / 2: return in_service;
        case 0x1002 / 2: return in_unknown;
        case 0x1004 / 2: return in_coinage;
        case 0x1006 / 2: return in_dsw;
        case 0x3020 / 2: return adc[adc_select];
    }
    if (log_io) fprintf(stderr, "f%ld misc_io_r %04x\n", frame_no, off * 2);
    return open_bus_r();
}
static void misc_io_w(u32 off, u16 d, u16 mask) {
    off = (off >> 1) & (0x303f / 2);
    switch (off) {
        case 0x0000 / 2:
            if (mask & 0x00ff) {
                adc_select = (d >> 6) & 3;
                if (display_enable != ((d >> 5) & 1)) request_partial();
                display_enable = (d >> 5) & 1;
            }
            return;
        case 0x0020 / 2: { static const bool rl = getenv("SH_RSTLOG") != nullptr; if (rl) printf("RST %04x %.9f f%ld line %d\n", d, now_cycles() / 1e7, frame_no, cur_line); }
            if (mask & 0x00ff) { int e = latch_entry(0x200 | ((d & 1) << 8)); latch_bytes.push_back(e); if (sndc_on) { snd_event(e & 0x3ff, e >> 16); lq_log(e); } }
            if (mask & 0x00ff) sh_sound_reset(!(d & 1), (long)(m68k_cycles_run() * 0.4)); return;   // D0: 0 holds the sound board in reset
        case 0x3000 / 2: return;          // watchdog
        case 0x3020 / 2: return;          // ADC start
    }
    if (log_io) fprintf(stderr, "f%ld misc_io_w %04x = %04x & %04x\n", frame_no, off * 2, d, mask);
}

// SH_ROMHIST=<file>: the main CPU's ROM reads a 1 KB block, over the whole run (docs/10: what goes in which memory)
static long romhist[0x180];
static u16 main_read16(u32 a) {
    a &= 0xfffffe;
    u32 off;
    const MapEnt* e = map_find(a, off, false);
    if (!e) return 0xff00 | mapper_read(a >> 1);
    switch (e->kind) {
        case K_RAM:    if (shared_log && e->ram == cpu1ram) shared_note(0, (off >> 1) % e->words); return e->ram[(off >> 1) % e->words];
        case K_ROM:    romhist[(off >> 10) % 0x180]++; return (rom_main[off] << 8) | rom_main[off + 1];
        case K_SUBROM: return off < 0x60000 ? (rom_sub[off] << 8) | rom_sub[off + 1] : 0xffff;
        case K_ROADCTL: return road_control_r();
        case K_MISCIO: return misc_io_r(off);
    }
    return 0xffff;
}

static void main_write16(u32 a, u16 d, u16 mask) {
    a &= 0xfffffe;
    u32 off;
    const MapEnt* e = map_find(a, off, true);
    if (!e) { if (mask & 0x00ff) mapper_write(a >> 1, d & 0xff); return; }
    switch (e->kind) {
        case K_RAM: {
            u32 wi = (off >> 1) % e->words;
            if (shared_log && e->ram == cpu1ram) shared_note(1, wi);
            if (e->ram == textram && wi >= 0xe80 / 2) request_partial();
            u16& w = e->ram[wi]; w = (w & ~mask) | (d & mask); return;
        }
        case K_ROM: case K_SUBROM: return;
        case K_ROADCTL: road_control_w(d, mask); return;
        case K_MISCIO: { static const bool rl = getenv("SH_RSTLOG") != nullptr; if (rl && ((off >> 1) & 0x181f) == 0x10) printf("IOW %06x\n", a); } misc_io_w(off, d, mask); return;
    }
}

// MAME: segaorun_state::sub_map (global mask 0xfffff, unmapped reads high)
// SH_SUBRELOC=<image> (tools/sub/relocate.py): the sub 68000 runs the relocated program as the Genesis' 68000 would -
// its world in a 1 MB window at SUB_BASE; any other address (but the vectors) is logged as a stray and reads 0xFFFF
static long sub_strays;
static int sub_vec_fetch, sub_low_mode;
static u8  sub_code[0x1c00];           // SH_SUBRELOC=<prefix> with <prefix>.code: the code's RAM copy at code_base
static u32 code_base;
static bool sub_window(u32& a, bool write) {
    if (!sub_base) { a &= 0xffffe; return true; }
    a &= 0xfffffe;
    if (code_base && a >= code_base && a < code_base + sizeof sub_code) { a = 0x400000 | (a - code_base); return true; }
    if (a >= sub_base && a < sub_base + 0xa0000) { a -= sub_base; return true; }
    if (a < 0x400 && !write) {
        // the vector table: a vector fetch (after an interrupt's acknowledge, or the reset) gets the handler moved by
        // the base (the Genesis shim's job); any other read sees what the Genesis would: SH_SUBLOW=orig the arcade's
        // ROM (as the board), =ff 0xFFFF
        // an exception's vector: after an interrupt's acknowledge or at the reset (sub_vec_fetch), or in a TRAP,
        // CHK, TRAPV, a division, the illegal and line A / F instructions
        unsigned ir = m68k_get_reg(nullptr, M68K_REG_IR);
        bool exc_ir = (ir & 0xfff0) == 0x4e40 || (ir & 0xf1c0) == 0x4180 || ir == 0x4e76 || (ir & 0xf0c0) == 0x80c0 ||
                      (ir & 0xf000) == 0xa000 || (ir & 0xf000) == 0xf000 || ir == 0x4afc;
        if (sub_vec_fetch || exc_ir) { a = 0x100000 | a; return true; }
        if (sub_low_mode == 1) { a = 0x200000 | a; return true; }
        if (sub_strays++ < 50) fprintf(stderr, "f%ld sub low read %06x pc %06x\n", frame_no, a, m68k_get_reg(nullptr, M68K_REG_PPC));
        a = 0x300000; return true;
    }
    if (sub_strays++ < 50) fprintf(stderr, "f%ld sub stray %s %06x pc %06x\n", frame_no, write ? "write" : "read", a, m68k_get_reg(nullptr, M68K_REG_PPC));
    return false;
}
// SH_SUBACC=1: the sub CPU's accesses a frame by kind (opcode/ROM data, RAM, road RAM, road control) - what would cross
// the cart bus if its program ran from the Genesis' RAM (docs/08)
static long subacc[6]; static bool subacc_on;
static u16 sub_read16(u32 a) {
    if (!sub_window(a, false)) return 0xffff;
    if (subacc_on) { u32 b = a & 0xffffe; subacc[b < 0x60000 ? ((b >= 0x400 && b < 0x1b00) ? 0 : 1) : b < 0x80000 ? 2 : b < 0x90000 ? 3 : 4]++; }
    if (a & 0x400000) { u32 o = a & 0x3ffe; return (sub_code[o] << 8) | sub_code[o + 1]; }
    if (a & 0x300000) {
        u32 o = a & 0x3fe;
        if ((a & 0x300000) == 0x300000) return 0xffff;
        u16 v = (rom_sub[o] << 8) | rom_sub[o + 1];
        if ((a & 0x300000) == 0x100000) {        // a vector fetch: two words, the long + the base
            u32 l = ((rom_sub[o & ~3u] << 24) | (rom_sub[(o & ~3u) + 1] << 16) | (rom_sub[(o & ~3u) + 2] << 8) | rom_sub[(o & ~3u) + 3]) ;
            l += (code_base && l < 0x1a9e) ? code_base : sub_base;     // as relocate.py's moved(): the code's or the window's
            v = (o & 2) ? (l & 0xffff) : (l >> 16);
            if ((o & 2) && sub_vec_fetch == 1) sub_vec_fetch = 0;
        }
        return v;
    }
    if (a < 0x60000) return (rom_subcpu[a] << 8) | rom_subcpu[a + 1];
    if (a < 0x80000) return cpu1ram[(a & 0x7fff) >> 1];
    if (a < 0x90000) return roadram[(a & 0xfff) >> 1];
    if (a < 0xa0000) return road_control_r();
    return 0xffff;
}
static FILE* sub_wlog;                 // SH_WLOG=<file>: every write of the sub 68000 (frame, pc, address, data, mask)
static void sub_write16(u32 a, u16 d, u16 mask) {
    if (!sub_window(a, true)) return;
    if (subacc_on) { u32 b = a & 0xffffe; subacc[b < 0x80000 ? 2 : b < 0x90000 ? 3 : 4]++; subacc[5]++; }
    if (sub_wlog) fprintf(sub_wlog, "%ld %06x %05x %04x %04x\n", frame_no, (m68k_get_reg(nullptr, M68K_REG_PPC) - sub_base) & 0xfffff, a, d, mask);
    u16* w = nullptr;
    if (a & 0x400000) {
        u32 o = a & 0x3ffe; u16 v = ((sub_code[o] << 8) | sub_code[o + 1]) & ~mask; v |= d & mask;
        sub_code[o] = v >> 8; sub_code[o + 1] = v & 0xff; return;
    }
    if (a < 0x60000) return;
    else if (a < 0x80000) w = &cpu1ram[(a & 0x7fff) >> 1];
    else if (a < 0x90000) w = &roadram[(a & 0xfff) >> 1];
    else if (a < 0xa0000) { road_control_w(d, mask); return; }
    else return;
    *w = (*w & ~mask) | (d & mask);
}

// Musashi's bus
static u16 rd16(u32 a) {
    if (mapents.empty() && !cur_cpu) return a < 0x60000 ? (rom_main[a & ~1u] << 8) | rom_main[(a & ~1u) + 1] : 0xffff;
    return cur_cpu ? sub_read16(a) : main_read16(a);
}
static void wr16(u32 a, u16 d, u16 m) { if (cur_cpu) sub_write16(a, d, m); else main_write16(a, d, m); }
extern "C" {
unsigned int m68k_read_memory_8(unsigned int a)  { u16 w = rd16(a & ~1); return (a & 1) ? (w & 0xff) : (w >> 8); }
unsigned int m68k_read_memory_16(unsigned int a) { return rd16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { u32 h = rd16(a); return (h << 16) | rd16(a + 2); }
void m68k_write_memory_8(unsigned int a, unsigned int d) {
    if (a & 1) wr16(a & ~1, d & 0xff, 0x00ff); else wr16(a & ~1, (d & 0xff) << 8, 0xff00);
}
void m68k_write_memory_16(unsigned int a, unsigned int d) { wr16(a, d, 0xffff); }
void m68k_write_memory_32(unsigned int a, unsigned int d) { wr16(a, d >> 16, 0xffff); wr16(a + 2, d & 0xffff, 0xffff); }
unsigned int m68k_read_disassembler_8(unsigned int a)  { return m68k_read_memory_8(a); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return m68k_read_memory_16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return m68k_read_memory_32(a); }

int sh_int_ack(int level) {
    if (cur_cpu == 1) sub_vec_fetch = 1;
    if (cur_cpu == 0 && hold_irq == level) { hold_irq = 0; main_set_irq(); }
    return M68K_INT_ACK_AUTOVECTOR;
}
void sh_reset_out(void) { if (cur_cpu == 0) sub_reset_pending = 1; }   // MAME: m68k_reset_callback
// event cover: every instruction address each CPU executes (cover0.bin / cover1.bin, a byte per address)
static u8* cover[2];
static long trace_pc = -1;               // SH_TRACEPC=<hex>: the sub's registers each time it reaches that (unrelocated) pc
void sh_instr_hook(unsigned int pc) {
    if (cover[cur_cpu]) cover[cur_cpu][pc & 0xfffff] = 1;
    static unsigned last_pc[2]; static int outside;
    if (cur_cpu == 1 && getenv("SH_LOWLOG") && ((pc - sub_base) & 0xfffff) < 0x1000 && ((last_pc[1] - sub_base) & 0xfffff) >= 0x1000)
        fprintf(stderr, "f%ld sub enters %06x from %06x\n", frame_no, pc, last_pc[1]);
    if (cur_cpu == 1 && sub_base && !(code_base && pc >= code_base && pc < code_base + sizeof sub_code) &&
        !(pc >= sub_base && pc < sub_base + 0xa0000) && outside++ < 5)
        fprintf(stderr, "f%ld sub runs at %06x (before: %06x)\n", frame_no, pc, last_pc[1]);
    last_pc[cur_cpu] = pc;
    if (cur_cpu == 1 && trace_pc >= 0 && ((pc - sub_base) & 0xfffff) == (unsigned)trace_pc) {
        printf("T f%ld pc %06x", frame_no, pc);
        for (int r = 0; r < 16; r++) printf(" %08x", m68k_get_reg(nullptr, (m68k_register_t)(M68K_REG_D0 + r)));
        printf("\n");
    }
}
}

// ------------------------------------------------------------------------------------------------ the two CPUs
static std::vector<u8> ctx[2];
static u32 (*pc_hist)[0x100000];        // event pcprof:N: where each CPU is at the end of each slice, from frame N
static long pcprof_from;
static bool busy_log; static long busy_n[2], busy_b[2];
static double debt[2];

static int main_level() {           // MAME: segaorun_state::update_main_irqs, plus the mapper's HOLD line
    int l = 0;
    if (irq2_state) l = 2;
    if (vblank_state) l = 4;
    if (vblank_state && irq2_state) l = 6;
    if (hold_irq > l) l = hold_irq;
    return l;
}
static void main_set_irq() { if (cur_cpu == 0) m68k_set_irq(main_level()); }

static void run_cpu(int c, double cycles) {
    cur_cpu = c;
    m68k_set_context(ctx[c].data());
    if (c == 1 && sub_reset_pending) { sub_vec_fetch = 2; m68k_pulse_reset(); sub_vec_fetch = 0; sub_reset_pending = 0; }
    m68k_set_irq(c == 0 ? main_level() : (sub_irq4 ? 4 : 0));
    debt[c] += cycles;
    if (c == 0 && (main_halted || main_paused)) debt[c] = 0;
    else if (debt[c] > 0) { int r = m68k_execute((int)ceil(debt[c])); debt[c] -= r; if (c == 0) main_done += r; }
    if (busy_log) {     // the slices each CPU ends outside its wait for the vblank flag (0x60400): main 0x149c-0x14a3, sub 0x10ca-0x10d1
        u32 pc = m68k_get_reg(nullptr, M68K_REG_PC);
        bool idle = c == 0 ? (pc >= 0x149c && pc < 0x14a4) : (pc >= 0x10ca && pc < 0x10d2);
        busy_n[c]++; if (!idle) busy_b[c]++;
    }
    if (pc_hist && frame_no >= pcprof_from) pc_hist[c][m68k_get_reg(nullptr, M68K_REG_PC) & 0xfffff]++;
    m68k_get_context(ctx[c].data());
}

static bool external_sub;              // the library (sh_board.h): the sub CPU is outside (the Genesis' 68000)
static long picture_count;
// the sound board (host/sh_sound.cpp): its Z80 at 4 MHz beside the 10 MHz 68000s, a slice at a time; its SegaPCM's
// samples kept for SH_WAV=<file> (31,250 Hz stereo, the voices' sum clipped: MAME's -wavwrite of the PCM alone, measured)
static std::vector<int16_t> audio; static double z80_debt; static bool audio_on, sound_ready;
static void run_sound(double main_cycles) {
    if (sndc_on) {                           // a tick every 8 ms of the main CPU's time (80,000 cycles)
        static double acc; static FILE* f; static int32_t buf[500];
        if (!f) { f = fopen(getenv("SH_SNDC"), "wb"); static const char h[44] = {0}; fwrite(h, 1, 44, f); }
        acc += main_cycles;
        while (acc >= 80000) {
            acc -= 80000; snd_tick(buf, 250, 1); lq_log(-1);
            static long nt; if (getenv("SH_SNDDBG") && ++nt % 375 == 0) snd_debug();
            for (int i = 0; i < 250; i++) { int v = (buf[2 * i] + buf[2 * i + 1]) / 2; int16_t o = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); fwrite(&o, 2, 1, f); }
            fflush(f);
        }
    }
    if (!sound_ready) return;
    z80_debt += main_cycles * 0.4;
    long c = (long)z80_debt; if (c <= 0) return;
    z80_debt -= c;
    static int16_t buf[2 * 4096];
    int n = sh_sound_run(c, buf, 4096);
    if (audio_on) for (int i = 0; i < 2 * n; i++) audio.push_back(buf[i]);
}
static void run_both(double cycles) {
    if (external_sub) { run_cpu(0, cycles); run_sound(cycles); return; }
    static const double chunk = getenv("SH_CHUNK") ? atof(getenv("SH_CHUNK")) : 160;  // MAME: 1/6000 s
    while (cycles > 0) {
        double c = std::min(chunk, cycles);
        run_cpu(0, c);
        run_cpu(1, c);
        run_sound(c);
        cycles -= c;
    }
}

// ------------------------------------------------------------------------------------------------ video
static u8 tilepix[4096][64];
static u8 roadgfx[(256 * 2 + 1) * 512];
static u8 pal_normal[32], pal_shadow[32], pal_hilight[32];
static u16 bitmap[224][320];          // palette indexes 0-8191, kept across frames as MAME's screen bitmap
static u8 pri[224][320];
static u16 sprbmp[224][512];
static int gy0 = 0, gy1 = 223;        // the lines being drawn (MAME's cliprect of a partial update)
// measurements (docs/02): sprites drawn, pixels written, sprite ROM words read, per frame and the worst line
static int st_spr, st_pix, st_words, st_linewords[224], st_linepix[224];
static bool stats;

// MAME: compute_resistor_weights (resnet.cpp, Couriersud), one network, no pull-up/down, autoscale
static void resistor_weights(int count, const int* res, double* w) {
    double ws[8], sum = 0;
    for (int n = 0; n < count; n++) {
        double R0 = 1.0 / 1e12, R1 = 1.0 / 1e12;
        for (int j = 0; j < count; j++) {
            if (j == n) { if (res[j] != 0) R1 += 1.0 / res[j]; }
            else if (res[j] != 0) R0 += 1.0 / res[j];
        }
        R0 = 1.0 / R0; R1 = 1.0 / R1;
        double v = 255.0 * R0 / (R1 + R0);
        ws[n] = std::clamp(v, 0.0, 255.0);
        sum += ws[n];
    }
    double scale = 255.0 / sum;
    for (int n = 0; n < count; n++) w[n] = ws[n] * scale;
}

static void video_init() {
    // MAME: sega_16bit_common_base::palette_init
    static const int rn[6] = { 3900, 2000, 1000, 1000 / 2, 1000 / 4, 0 };
    static const int rs[6] = { 3900, 2000, 1000, 1000 / 2, 1000 / 4, 470 };
    double wn[6], wsh[6];
    resistor_weights(6, rn, wn);
    resistor_weights(6, rs, wsh);
    for (int v = 0; v < 32; v++) {
        int b[5]; for (int i = 0; i < 5; i++) b[i] = (v >> i) & 1;
        double n = 0, s = 0;
        for (int i = 0; i < 5; i++) { n += wn[i] * b[i]; s += wsh[i] * b[i]; }
        pal_normal[v] = (u8)(int)(n + 0.5);
        pal_shadow[v] = (u8)(int)(s + 0.5);
        pal_hilight[v] = (u8)(int)(s + wsh[5] + 0.5);
    }
    // gfx_8x8x3_planar: planes RGN_FRAC(2,3), (1,3), (0,3), MSB first
    for (int t = 0; t < 4096; t++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x++) {
                int bit = 7 - x;
                int p = (((gfx1[0x10000 + t * 8 + y] >> bit) & 1) << 2) |
                        (((gfx1[0x08000 + t * 8 + y] >> bit) & 1) << 1) |
                        (((gfx1[0x00000 + t * 8 + y] >> bit) & 1) << 0);
                tilepix[t][y * 8 + x] = p;
            }
    // MAME: segaic16_road_device::segaic16_road_outrun_decode
    const int len = 0x10000;
    for (int y = 0; y < 256 * 2; y++) {
        const u8* src = roadrom + ((y & 0xff) * 0x40 + (y >> 8) * 0x8000) % len;
        u8* dst = roadgfx + y * 512;
        for (int x = 0; x < 512; x++) {
            dst[x] = (((src[x >> 3] >> (~x & 7)) & 1) << 0) | (((src[(x >> 3) + 0x4000] >> (~x & 7)) & 1) << 1);
            if (x >= 256 - 8 && x < 256 && dst[x] == 3) dst[x] |= 4;
        }
    }
    memset(roadgfx + 256 * 2 * 512, 3, 512);
}

// MAME: segaic16_video_device::tilemap_16b_latch_values / tilemap_16b_fill_latch (scanline 261)
static void latch_tilemap() {
    for (int i = 0; i < 4; i++) {
        latched_page[i] = textram[0xe80 / 2 + i];
        latched_ys[i]   = textram[0xe90 / 2 + i];
        latched_xs[i]   = textram[0xe98 / 2 + i];
    }
}

// MAME: tilemap_16b_draw_layer + draw_virtual_tilemap + tilemap_16b_alt_tile_info + tilemap_t::draw
// (one category, transparent pen 0, the priority bitmap OR-ed with the mark), flip off
static void draw_layer(int which, int category, u8 mark) {
    u16 xscroll = latched_xs[which], yscroll = latched_ys[which], pages = latched_page[which];
    for (int y = gy0; y <= gy1; y++) {
        u16 rowscroll = textram[0xf80 / 2 + 0x40 / 2 * which + y / 8];
        for (int x = 0; x < 320; x++) {
            u16 effx, effy, effpages = pages;
            effx = (xscroll & 0x8000) ? rowscroll : xscroll;
            if (yscroll & 0x8000) effy = textram[0xf16 / 2 + 0x40 / 2 * which + (x + 8) / 16];
            else effy = yscroll;
            if (rowscroll & 0x8000) {
                effx = latched_xs[which + 2]; effy = latched_ys[which + 2]; effpages = latched_page[which + 2];
            }
            effx = (0xc0 - effx + 0) & 0x3ff;
            effy = effy & 0x1ff;
            u32 X = (x + effx) & 0x3ff, Y = (y + effy) & 0x1ff;
            int page = (effpages >> (4 * (((Y >> 8) & 1) * 2 + ((X >> 9) & 1)))) & 0xf;
            u16 data = tileram[page * 64 * 32 + ((Y & 255) >> 3) * 64 + ((X & 511) >> 3)];
            if (((data >> 15) & 1) != category) continue;
            int color = (data >> 5) & 0x7f;
            int code = (data & 0x1fff) % 4096;
            int pen = tilepix[code][(Y & 7) * 8 + (X & 7)];
            if (pen == 0) continue;
            bitmap[y][x] = color * 8 + pen;
            pri[y][x] |= mark;
        }
    }
}

// MAME: tilemap_16b_alt_text_info, the text tilemap 64x28 with scrolldx -192
static void draw_text(int category, u8 mark) {
    for (int y = gy0; y <= gy1; y++)
        for (int x = 0; x < 320; x++) {
            int X = (x + 192) & 511;
            u16 data = textram[(y >> 3) * 64 + (X >> 3)];
            if (((data >> 15) & 1) != category) continue;
            int color = (data >> 8) & 7;
            int code = data & 0xff;
            int pen = tilepix[code][(y & 7) * 8 + (X & 7)];
            if (pen == 0) continue;
            bitmap[y][x] = color * 8 + pen;
            pri[y][x] |= mark;
        }
}

// MAME: segaic16_road_outrun_draw, colorbases 0x7f6 / 0x7c0 / 0x7c0, xoffs 0 (shangon video_start)
static void draw_road(bool background) {
    const int colorbase1 = 0x7f6, colorbase2 = 0x7c0, colorbase3 = 0x7c0, xoffs = 0;
    static const u8 priority_map[2][8] = { { 0x80,0x81,0x81,0x87,0,0,0,0x00 }, { 0x81,0x81,0x81,0x8f,0,0,0,0x80 } };
    const u16* rr = roadbuf;
    for (int y = gy0; y <= gy1; y++) {
        u16* dest = bitmap[y];
        const u16 data0 = rr[0x000 + y], data1 = rr[0x100 + y];
        if (background) {
            int color = -1;
            switch (road_control & 3) {
                case 0: if (data0 & 0x800) color = data0 & 0x7f; break;
                case 1: if (data0 & 0x800) color = data0 & 0x7f; else if (data1 & 0x800) color = data1 & 0x7f; break;
                case 2: if (data1 & 0x800) color = data1 & 0x7f; else if (data0 & 0x800) color = data0 & 0x7f; break;
                case 3: if (data1 & 0x800) color = data1 & 0x7f; break;
            }
            if (color != -1) { color |= colorbase3; for (int x = 0; x < 320; x++) dest[x] = color; }
            continue;
        }
        const u8 control = road_control & 3;
        u16 ct[32];
        if ((data0 & 0x800) && (data1 & 0x800)) continue;
        const u8* src0 = (data0 & 0x800) ? roadgfx + 256 * 2 * 512 : roadgfx + (0x000 + ((data0 >> 1) & 0xff)) * 512;
        int hpos0 = rr[0x200 + ((road_control & 4) ? y : (data0 & 0x1ff))] & 0xfff;
        int color0 = rr[0x600 + ((road_control & 4) ? y : (data0 & 0x1ff))];
        const u8* src1 = (data1 & 0x800) ? roadgfx + 256 * 2 * 512 : roadgfx + (0x100 + ((data1 >> 1) & 0xff)) * 512;
        int hpos1 = rr[0x400 + ((road_control & 4) ? (0x100 + y) : (data1 & 0x1ff))] & 0xfff;
        int color1 = rr[0x600 + ((road_control & 4) ? (0x100 + y) : (data1 & 0x1ff))];
        ct[0x00] = colorbase1 ^ 0x00 ^ ((color0 >> 0) & 1);
        ct[0x01] = colorbase1 ^ 0x02 ^ ((color0 >> 1) & 1);
        ct[0x02] = colorbase1 ^ 0x04 ^ ((color0 >> 2) & 1);
        u8 bg = (color0 >> 8) & 0xf;
        ct[0x03] = (data0 & 0x200) ? ct[0x00] : (colorbase2 ^ 0x00 ^ bg);
        ct[0x07] = colorbase1 ^ 0x06 ^ ((color0 >> 3) & 1);
        ct[0x10] = colorbase1 ^ 0x08 ^ ((color1 >> 4) & 1);
        ct[0x11] = colorbase1 ^ 0x0a ^ ((color1 >> 5) & 1);
        ct[0x12] = colorbase1 ^ 0x0c ^ ((color1 >> 6) & 1);
        bg = (color1 >> 8) & 0xf;
        ct[0x13] = (data1 & 0x200) ? ct[0x10] : (colorbase2 ^ 0x10 ^ bg);
        ct[0x17] = colorbase1 ^ 0x0e ^ ((color1 >> 7) & 1);
        hpos0 = (hpos0 - (0x5f8 + xoffs)) & 0xfff;
        hpos1 = (hpos1 - (0x5f8 + xoffs)) & 0xfff;
        for (int x = 0; x < 320; x++) {
            const int pix0 = (hpos0 < 0x200) ? src0[hpos0] : 3;
            const int pix1 = (hpos1 < 0x200) ? src1[hpos1] : 3;
            switch (control) {
                case 0: if (!(data0 & 0x800)) dest[x] = ct[0x00 + pix0]; break;
                case 1: dest[x] = ((priority_map[0][pix0] >> pix1) & 1) ? ct[0x10 + pix1] : ct[0x00 + pix0]; break;
                case 2: dest[x] = ((priority_map[1][pix0] >> pix1) & 1) ? ct[0x10 + pix1] : ct[0x00 + pix0]; break;
                case 3: if (!(data1 & 0x800)) dest[x] = ct[0x10 + pix1]; break;
            }
            hpos0 = (hpos0 + 1) & 0xfff;
            hpos1 = (hpos1 + 1) & 0xfff;
        }
    }
}

// MAME: sega_sys16b_sprite_device::draw (origin 184, 0; numbanks 8; no flip); it writes back to sprite RAM
static void draw_sprites() {
    for (int y = gy0; y <= gy1; y++) for (int x = 0; x < 512; x++) sprbmp[y][x] = 0xffff;
    if (gy0 == 0) { st_spr = st_pix = st_words = 0; memset(st_linewords, 0, sizeof st_linewords); memset(st_linepix, 0, sizeof st_linepix); }
    const int numbanks = 8;
    for (u16* data = spriteram; data < spriteram + 0x800; data += 8) {
        if (data[2] & 0x8000) break;
        int bottom = data[0] >> 8, top = data[0] & 0xff;
        int xpos = data[1] & 0x1ff;
        int hide = data[2] & 0x4000, flip = data[2] & 0x100;
        int pitch = (int8_t)(data[2] & 0xff);
        u16 addr = data[3];
        int bank = (data[4] >> 8) & 0xf;
        int colpri = ((data[4] & 0xff) << 4) | (((data[1] >> 9) & 0xf) << 12);
        int vzoom = (data[5] >> 5) & 0x1f, hzoom = data[5] & 0x1f;
        data[7] = addr;
        if (hide || top >= bottom || bank == 255) continue;
        bank %= numbanks;
        const u16* spritedata = sprrom + 0x10000 * bank;
        data[5] &= 0x03ff;
        if (gy0 == 0) st_spr++;
        const int xdelta = 1;
        for (int y = top; y < bottom; y++) {
            addr += pitch;
            data[5] += vzoom << 10;
            if (data[5] & 0x8000) { addr += pitch; data[5] &= ~0x8000; }
            if (y < gy0 || y > gy1) continue;
            u16* dest = sprbmp[y];
            int x, xacc = 4 * hzoom, pix = 0;
            auto put = [&](int p) {
                xacc = (xacc & 0x3f) + hzoom;
                if (xacc < 0x40) {
                    if (x >= 184 && x < 184 + 320 && p != 0 && p != 15) { dest[x - 184] = colpri | p; st_pix++; st_linepix[y]++; }
                    x += xdelta;
                }
            };
            if (!flip) {
                data[7] = addr - 1;
                for (x = xpos; ((xpos - x) & 0x1ff) != 1; ) {
                    u16 pixels = spritedata[++data[7]]; st_words++; st_linewords[y]++;
                    pix = (pixels >> 12) & 0xf; put(pix);
                    pix = (pixels >> 8) & 0xf;  put(pix);
                    pix = (pixels >> 4) & 0xf;  put(pix);
                    pix = (pixels >> 0) & 0xf;  put(pix);
                    if (pix == 15) break;
                }
            } else {
                data[7] = addr + 1;
                for (x = xpos; ((xpos - x) & 0x1ff) != 1; ) {
                    u16 pixels = spritedata[--data[7]]; st_words++; st_linewords[y]++;
                    pix = (pixels >> 0) & 0xf;  put(pix);
                    pix = (pixels >> 4) & 0xf;  put(pix);
                    pix = (pixels >> 8) & 0xf;  put(pix);
                    pix = (pixels >> 12) & 0xf; put(pix);
                    if (pix == 15) break;
                }
            }
        }
    }
}

// MAME: segaorun_state::screen_update_shangon, for lines gy0-gy1
static void screen_update() {
    draw_sprites();
    for (int y = gy0; y <= gy1; y++) memset(pri[y], 0, sizeof(pri[y]));
    draw_road(true);
    draw_layer(1, 0, 0x01); draw_layer(1, 1, 0x02);
    draw_layer(0, 0, 0x02); draw_layer(0, 1, 0x04);
    draw_road(false);
    draw_text(0, 0x08); draw_text(1, 0x08);
    for (int y = gy0; y <= gy1; y++)
        for (int x = 0; x < 320; x++) {
            u16 pix = sprbmp[y][x];
            if (pix == 0xffff) continue;
            int priority = (pix >> 10) & 3;
            if ((1 << priority) > pri[y][x]) {
                if ((pix & 0x03f0) == 0x03f0) bitmap[y][x] += 0x1000;
                else bitmap[y][x] = 0x400 | (pix & 0x3ff);
            }
        }
}

// MAME: sega_16bit_common_base::paletteram_w - the colour of an index 0-8191
static void pen_rgb(u16 idx, u8* rgb) {
    u16 v = paletteram[idx & 0xfff];
    u8 r = ((v >> 12) & 0x01) | ((v << 1) & 0x1e);
    u8 g = ((v >> 13) & 0x01) | ((v >> 3) & 0x1e);
    u8 b = ((v >> 14) & 0x01) | ((v >> 7) & 0x1e);
    if (idx < 0x1000) { rgb[0] = pal_normal[r]; rgb[1] = pal_normal[g]; rgb[2] = pal_normal[b]; }
    else if (v & 0x8000) { rgb[0] = pal_hilight[r]; rgb[1] = pal_hilight[g]; rgb[2] = pal_hilight[b]; }
    else { rgb[0] = pal_shadow[r]; rgb[1] = pal_shadow[g]; rgb[2] = pal_shadow[b]; }
}

// the colours are taken when the picture is drawn (line 224), with the palette of that moment
static u8 rgbframe[224][320][3];
static long idx_frames, idx_max, idx_sum, idx_over;   // SH_IDXSTAT: distinct palette indexes a picture (docs/10)
static u16 pal224[0x1000];            // the palette the colours were taken with (wdump)
static void take_rgb() {
    memcpy(pal224, paletteram, sizeof pal224);
    for (int y = 0; y < 224; y++) for (int x = 0; x < 320; x++) pen_rgb(bitmap[y][x], rgbframe[y][x]);
    static const bool on = getenv("SH_IDXSTAT") != nullptr;
    if (on) {
        static u8 seen[0x2000]; memset(seen, 0, sizeof seen); long n = 0;
        for (int y = 0; y < 224; y++) for (int x = 0; x < 320; x++) { u16 v = bitmap[y][x] & 0x1fff; if (!seen[v]) { seen[v] = 1; n++; } }
        idx_frames++; idx_sum += n; idx_max = std::max(idx_max, n); if (n > 255) idx_over++;
        if (idx_frames % 600 == 0) fprintf(stderr, "idx: %ld pictures, distinct indexes avg %ld max %ld, over 255: %ld\n", idx_frames, idx_sum / idx_frames, idx_max, idx_over);
    }
}
static void write_ppm(const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    fprintf(f, "P6\n320 224\n255\n");
    fwrite(rgbframe, 1, sizeof(rgbframe), f);
    fclose(f);
}

// ------------------------------------------------------------------------------------------------ the frame
// MAME: screen 25.1748 MHz / 4, 400 x 262, visible 320 x 224; segaorun_state::scanline_tick
static const double CYC_LINE = 10000000.0 * 400.0 / (25174800.0 / 4.0);

// MAME: screen_device::update_partial / reset_partial_updates (at line 0). MAME draws the lines up to the beam
// whenever the text RAM's scroll area (0xE80 up) is written or the display is switched (segaic16.cpp textram_w,
// set_display_enable); the rest at the start of vblank (line 224). Each part redraws the sprites over its lines.
static int last_partial;
static bool spr_dump, vid_dump;                 // this frame: write sNNNNN.bin (event sprdump)
static std::string out_dir;
static long st_partials;
static void update_partial(int scanline) {
    if (scanline < last_partial) return;
    int top = std::max(0, last_partial), bot = std::min(223, scanline);
    if (top <= bot && top == 0 && bot == 223 && vid_dump) {
        // the video bench's input (fpga/shfx/sim tb_video): every memory the picture is made of, before the draw,
        // then MAME's bitmap (palette indexes, bit 12 the shadow half)
        char n[64]; snprintf(n, sizeof n, "/v%05ld.bin", frame_no);
        FILE* f = fopen((out_dir + n).c_str(), "wb");
        u16 regs[16] = { latched_page[0], latched_page[1], latched_page[2], latched_page[3],
                         latched_ys[0], latched_ys[1], latched_ys[2], latched_ys[3],
                         latched_xs[0], latched_xs[1], latched_xs[2], latched_xs[3], (u16)road_control, 0, 0, 0 };
        if (f) { fwrite(regs, 2, 16, f); fwrite(tileram, 2, 0x8000, f); fwrite(textram, 2, 0x800, f);
                 fwrite(roadbuf, 2, 0x800, f); fwrite(spriteram, 2, 0x800, f); }
        gy0 = top; gy1 = bot; screen_update(); st_partials++;
        if (f) { for (int y = 0; y < 224; y++) fwrite(bitmap[y], 2, 320, f); fclose(f); }
        last_partial = scanline + 1;
        return;
    }
    if (top <= bot && top == 0 && bot == 223 && spr_dump) {
        // the bench's input (fpga/shfx/sim): the sprite RAM before the draw, then the sprites' line buffers after it
        static u16 pre[0x800]; memcpy(pre, spriteram, sizeof pre);
        gy0 = top; gy1 = bot; screen_update(); st_partials++;
        char n[64]; snprintf(n, sizeof n, "/s%05ld.bin", frame_no);
        FILE* f = fopen((out_dir + n).c_str(), "wb");
        if (f) { fwrite(pre, 2, 0x800, f); for (int y = 0; y < 224; y++) fwrite(sprbmp[y], 2, 320, f); fclose(f); }
        last_partial = scanline + 1;
        return;
    }
    if (top <= bot) { gy0 = top; gy1 = bot; screen_update(); st_partials++; if (getenv("SH_PLOG") && (top > 0 || bot < 223)) fprintf(stderr, "f%ld partial %d-%d\n", frame_no, top, bot); }
    last_partial = scanline + 1;
}

static void apply_inputs(long f);
// event hash: at line 224's start, a hash of what the picture is made of (fpga/shfx/sim tb_main prints the same):
// tile, text, palette and road RAM (the buffer the picture reads), the sprite RAM without what MAME's draw writes back
// into it (word 7, word 5's top 6 bits); then the sub CPU's RAM
static bool hash_log;
static uint64_t fnv(uint64_t h, const u16* p, size_t n, int kind = 0) {
    for (size_t i = 0; i < n; i++) {
        u16 v = p[i];
        if (kind == 1) { if ((i & 7) == 7) v = 0; else if ((i & 7) == 5) v &= 0x3ff; }
        h = (h ^ (v & 0xff)) * 0x100000001b3ULL; h = (h ^ (v >> 8)) * 0x100000001b3ULL;
    }
    return h;
}
static uint64_t state_hash() {
    uint64_t h = 0xcbf29ce484222325ULL;
    h = fnv(h, tileram, 0x8000); h = fnv(h, textram, 0x800); h = fnv(h, paletteram, 0x1000);
    h = fnv(h, roadbuf, 0x800); h = fnv(h, spriteram, 0x800, 1); h = fnv(h, cpu1ram, 0x4000);
    return h;
}
static bool prelude;
static double now_cycles() { return (prelude ? cur_line - 224.0 : 38.0 + (double)frame_no * 262 + cur_line) * CYC_LINE + m68k_cycles_run(); }
static bool partials_on = true;
// SH_SCAN=1 (docs/10): each line drawn at its own start, its colours taken then - as the arcade's hardware (and the
// FPGA) make a line while it is shown - instead of MAME's whole picture at line 224 with its partial updates
static bool scan_mode;
static void request_partial() { if (partials_on && !scan_mode) update_partial(cur_line); }
static void pic_hash_print() {
    static const bool on = getenv("SH_PICHASH") != nullptr;
    if (!on) return;
    uint64_t h = 0xcbf29ce484222325ULL;
    for (int y = 0; y < 224; y++) for (int x = 0; x < 320; x++) for (int c = 0; c < 3; c++) h = (h ^ rgbframe[y][x][c]) * 0x100000001b3ULL;
    printf("P f%ld %016llx\n", frame_no, (unsigned long long)h);
    uint64_t q = 0xcbf29ce484222325ULL;   // the 32X's colours (5 bits a channel: fpga/shfx/sim tb_core's hash)
    for (int y = 0; y < 224; y++) for (int x = 0; x < 320; x++) {
        u16 c = (rgbframe[y][x][0] >> 3) | ((rgbframe[y][x][1] >> 3) << 5) | ((rgbframe[y][x][2] >> 3) << 10);
        q = (q ^ (c & 0xff)) * 0x100000001b3ULL; q = (q ^ (c >> 8)) * 0x100000001b3ULL;
    }
    printf("Q f%ld %016llx\n", frame_no, (unsigned long long)q);
}
// MAME's screen starts at the start of vblank (time 0 is line 224: screen_device's first vblank_start_time is 0), and
// the CPUs with it: the run starts with lines 224-261 (the prelude, no picture and no frame end), then frame 0's
// 262 lines end at its line 224 as MAME's frame_done does. (The board's start in time only matters to the sound
// board: its Z80 runs free of the main's vblank waits; docs/04.)
static void run_line(int line) {
    cur_line = line;
    if (line == 0) last_partial = 0;
    if (scan_mode && line < 224) {
        gy0 = gy1 = line; screen_update();
        for (int x = 0; x < 320; x++) pen_rgb(bitmap[line][x], rgbframe[line][x]);
    }
    if (line == 66 || line == 130 || line == 194) irq2_state = 0;
    if (line == 223) { vblank_state = 1; sub_irq4 = 1; }
    if (line == 224) {
        vblank_state = 0; sub_irq4 = 0;
    }
    if (line == 224 && !prelude) {
        if (hash_log) {
            const uint64_t o = 0xcbf29ce484222325ULL;
            printf("H f%ld %016llx  %08x %08x %08x %08x %08x %08x\n", frame_no, (unsigned long long)state_hash(),
                   (u32)fnv(o, tileram, 0x8000), (u32)fnv(o, textram, 0x800), (u32)fnv(o, paletteram, 0x1000),
                   (u32)fnv(o, roadbuf, 0x800), (u32)fnv(o, spriteram, 0x800, 1), (u32)fnv(o, cpu1ram, 0x4000));
        }
        if (!scan_mode) {
            if (partials_on) update_partial(223);
            else { gy0 = 0; gy1 = 223; screen_update(); }
            take_rgb();
        }
        pic_hash_print();
        static const bool vst = getenv("SH_VSTATE") != nullptr;
        static const long rdump = getenv("SH_RAMCMP") ? atol(getenv("SH_RAMCMP")) : -1;
        if (frame_no == rdump && sndc_on) {
            for (int a = 0xf800; a < 0x10000; a++) { uint8_t x = sh_sound_peek(a), y = snd_peek(a); if (x != y) printf("RAM %04x ref %02x snd %02x\n", a, x, y); }
            for (int a = 0xf000; a < 0xf100; a++) { uint8_t x = sh_sound_peek(a), y = snd_peek(a); if (x != y) printf("PCM %04x ref %02x snd %02x\n", a, x, y); }
        }
        if (vst && frame_no % 62 == 0) { printf("V %ld", frame_no); for (int n = 0; n < 16; n++) printf(" %02x/%02x/%02x", sh_sound_peek(0xf086 + 8 * n), sh_sound_peek(0xf002 + 8 * n), sh_sound_peek(0xf085 + 8 * n)); printf("\n"); }
        picture_count++;
        apply_inputs(frame_no + 1);
        static const long sd = getenv("SH_SPRDUMP") ? atol(getenv("SH_SPRDUMP")) : -1;   // sprite RAM after drawing
        if (frame_no == sd) { FILE* f = fopen("spr_ours.txt", "w"); for (int i = 0; i < 0x800; i++) fprintf(f, "%04x\n", spriteram[i]); fclose(f); }
    }
    if (line == 261) latch_tilemap();
    run_both(CYC_LINE * 320.0 / 400.0);
    if (line == 65 || line == 129 || line == 193) irq2_state = 1;
    run_both(CYC_LINE * 80.0 / 400.0);
}
static void run_frame() { for (int line = 0; line < 262; line++) run_line(line); }

// ------------------------------------------------------------------------------------------------ events
struct Ev { std::string k; long a, b, s; int v; };
static std::vector<Ev> evs;

static void parse_events(const char* s) {
    std::string str(s);
    size_t p = 0;
    while (p < str.size()) {
        size_t q = str.find(',', p); if (q == std::string::npos) q = str.size();
        std::string t = str.substr(p, q - p); p = q + 1;
        Ev e{ "", 0, 0, 1, 0 };
        size_t c = t.find(':');
        e.k = t.substr(0, c);
        if (c != std::string::npos) {
            std::string r = t.substr(c + 1);
            size_t eq = r.find('='); if (eq != std::string::npos) { e.v = strtol(r.c_str() + eq + 1, nullptr, 16); r = r.substr(0, eq); }
            size_t sl = r.find('/'); if (sl != std::string::npos) { e.s = atol(r.c_str() + sl + 1); r = r.substr(0, sl); }
            size_t d = r.find('-');
            e.a = atol(r.c_str()); e.b = (d != std::string::npos) ? atol(r.c_str() + d + 1) : e.a;
        }
        if (e.k == "log") log_io = true;
        else if (e.k == "stats") stats = true;
        else if (e.k == "snd") snd_log = true;
        else if (e.k == "latchlog") sh_sound_latch_log = [](uint8_t d) { printf("L f%ld %02x\n", frame_no, d); };
        else if (e.k == "sndclog") snd_pcm_log = [](uint8_t r, uint8_t d) { printf("C f%ld %02x %02x\n", frame_no, r, d); };
        else if (e.k == "pcmlog") sh_sound_pcm_log = [](uint8_t r, uint8_t d) { printf("P f%ld %02x %02x\n", frame_no, r, d); };
        else if (e.k == "busy") busy_log = true;
        else if (e.k == "hash") hash_log = true;
        else if (e.k == "cover") { cover[0] = (u8*)calloc(1, 0x100000); cover[1] = (u8*)calloc(1, 0x100000); }
        else if (e.k == "pcprof") { pc_hist = (u32(*)[0x100000])calloc(2, sizeof(u32) * 0x100000); pcprof_from = e.a; }
        else if (e.k == "nopartial") partials_on = false;
        else if (e.k == "dsw") in_dsw = (u8)strtol(t.c_str() + c + 1, nullptr, 16);
        else evs.push_back(e);
    }
}

// the inputs of frame f; they change at the start of vblank (line 224) of the frame before, where MAME's
// frame_done (and so a Lua script's, tools/mame_snap.lua) sets them
static void apply_inputs(long f) {
    in_unknown = 0xff; adc[0] = 0x80; adc[1] = 0; adc[2] = 0;
    for (auto& e : evs) {
        if (e.k == "coin" && f >= e.a && f < e.a + 6) in_unknown &= ~0x01;
        if (e.k == "start" && f >= e.a && f < e.a + 6) in_unknown &= ~0x10;
        if (e.k == "gas" && f >= e.a && f <= e.b) adc[1] = 0xff;
        if (e.k == "brake" && f >= e.a && f <= e.b) adc[2] = 0xff;
        if (e.k == "steer" && f >= e.a && f <= e.b) adc[0] = e.v;
    }
}

static bool in_range(const Ev& e, long f) { return f >= e.a && f <= e.b && ((f - e.a) % e.s) == 0; }

#ifndef SHB_LIBRARY
int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: shangon_ref <romdir> <frames> <outdir> [events]\n"); return 1; }
    if (!load_roms(argv[1])) return 1;
    memcpy(rom_subcpu, rom_sub, sizeof rom_sub);
    shared_log = getenv("SH_SHARED") != nullptr;
    scan_mode = getenv("SH_SCAN") != nullptr;
    if (getenv("SH_TRACEPC")) trace_pc = strtol(getenv("SH_TRACEPC"), nullptr, 16);
    if (getenv("SH_WLOG")) sub_wlog = fopen(getenv("SH_WLOG"), "w");
    if (getenv("SH_SUBRELOC")) {
        std::string pre = getenv("SH_SUBRELOC");
        FILE* f = fopen((pre + ".win").c_str(), "rb");
        if (f) {                        // the two-part move (relocate.py): the window's part and the code's RAM copy
            size_t n = fread(rom_subcpu, 1, sizeof rom_subcpu, f); fclose(f);
            f = fopen((pre + ".code").c_str(), "rb"); if (!f) { fprintf(stderr, "no %s.code\n", pre.c_str()); return 1; }
            n = fread(sub_code, 1, sizeof sub_code, f); fclose(f); (void)n;
            code_base = getenv("SH_CODEBASE") ? strtoul(getenv("SH_CODEBASE"), nullptr, 16) : 0xff0000;
        } else {
            f = fopen(pre.c_str(), "rb"); if (!f) { fprintf(stderr, "no %s\n", pre.c_str()); return 1; }
            size_t n = fread(rom_subcpu, 1, sizeof rom_subcpu, f); fclose(f); (void)n;
        }
        sub_low_mode = getenv("SH_SUBLOW") && std::string(getenv("SH_SUBLOW")) == "orig" ? 1 : 0;
        sub_base = getenv("SH_SUBBASE") ? strtoul(getenv("SH_SUBBASE"), nullptr, 16) : 0x900000;
    }
    if (argc >= 5 && std::string(argv[2]) == "dasmcov") {       // shangon_ref <romdir> dasmcov <main|sub> <coverN.bin>
        int sub = std::string(argv[3]) == "sub";
        std::vector<u8> cv(0x100000); FILE* f = fopen(argv[4], "rb"); size_t n = fread(cv.data(), 1, cv.size(), f); fclose(f); (void)n;
        cur_cpu = sub; m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000);
        for (unsigned a = 0; a < 0x100000; a++) if (cv[a]) {
            char buf[128]; unsigned len = m68k_disassemble(buf, a, M68K_CPU_TYPE_68000); printf("%06x  %u  %s\n", a, len, buf);
        }
        return 0;
    }
    if (argc >= 5 && std::string(argv[2]) == "dasm") {          // shangon_ref <romdir> dasm <main|sub> <hex addr> [n]
        int sub = std::string(argv[3]) == "sub";
        unsigned a = strtoul(argv[4], nullptr, 16); int n = argc > 5 ? atoi(argv[5]) : 40;
        cur_cpu = sub; m68k_init(); m68k_set_cpu_type(M68K_CPU_TYPE_68000);
        for (int i = 0; i < n; i++) {
            char buf[128];
            unsigned len = m68k_disassemble(buf, a, M68K_CPU_TYPE_68000);
            printf("%06x  %s\n", a, buf); a += len;
        }
        return 0;
    }
    if (argc >= 4 && std::string(argv[2]) == "render") {
        // shangon_ref <romdir> render <file>...: what fpga/shfx/sim tb_core's TB_DUMP writes at line 224 (the core's
        // latched scroll values, road control and display bit, then its tile, text, road buffer, sprite and palette RAMs)
        // drawn as MAME draws the picture; prints `Q <file> <hash of its 32X colours>` (docs/10)
        video_init();
        for (int i = 3; i < argc; i++) {
            FILE* f = fopen(argv[i], "rb"); if (!f) continue;
            u16 regs[16];
            bool ok = fread(regs, 2, 16, f) == 16 && fread(tileram, 2, 0x8000, f) == 0x8000 && fread(textram, 2, 0x800, f) == 0x800 &&
                      fread(roadbuf, 2, 0x800, f) == 0x800 && fread(spriteram, 2, 0x800, f) == 0x800 && fread(paletteram, 2, 0x1000, f) == 0x1000;
            fclose(f);
            if (!ok) { printf("Q %s short\n", argv[i]); continue; }
            for (int k = 0; k < 4; k++) { latched_page[k] = regs[k]; latched_ys[k] = regs[4 + k]; latched_xs[k] = regs[8 + k]; }
            road_control = regs[12]; display_enable = regs[13] & 1;
            gy0 = 0; gy1 = 223; screen_update(); take_rgb();
            uint64_t q = 0xcbf29ce484222325ULL;
            for (int y = 0; y < 224; y++) for (int x = 0; x < 320; x++) {
                u16 c = (rgbframe[y][x][0] >> 3) | ((rgbframe[y][x][1] >> 3) << 5) | ((rgbframe[y][x][2] >> 3) << 10);
                q = (q ^ (c & 0xff)) * 0x100000001b3ULL; q = (q ^ (c >> 8)) * 0x100000001b3ULL;
            }
            printf("Q %s %016llx\n", argv[i], (unsigned long long)q);
            if (getenv("SH_RENDER_PPM")) write_ppm(std::string(argv[i]) + ".ppm");
        }
        return 0;
    }
    long frames = atol(argv[2]);
    std::string out = argv[3];
    out_dir = out;
    if (argc > 4) parse_events(argv[4]);
    if (getenv("SH_COIN")) in_coinage = (u8)strtol(getenv("SH_COIN"), nullptr, 16);
    video_init();
    sh_sound_init(soundrom, pcmrom); sound_ready = true;
    static u8 pcm128[0x20000]; for (int i = 0; i < 4; i++) memcpy(pcm128 + 0x8000 * i, pcmrom + 0x10000 * i, 0x8000);
    if (getenv("SH_SNDC")) { snd_init(soundrom, pcm128); sndc_on = true; if (getenv("SH_SNDHIST")) snd_pc_hist = (unsigned*)calloc(65536, sizeof(unsigned)); } audio_on = getenv("SH_WAV") != nullptr;

    m68k_init();
    for (int c = 0; c < 2; c++) {
        ctx[c].resize(m68k_context_size());
        cur_cpu = c;
        m68k_set_cpu_type(M68K_CPU_TYPE_68000);
        memset(mregs, 0, sizeof(mregs));
        update_mapping();               // MAME: mapper device_reset - all registers 0
        if (c == 1) sub_vec_fetch = 2;
        m68k_pulse_reset();
        sub_vec_fetch = 0;
        m68k_get_context(ctx[c].data());
    }

    apply_inputs(0);
    frame_no = 0; prelude = true;
    if (!getenv("SH_NOPRELUDE")) for (int line = 224; line < 262; line++) run_line(line);
    prelude = false;
    for (frame_no = 0; frame_no < frames; frame_no++) {
        { static const bool al = getenv("SH_ATTRLOG") != nullptr;   // the attract mode's flags (docs/04: 0x60050, 0x60408, 0x66052)
          if (al) printf("ATTR f%ld %04x %04x %04x de %d\n", frame_no, workram[0x28], workram[0x204], workram[0x3029], display_enable);
          static const char* wd = getenv("SH_WRAMDUMP");    // SH_WRAMDUMP=<prefix>,<every>: the main CPU's work RAM (big-endian words)
          if (wd) { static long every = atol(strchr(wd, ',') ? strchr(wd, ',') + 1 : "600"); if (frame_no % every == 0) {
              char n[512]; snprintf(n, sizeof n, "%.*s%05ld.bin", (int)(strchr(wd, ',') ? strchr(wd, ',') - wd : strlen(wd)), wd, frame_no);
              FILE* f = fopen(n, "wb"); for (int i = 0; i < 0x4000; i++) { u8 b[2] = { (u8)(workram[i] >> 8), (u8)workram[i] }; fwrite(b, 1, 2, f); } fclose(f); } } }
        bool shot = false, wdump = false;
        spr_dump = false; vid_dump = false;
        for (auto& e : evs) {
            if (e.k == "shot" && in_range(e, frame_no)) shot = true;
            if (e.k == "sprdump" && in_range(e, frame_no)) spr_dump = true;
            if (e.k == "vdump" && in_range(e, frame_no)) vid_dump = true;
            if (e.k == "wdump" && in_range(e, frame_no)) wdump = true;
        }
        busy_n[0] = busy_n[1] = busy_b[0] = busy_b[1] = 0;
        subacc_on = getenv("SH_SUBACC") != nullptr; memset(subacc, 0, sizeof subacc);
        run_frame();
        if (subacc_on) printf("A f%ld code %ld romdata %ld ram %ld road %ld ctl %ld (writes %ld)\n", frame_no, subacc[0], subacc[1], subacc[2], subacc[3], subacc[4], subacc[5]);
        if (busy_log) printf("B f%ld %.1f %.1f\n", frame_no, 100.0 * busy_b[0] / busy_n[0], 100.0 * busy_b[1] / busy_n[1]);
        if (stats) {
            int mw = 0, mp = 0;
            for (int y = 0; y < 224; y++) { mw = std::max(mw, st_linewords[y]); mp = std::max(mp, st_linepix[y]); }
            printf("S f%ld spr %d pix %d words %d maxline words %d pix %d\n", frame_no, st_spr, st_pix, st_words, mw, mp);
        }
        if (shot) { char n[64]; snprintf(n, sizeof n, "/f%05ld.ppm", frame_no); write_ppm(out + n); }
        if (wdump) {   // the picture writer's bench (fpga/shfx/sim tb_writer): the palette at line 224, MAME's bitmap, the colours
            char n[64]; snprintf(n, sizeof n, "/w%05ld.bin", frame_no);
            FILE* f = fopen((out + n).c_str(), "wb");
            if (f) { fwrite(pal224, 2, 0x1000, f); fwrite(bitmap, 2, 224 * 320, f); fwrite(rgbframe, 1, sizeof rgbframe, f); fclose(f); }
        }
        if (frame_no % 60 == 0) {
            cur_cpu = 0; m68k_set_context(ctx[0].data());
            u32 pc0 = m68k_get_reg(nullptr, M68K_REG_PC);
            cur_cpu = 1; m68k_set_context(ctx[1].data());
            u32 pc1 = m68k_get_reg(nullptr, M68K_REG_PC);
            printf("f%ld pc %06x / %06x  map", frame_no, pc0, pc1);
            for (int i = 0x10; i < 0x20; i++) printf(" %02x", mregs[i]);
            printf("  disp %d road %d\n", display_enable, road_control);
            fflush(stdout);
        }
    }
    if (shared_log) {
        FILE* f = fopen((out_dir + "/shared.txt").c_str(), "w");
        for (int i = 0; i < 0x4000; i++) if (shared_cnt[0][i] || shared_cnt[1][i])
            fprintf(f, "%05x r %ld w %ld\n", 0x60000 + i * 2, shared_cnt[0][i], shared_cnt[1][i]);
        fclose(f);
        fprintf(stderr, "main's accesses to the sub's RAM: %ld reads, %ld writes\n", shared_main_cnt[0], shared_main_cnt[1]);
    }
    for (int c = 0; c < 2; c++) if (cover[c]) {
        FILE* f = fopen((out_dir + "/cover" + std::to_string(c) + ".bin").c_str(), "wb"); fwrite(cover[c], 1, 0x100000, f); fclose(f);
    }
    if (snd_pc_hist) { FILE* f = fopen(getenv("SH_SNDHIST"), "w"); for (int a = 0; a < 65536; a++) if (snd_pc_hist[a]) fprintf(f, "%04x %u\n", a, snd_pc_hist[a]); fclose(f); }
    if (audio_on && !audio.empty()) {          // SH_WAV: the SegaPCM's sound at MAME's -wavwrite level, 31,250 Hz stereo
        FILE* f = fopen(getenv("SH_WAV"), "wb");
        uint32_t n = audio.size() * 2, rate = 31250, br = rate * 4;
        fwrite("RIFF", 1, 4, f); uint32_t sz = 36 + n; fwrite(&sz, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
        uint32_t fl = 16; uint16_t fmt = 1, ch = 2, ba = 4, bits = 16;
        fwrite(&fl, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f);
        fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f);
        for (int16_t v : audio) fwrite(&v, 2, 1, f);
        fclose(f);
    }
    if (getenv("SH_ROMHIST")) {
        FILE* f = fopen(getenv("SH_ROMHIST"), "w");
        for (int b = 0; b < 0x180; b++) if (romhist[b]) fprintf(f, "%05x %ld\n", b << 10, romhist[b]);
        fclose(f);
    }
    if (pc_hist) {
        FILE* f = fopen((out_dir + "/pcprof.txt").c_str(), "w");
        for (int c = 0; c < 2; c++) for (u32 a = 0; a < 0x100000; a++) if (pc_hist[c][a]) fprintf(f, "%d %06x %u\n", c, a, pc_hist[c][a]);
        fclose(f);
    }
    return 0;
}
#endif

// ------------------------------------------------------------------------------------------------ the library (sh_board.h)
// The arcade without its sub CPU: the main 68000, the mapper, the video - what the FPGA is - for a host that runs the sub
// program itself (ares' EverDrive model: the Genesis' 68000). Built with -DSHB_LIBRARY.
#include "sh_board.h"
static void shb_copy(const u8* src, u8* dst, size_t n) { memcpy(dst, src, n); }
extern "C" int shb_init(const shb_roms* r) {
    shb_copy(r->main_rom, rom_main, sizeof rom_main);
    shb_copy(r->sub_rom, rom_sub, sizeof rom_sub);
    memcpy(rom_subcpu, rom_sub, sizeof rom_sub);
    shb_copy(r->tiles, gfx1, sizeof gfx1);
    for (int i = 0; i < 0x80000; i++) sprrom[i] = (r->sprites[2 * i] << 8) | r->sprites[2 * i + 1];
    memset(roadrom, 0, sizeof roadrom); shb_copy(r->road, roadrom, 0x8000);
    video_init();
    external_sub = true;
    m68k_init();
    ctx[0].resize(m68k_context_size()); ctx[1].resize(m68k_context_size());
    cur_cpu = 0; m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    memset(mregs, 0, sizeof mregs); update_mapping();
    m68k_pulse_reset(); m68k_get_context(ctx[0].data());
    frame_no = 0; picture_count = 0;
    return 0;
}
extern "C" void shb_line(int line, const shb_inputs* in) {
    if (in) { in_unknown = in->unknown; in_dsw = in->dsw; in_coinage = in->coinage; for (int i = 0; i < 4; i++) adc[i] = in->adc[i]; }
    run_line(line);
    if (line == 261) frame_no++;
}
extern "C" long shb_pictures() { return picture_count; }
extern "C" int shb_sub_irq4() { return sub_irq4; }
extern "C" int shb_display_enable() { return display_enable; }
extern "C" void shb_pause(int on) { main_paused = on; }          // (kit 7) the main CPU stopped where it is
extern "C" unsigned shb_wram_read(unsigned off) { return workram[(off >> 1) & 0x3fff]; }   // (kit 7) 0x1B0000-0x1B7FFF
extern "C" void shb_wram_write(unsigned off, unsigned d, unsigned mask) {
    u16& w = workram[(off >> 1) & 0x3fff]; w = (u16)((w & ~mask) | (d & mask));
}   // I/O 0x0000 bit 5: 0 in the attract mode (the sound muted, docs/13)
extern "C" int shb_take_sub_reset() { int r = sub_reset_pending; sub_reset_pending = 0; return r; }
// the sub's world as the console reaches it: an offset in 0x60000-0x9FFFF (its RAM, the road RAM, the road control)
extern "C" unsigned shb_sub_read(unsigned off) {
    off &= 0xffffe;
    if (off >= 0x60000 && off < 0x80000) return cpu1ram[(off & 0x7fff) >> 1];
    if (off >= 0x80000 && off < 0x90000) return roadram[(off & 0xfff) >> 1];
    if (off >= 0x90000 && off < 0xa0000) return road_control_r();
    return 0xffff;
}
extern "C" void shb_sub_write(unsigned off, unsigned d, unsigned mask) {
    off &= 0xffffe; u16* w = nullptr;
    if (off >= 0x60000 && off < 0x80000) w = &cpu1ram[(off & 0x7fff) >> 1];
    else if (off >= 0x80000 && off < 0x90000) w = &roadram[(off & 0xfff) >> 1];
    else if (off >= 0x90000 && off < 0xa0000) { road_control_w(d, mask); return; }
    if (w) *w = (*w & ~mask) | (d & mask);
}
// the last picture as the FPGA's writer makes it: a byte a pixel, the colours numbered in the order met (0 kept for
// the 32X's black border), their BGR555 words; returns the number of colours (255 at most: past it, 255)
extern "C" int shb_take_latch(void) {    // every byte to the sound board and its resets, in order (register 0x48's bits 9-0)
    if (latch_bytes.empty()) return -1;
    int c = latch_bytes.front(); latch_bytes.erase(latch_bytes.begin()); return c;
}
extern "C" int shb_take_sound(void) {
    if (snd_q.empty()) return -1;
    int c = snd_q.front(); snd_q.erase(snd_q.begin()); return c;
}
extern "C" int shb_picture8(unsigned char* pix, unsigned short* pal) {
    int n = 1; std::vector<int> map(1 << 15, -1);
    pal[0] = 0;
    for (int y = 0; y < 224; y++) for (int x = 0; x < 320; x++) {
        const u8* c = rgbframe[y][x];
        int v = (c[0] >> 3) | ((c[1] >> 3) << 5) | ((c[2] >> 3) << 10);
        if (map[v] < 0) { if (n < 256) { map[v] = n; pal[n] = v; n++; } else map[v] = 255; }
        pix[y * 320 + x] = map[v];
    }
    for (int i = n; i < 256; i++) pal[i] = 0;
    return n - 1;
}

