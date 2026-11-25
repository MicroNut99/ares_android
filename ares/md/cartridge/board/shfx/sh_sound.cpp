// sh_sound.cpp - Super Hang-On's sound board for the reference (docs/04): the Z80 (superzazu/z80, MIT, ref/z80) running
// epr-10649c.88, its 2 KB of RAM, the sound latch from the main 68000 (the 315-5195's register 3: a write raises the
// Z80's NMI, the Z80's read of port 0x40 lowers it), the YM2151 as its timers and status only (the music is MD+'s;
// the board does not wire the YM's IRQ: the program polls the timer flags), and the SegaPCM (315-5218: 16 voices, the
// samples epr-10643-10646). What MAME does (segaorun.cpp sound_map / sound_portmap, 315_5195.cpp pread / pbf,
// ymfm_fm.ipp's timers, segapcm.cpp - BSD-3-Clause, Aaron Giles and the MAME team); this file is ours (MIT) but for
// the SegaPCM's voice tick, which follows segapcm.cpp line for line.
#include "sh_sound.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
extern "C" {
#include "z80.h"
}

extern void (*sh_sound_latch_log)(uint8_t);
namespace {
struct Voice { uint32_t addr = 0xffff00; uint16_t loop = 0xffff; uint8_t end = 0xff, freq = 0xff, lvol = 0xff, rvol = 0xff, ctrl = 0xff; };
struct Board {
    z80 cpu;
    const uint8_t* rom = nullptr;           // 32 KB
    const uint8_t* pcm = nullptr;           // 256 KB: the four 32 KB ROMs at 0x00000, 0x10000, 0x20000, 0x30000, each twice
    uint8_t ram[0x800];
    uint8_t pram[0x100];                    // the SegaPCM's RAM (its voices' registers read from the voices)
    Voice v[16];
    uint8_t latch = 0; bool nmi_line = false;
    bool reset = false;
    // the YM2151's timers (ymfm: A counts 64 clocks of 4 MHz a step, B 1024)
    uint8_t ym_addr = 0, ym_reg[0x20];
    uint8_t status = 0;
    unsigned long busy_end = 0;             // (ymfm: busy for 32 x 2 clocks after a data write)
    bool ta_run = false, tb_run = false; long ta_left = 0, tb_left = 0;
    long pcm_phase = 0;                     // Z80 cycles to the next sample (128 a sample)
    int16_t* out = nullptr; int out_cap = 0, out_n = 0;
    long pcm_log_frame = -1;
    struct Ev { long t; int kind; uint8_t d; } ev[64]; int nev = 0;   // the main's writes, each at its own Z80 cycle
    long target = 0;                        // the Z80's cycle the slices have reached (an instruction's overshoot carried)
} B;

uint8_t rd(void*, uint16_t a) {
    if (a < 0xf000) return a < 0x8000 ? B.rom[a] : 0x00;
    if (a < 0xf800) {
        uint8_t o = a & 0xff; Voice& v = B.v[(o >> 3) & 15];
        static const bool log = getenv("SH_PCMRD") != nullptr;
        if (log) fprintf(stderr, "R %04x pc %04x\n", a, B.cpu.pc);
        switch (o & 0x87) {
            case 0x02: return v.lvol; case 0x03: return v.rvol;
            case 0x04: return v.loop & 0xff; case 0x05: return v.loop >> 8;
            case 0x06: return v.end; case 0x07: return v.freq;
            case 0x84: return (v.addr >> 8) & 0xff; case 0x85: return (v.addr >> 16) & 0xff;
            case 0x86: return v.ctrl;
        }
        return B.pram[o];
    }
    return B.ram[a & 0x7ff];
}
void wr(void*, uint16_t a, uint8_t d) {
    if (a < 0xf000) return;
    if (a < 0xf800) {
        uint8_t o = a & 0xff; Voice& v = B.v[(o >> 3) & 15];
        B.pram[o] = d;
        if (sh_sound_pcm_log) sh_sound_pcm_log(o, d);
        switch (o & 0x87) {
            case 0x02: v.lvol = d; break; case 0x03: v.rvol = d; break;
            case 0x04: v.loop = (v.loop & 0xff00) | d; break; case 0x05: v.loop = (v.loop & 0x00ff) | (d << 8); break;
            case 0x06: v.end = d; break; case 0x07: v.freq = d; break;
            case 0x84: v.addr = (v.addr & ~0x00ff00u) | (uint32_t(d) << 8); break;
            case 0x85: v.addr = (v.addr & ~0xff0000u) | (uint32_t(d) << 16); break;
            case 0x86: v.ctrl = d; break;
        }
        return;
    }
    B.ram[a & 0x7ff] = d;
}
void ym_timers_start(int which) {
    // (ymfm update_timer: A's period (1024 - A) x 64 clocks, B's 16 x (256 - B) x 64 - the Z80's clock is the YM's)
    if (which == 0) { int a = (B.ym_reg[0x10] << 2) | (B.ym_reg[0x11] & 3); B.ta_left = (1024 - a) * 64L; }
    else B.tb_left = 16L * (256 - B.ym_reg[0x12]) * 64;
}
void ym_write(uint8_t r, uint8_t d) {
    static const bool log = getenv("SH_YMLOG") != nullptr;
    if (log && r >= 0x10 && r <= 0x14) fprintf(stderr, "YM %02x = %02x at %lu\n", r, d, B.cpu.cyc);
    if (r >= 0x10 && r <= 0x14) B.ym_reg[r] = d;
    if (r != 0x14) return;
    if (d & 0x10) B.status &= ~1;
    if (d & 0x20) B.status &= ~2;
    bool la = d & 1, lb = d & 2;
    if (lb && !B.tb_run) { B.tb_run = true; ym_timers_start(1); } else if (!lb) B.tb_run = false;
    if (la && !B.ta_run) { B.ta_run = true; ym_timers_start(0); } else if (!la) B.ta_run = false;
}
uint8_t pin(z80*, uint8_t p) {
    if (p < 0x40) return (p & 1) ? uint8_t(B.status | (B.cpu.cyc < B.busy_end ? 0x80 : 0)) : 0xff;   // (ymfm ym2151::read)
    if (p < 0x80) { B.nmi_line = false; if (sh_sound_latch_log) sh_sound_latch_log(B.latch);
        static const bool lt = getenv("SH_LATCHT") != nullptr; if (lt) printf("LT %02x %.9f\n", B.latch, B.cpu.cyc / 4e6);
        return B.latch; }
    return 0xff;
}
void pout(z80*, uint8_t p, uint8_t d) {
    if (p < 0x40) { if (p & 1) { ym_write(B.ym_addr, d); B.busy_end = B.cpu.cyc + 64; } else B.ym_addr = d; }
}
void timers(long cyc) {
    for (int t = 0; t < 2; t++) {
        bool& run = t ? B.tb_run : B.ta_run; long& left = t ? B.tb_left : B.ta_left;
        if (!run) continue;
        left -= cyc;
        while (left <= 0) {
            if (B.ym_reg[0x14] & (t ? 8 : 4)) B.status |= t ? 2 : 1;   // the flag only while its IRQ enable is set
            long add = t ? 16L * (256 - B.ym_reg[0x12]) * 64 : (1024 - ((B.ym_reg[0x10] << 2) | (B.ym_reg[0x11] & 3))) * 64L;
            left += add;
        }
    }
}
void pcm_tick(int32_t& l, int32_t& r) {
    l = r = 0;
    static const bool vlog = getenv("SH_VOICELOG") != nullptr; static long vt; static unsigned vm;
    if (vlog) { for (int i = 0; i < 16; i++) if (!(B.v[i].ctrl & 1)) vm |= 1u << i;
        if (++vt % 31250 == 0) { fprintf(stderr, "V %ld %04x", vt / 31250, vm); for (auto& v : B.v) fprintf(stderr, " %02x/%02x", v.ctrl, v.lvol); fprintf(stderr, "\n"); vm = 0; } }
    for (auto& v : B.v) {
        // segapcm.cpp voice_t::tick
        if (!(v.ctrl & 1)) {
            if ((v.addr >> 16) == ((v.end + 1) & 0xff)) {
                if (v.ctrl & 2) { v.ctrl |= 1; continue; }
                else v.addr = uint32_t(v.loop) << 8;
            }
            uint32_t bank = (v.ctrl & 0x70) << 12;
            int8_t s = int8_t(B.pcm[(bank + (v.addr >> 8)) & 0x3ffff] - 0x80);
            l += s * (v.lvol & 0x7f); r += s * (v.rvol & 0x7f);
            v.addr = (v.addr + v.freq) & 0xffffff;
        } else v.addr &= 0xffff00;
    }
}
} // namespace

