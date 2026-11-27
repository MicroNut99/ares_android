/* snd.c - Super Hang-On's sound effects (docs/12): the arcade's sound board's program on the 32X's slave SH2.
 *
 * The board (MAME segaorun.cpp, the reference's host/sh_sound.cpp - matched to MAME, docs/04): a Z80 at 4 MHz running
 * epr-10649c, 2 KB of RAM, the sound latch from the main CPU (its NMI), the YM2151 (here only its timers, status and
 * busy flag: the music is MD+'s) and the SegaPCM (16 voices, 31,250 samples a second, the samples epr-10643-10646).
 * The program polls timer A (125 times a second); one tick here is one period of it: the Z80 runs 32,000 cycles (its
 * wait for the timer skipped), then the voices make that tick's samples.
 * The Z80 is z80f.c (ours: lean, for the SH-2). The voices' tick follows MAME's segapcm.cpp (BSD-3-Clause) and OutRun
 * CD32X's pwm_sound.cpp: the chip's registers are its RAM, 0x86 + 8n the control, 0x84/0x85 + 8n the address.
 * The game's bytes come with their times (the core's latch FIFO, docs/12) and reach the Z80 as far apart as they were
 * sent, a tick later; the tunes' commands are taken out (MD+ plays the music).
 * Built for the slave SH2 (main.c) and for the PC (host/, the comparison with the reference). MIT.
 */
#include <stdint.h>
#include <string.h>
#include "snd.h"

enum { TICK = 32000, WAIT_PC = 0x08C2, UNIT = 16, SAT = 4095 };   /* Z80 cycles a tick; its wait for timer A; the
                                                                     core's 4 us; its "16 ms or more" */
static const uint8_t *zrom;                 /* 32 KB */
static const uint8_t *pcm;                  /* 128 KB: the four 32 KB sample ROMs one after another */
static uint8_t zram[0x800];
static uint8_t pr[0x100];                   /* the SegaPCM's RAM = its registers */
static uint8_t low[16];                     /* each voice's address fraction */
static uint8_t latch, nmi_line;            /* (the 315-5195's: set by a byte, cleared by the Z80's read; NMI on its rise) */
static uint16_t q[64], qd[64]; static uint32_t qtime[64];
static int qh, qt, qs;                      /* the bytes (and resets) from the main CPU not yet given; qs: not yet timed */
static uint32_t last_t;                     /* the Z80 cycle the last one was (to be) given at */
static uint8_t held;                        /* the main CPU holds the board in reset */
static uint8_t ym_addr, ym_reg[0x20], status, ta_run, tb_run;
static uint32_t ta_next, tb_next, busy_end;
snd_stats_t snd_stats;

/* ---- the YM2151's timers, brought up to date when the Z80 looks (lazily: no work while it runs) */
static uint32_t ta_period(void) { return (1024 - ((ym_reg[0x10] << 2) | (ym_reg[0x11] & 3))) * 64u; }
static uint32_t tb_period(void) { return 16u * (256 - ym_reg[0x12]) * 64; }
static void timers_to(uint32_t now)
{
    if (ta_run) while ((int32_t)(now - ta_next) >= 0) { if (ym_reg[0x14] & 4) status |= 1; ta_next += ta_period(); }
    if (tb_run) while ((int32_t)(now - tb_next) >= 0) { if (ym_reg[0x14] & 8) status |= 2; tb_next += tb_period(); }
}
static uint8_t snd_in(uint32_t p, uint32_t now)
{
    p &= 0xff;
    if (p < 0x40) { if (!(p & 1)) return 0xff; timers_to(now); return (uint8_t)(status | ((int32_t)(now - busy_end) < 0 ? 0x80 : 0)); }
    if (p < 0x80) { nmi_line = 0; return latch; }
    return 0xff;
}
static void snd_out(uint32_t p, uint32_t v, uint32_t now)
{
    p &= 0xff;
    if (p >= 0x40) return;
    if (!(p & 1)) { ym_addr = (uint8_t)v; return; }
    busy_end = now + 64;
    if (ym_addr < 0x10 || ym_addr > 0x14) return;
    timers_to(now);
    ym_reg[ym_addr] = (uint8_t)v;
    if (ym_addr != 0x14) return;
    if (v & 0x10) status &= ~1;
    if (v & 0x20) status &= ~2;
    if ((v & 2) && !tb_run) { tb_run = 1; tb_next = now + tb_period(); } else if (!(v & 2)) tb_run = 0;
    if ((v & 1) && !ta_run) { ta_run = 1; ta_next = now + ta_period(); } else if (!(v & 1)) ta_run = 0;
}
#ifndef __32X__
void (*snd_pcm_log)(uint8_t reg, uint8_t data);
#define PCM_LOG(a, v) do { if (snd_pcm_log) snd_pcm_log((uint8_t)(a), (uint8_t)(v)); } while (0)
#else
#define PCM_LOG(a, v) do { } while (0)
#endif
#define ZRB(a)      ((a) < 0x8000 ? zrom[(a)] : (a) >= 0xf800 ? zram[(a) & 0x7ff] : (a) >= 0xf000 ? pr[(a) & 0xff] : 0)
#define ZWB(a, v)   do { uint32_t a_ = (a); if (a_ >= 0xf800) zram[a_ & 0x7ff] = (v); \
                         else if (a_ >= 0xf000) { PCM_LOG(a_, v); pr[a_ & 0xff] = (v); } } while (0)