uint8_t sh_sound_peek(uint16_t a) { return rd(nullptr, a); }
void (*sh_sound_pcm_log)(uint8_t reg, uint8_t data) = nullptr;
void (*sh_sound_latch_log)(uint8_t data) = nullptr;

void sh_sound_init(const uint8_t* z80rom, const uint8_t* pcm256k) {
    memset(&B.cpu, 0, sizeof B.cpu);
    B.rom = z80rom; B.pcm = pcm256k;
    memset(B.ram, 0, sizeof B.ram);
    memset(B.pram, 0xff, sizeof B.pram);          // (segapcm device_start: the RAM 0xFF)
    for (auto& v : B.v) v = Voice();
    memset(B.ym_reg, 0, sizeof B.ym_reg); B.status = 0; B.ta_run = B.tb_run = false;
    z80_init(&B.cpu);
    B.cpu.read_byte = rd; B.cpu.write_byte = wr; B.cpu.port_in = pin; B.cpu.port_out = pout;
    B.latch = 0; B.nmi_line = false; B.reset = false; B.pcm_phase = 128; B.target = 0; B.nev = 0;
}
static void latch_now(uint8_t d) {
    B.latch = d;
    if (!B.nmi_line) { B.nmi_line = true; z80_gen_nmi(&B.cpu); }     // (the NMI on the line's rise)
}
static void reset_now(bool on);
static void post(int kind, uint8_t d, long at) {
    // (MAME: the 315-5195's synchronize(): the write reaches the Z80 at the main's time of it, `at` Z80 cycles into the
    // next slice)
    if (B.nev == 64) { kind ? reset_now(d) : latch_now(d); return; }
    B.ev[B.nev++] = { B.target + at, kind, d };
}
void sh_sound_latch(uint8_t d, long at) { post(0, d, at); }
void sh_sound_reset(bool on, long at) { post(1, on, at); }
static void reset_now(bool on) {
    if (on && !B.reset) {
        unsigned long c = B.cpu.cyc;
        z80_init(&B.cpu); B.cpu.read_byte = rd; B.cpu.write_byte = wr; B.cpu.port_in = pin; B.cpu.port_out = pout;
        B.cpu.cyc = c; B.nmi_line = false;
    }
    B.reset = on;
}
int sh_sound_run(long cycles, int16_t* out, int max_frames) {
    // runs the Z80 `cycles` cycles of 4 MHz; the SegaPCM's samples (31,250 a second: one each 128 cycles) into out
    // as left / right pairs; returns the pairs written
    int n = 0;
    if (B.target < (long)B.cpu.cyc - 64) B.target = (long)B.cpu.cyc;   // (after an init)
    B.target += cycles;
    while ((long)B.cpu.cyc < B.target) {
        if (B.nev && B.ev[0].t <= (long)B.cpu.cyc) {
            auto e = B.ev[0]; memmove(B.ev, B.ev + 1, --B.nev * sizeof B.ev[0]);
            if (e.kind) reset_now(e.d); else latch_now(e.d);
            continue;
        }
        unsigned long c0 = B.cpu.cyc;
        static FILE* zt = getenv("SH_ZTRACE") ? fopen(getenv("SH_ZTRACE"), "w") : nullptr;
        if (zt && !B.reset && !B.cpu.halted) fprintf(zt, "%04X\n", B.cpu.pc);
        if (B.reset) B.cpu.cyc += 4; else z80_step(&B.cpu);
        long d = (long)(B.cpu.cyc - c0);
        timers(d);
        B.pcm_phase -= d;
        while (B.pcm_phase <= 0) {
            B.pcm_phase += 128;
            int32_t l, r; pcm_tick(l, r);
            if (out && n < max_frames) {
                // (MAME: put_int(sample, 32768) and the route's 0.70: the raw sum is kept here, the mixer scales it)
                auto cl = [](int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); };
                out[2 * n] = cl(l); out[2 * n + 1] = cl(r);
            }
            n++;
        }
    }
    while (B.nev && B.ev[0].t <= B.target) {                       // (the slice's last writes)
        auto e = B.ev[0]; memmove(B.ev, B.ev + 1, --B.nev * sizeof B.ev[0]);
        if (e.kind) reset_now(e.d); else latch_now(e.d);
    }
    return n;
}