#define ZIN(p)      snd_in((p), cyc)
#define ZOUT(p, v)  snd_out((p), (v), cyc)
#define Z80F_HOOK(p) ((p) == WAIT_PC && (timers_to(cyc), !(status & 1)))
#ifndef __32X__
extern unsigned *snd_pc_hist;
#define Z80F_TRACE(p) do { if (snd_pc_hist) snd_pc_hist[p]++; } while (0)
#endif
#include "z80f.c"
#ifdef Z80R                                 /* the program as C (tools/sound/z80rec.py, build/z80r.c): the SH-2's way */
#define Z80R_HOOK_PC WAIT_PC
#define Z80R_STACK(a) zram[(a) & 0x7ff]
#include "z80r.c"
#define Z80_RUN z80r_run
#else
#define Z80_RUN z80f_run
#endif

static z80f_t cpu;

#ifndef __32X__
#include <stdio.h>
#include <stdlib.h>
unsigned *snd_pc_hist;
unsigned long snd_runs;
uint8_t snd_peek(uint16_t a) { return ZRB(a); }
void snd_debug(void) { fprintf(stderr, "snd: cyc %u pc %04x held %d q %d/%d/%d instr %u runs %lu ticks %u\n", cpu.cyc, cpu.pc, held, qt, qs, qh, cpu.ninstr, snd_runs, snd_stats.ticks); }
#endif

void snd_init(const uint8_t *z80rom, const uint8_t *pcm128k)
{
    z80f_tables();
    zrom = z80rom; pcm = pcm128k;
    memset(zram, 0, sizeof zram);
    memset(pr, 0xff, sizeof pr);
    memset(low, 0, sizeof low);
    memset(ym_reg, 0, sizeof ym_reg);
    status = 0; ta_run = tb_run = 0; latch = 0; nmi_line = 0; qh = qt = qs = 0; busy_end = 0; held = 0;
    memset(&cpu, 0, sizeof cpu);
    z80f_reset(&cpu);
    last_t = 0;
    memset(&snd_stats, 0, sizeof snd_stats);
}
int snd_is_music(uint16_t e)                /* docs/04's commands that start a tune (the YM2151's, with SegaPCM drums) */
{
    switch (e) {
    case 0x81: case 0x82: case 0x83: case 0x8E: case 0x92: case 0x96: case 0x9A: case 0x9D: case 0x9E: case 0x9F: return 1;
    }
    return 0;
}
void snd_event(uint16_t e, uint16_t delta)  /* from the core's latch FIFO (0x1A0048's bits 9-0: a byte, or bit 9 a reset)
                                               and its time after the one before (0x1A004A: 4 us, 4095 = 16 ms or more) */
{
    int n = (qh + 1) & 63;
    if (n == qt) { snd_stats.lost++; return; }
    if (delta >= 250 && snd_is_music(e)) e = 0x80;   /* a packet's command (1 ms of quiet before it) that is music: MD+'s */
    q[qh] = e; qd[qh] = delta; qh = n;
}

/* one tick: the Z80 for 32,000 cycles (4 MHz, 8 ms), then n samples (mono: left + right) of the voices into out */
void snd_tick(int32_t *out, int n, int step)
{
    const uint32_t start = cpu.cyc, end = start + TICK;
    /* the new entries' times: as far apart as the game sent them; a first one (after 16 ms or more of nothing) a tick
       from now, so the ones that follow it within the next tick still find their times ahead */
    for (; qs != qh; qs = (qs + 1) & 63) {
        uint32_t t = last_t + (uint32_t)qd[qs] * UNIT;
        if (qd[qs] >= SAT || (int32_t)(t - start) < 0) {
            t = start + TICK;
            if ((int32_t)(t - (last_t + (uint32_t)SAT * UNIT)) < 0) t = last_t + (uint32_t)SAT * UNIT;
        }
        if (qd[qs] >= 250 && (int32_t)(t - (start + 3 * TICK)) > 0) {
            /* a packet's first byte more than three ticks ahead: the ticks came slower than the game's clock (the
               32X's sound a little slow, a tick late): the gap before the packet shortened (to 1 ms at least; the
               bytes within a packet as they came), so the queue never runs away from the game */
            t = start + 3 * TICK;
            if ((int32_t)(t - (last_t + 250u * UNIT)) < 0) t = last_t + 250u * UNIT;
        }
        qtime[qs] = last_t = t;
    }
    const uint32_t i0 = cpu.ninstr;
    while ((int32_t)(cpu.cyc - end) < 0) {
        int wait = 0;
        while (qt != qs && (int32_t)(qtime[qt] - cpu.cyc) <= 0) {   /* the entries due: a reset, or a byte (its NMI) */
            const uint16_t v = q[qt];
            /* a byte while the one before is not read yet waits for the read (half a millisecond at most): the program
               runs in blocks here (z80rec.py), so an NMI can come a few instructions late and the board's spacing of
               the bytes (11 x 4 us) is not always there - a byte lost puts the program's packets out of step for good */
            if (!(v & 0x200) && nmi_line && !held && (int32_t)(cpu.cyc - qtime[qt]) < 2000) { wait = 1; break; }
            qt = (qt + 1) & 63;
            if (v & 0x200) {
                const int on = !(v & 0x100);
                if (!on && held) { z80f_reset(&cpu); nmi_line = 0; }  /* let go: from address 0 again */
                held = (uint8_t)on;
            } else { latch = (uint8_t)v; if (!held && !nmi_line) { nmi_line = 1; cpu.nmi = 1; } }
        }
        uint32_t limit = end;
        if (qt != qs && (int32_t)(qtime[qt] - limit) < 0) limit = qtime[qt];
        if (wait) limit = cpu.cyc + 32;     /* (a few instructions at a time until the read) */
        timers_to(cpu.cyc);
        if (held || cpu.halted || (cpu.pc == WAIT_PC && !(status & 1) && !cpu.nmi)) {   /* nothing to do: to the next event */
            uint32_t to = limit;
            if (!held && ta_run && (int32_t)(ta_next - to) < 0) to = ta_next;
            if ((int32_t)(to - cpu.cyc) <= 0) to = cpu.cyc + 4;
            cpu.cyc = to;
            continue;
        }
        Z80_RUN(&cpu, limit);
#ifndef __32X__
        snd_runs++;
#endif
    }
    snd_stats.instr += cpu.ninstr - i0; snd_stats.ticks++;
    /* the voices (MAME segapcm.cpp voice_t::tick): an output sample every step chip samples (the address moves step
       times as far; the samples between are not read - the 32X makes 15,625 a second, docs/12). Each voice in runs
       that cannot reach its end: the end looked at between runs only. */
    for (int i = 0; i < 2 * n; i++) out[i] = 0;
    int act = 0;
    for (int ch = 0; ch < 16; ch++) {
        uint8_t *r = pr + 8 * ch;
        if (r[0x86] & 1) { low[ch] = 0; continue; }
        act++;
        const uint8_t *rom = pcm + ((uint32_t)((r[0x86] >> 4) & 3) << 15);   /* (BANK_512: four 32 KB ROMs, mirrored) */
        uint32_t addr = (uint32_t)r[0x85] << 16 | (uint32_t)r[0x84] << 8 | low[ch];
        const uint32_t loop = (uint32_t)r[0x05] << 16 | (uint32_t)r[0x04] << 8;
        const uint32_t endp = (uint8_t)(r[0x06] + 1);
        const int32_t lv = r[2] & 0x7f, rv = r[3] & 0x7f;
        const uint32_t inc = (uint32_t)r[7] * step;
        int32_t *o = out;
        int left = n;
        while (left > 0) {
            if ((addr >> 16) == endp) {
                if (r[0x86] & 2) { r[0x86] |= 1; break; }
                addr = loop;
            }
            /* how many samples before the address's page reaches the end's (or wraps): no checks in between (a loop
               that starts in the end's page: one at a time, as MAME) */
            const uint32_t pg = addr >> 16;
            const uint32_t lim = (pg < endp ? endp << 16 : 0x1000000u) - addr;
            int cnt = pg == endp ? 1 : inc ? (int)((lim + inc - 1) / inc) : left;
            if (cnt > left) cnt = left;
            if (lv | rv) for (int i = 0; i < cnt; i++) {
                const int32_t v = (int32_t)rom[(addr >> 8) & 0x7fff] - 0x80;
                o[2 * i] += v * lv; o[2 * i + 1] += v * rv; addr += inc;
            }
            else addr += inc * (uint32_t)cnt;
            addr &= 0xffffff; o += 2 * cnt; left -= cnt;
        }
        r[0x84] = (uint8_t)(addr >> 8); r[0x85] = (uint8_t)(addr >> 16);
        low[ch] = (r[0x86] & 1) ? 0 : (uint8_t)addr;
    }
    snd_stats.voices = act;
}
