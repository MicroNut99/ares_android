/* urr_fx.h - U-R-R's cart FPGA engine (v2: the effects), as a C++ reference model.
   The Verilog (fpga/mapper/map_urr/urr_engine.sv) must give the same bytes for the same commands:
   fpga/sim/tb_engine.cpp runs both and compares every output memory.  ares (on its urr-fpga branch)
   uses this file too, so the game can be tried in an emulator.  Everything is integer, bit for bit.

   The cartridge window it owns: 0x3F8000-0x3FFFFF (U-R-R's ROM leaves it 0xFF; build.sh checks).
     0x3F8000  NT_A   plane A name table, 28 rows x 40 cells (words, row after row): the RINGS
     0x3F9000  NT_B   plane B name table, 28 x 40: the GLOW
     0x3FA000  HS     the H scroll table, 224 lines x (plane A, plane B) words: the RIPPLE
     0x3FB000  TXT    192 tiles (6144 bytes): the LETTERING, 256 x 48 px, as 16 sprites of 4 x 3 tiles
                      (sprite s = sy*8 + sx at 32*sx, 24*sy; its tiles column after column, 12 each)
     0x3FF000  registers (read):  0 "UR"  1 "R1"  2 version  3 last command  4 commands received
                      5 a free-running counter  6 status  7 text: centre y  8 text: centre x
                      status: bit 0 busy, 1 rings on, 2 glow on, 3 ripple on, 4 text on, 15:8 renders
     0x3FF800  the command window: a READ of 0x3FF800 + 2*v sends v (0..1023), answers v ^ 0x05A5:
                      0x000-0x0FF  select register v           0x100 | b  low byte
                      0x200 | b    high byte: register = (b << 8) | low, then the next register
                      0x300        RENDER (with the committed registers; busy until done)
                      0x301        CLEAR every output          0x302  COMMIT (live registers -> committed)
   Registers (16 bits; the SH2 writes them, COMMIT makes them the ones RENDER uses):
     0x00-0x1F  8 rings: x, y (screen px, signed), r (px, 0 = none), pal (1-3 = palette, 0 = none)
     0x20-0x24  glow: x, y, R (px), level (0-30), invR (65536 / R)
     0x25-0x29  ripple: amp (px), phase (0-255), centre line, extent (lines), invE (65536 / extent)
     0x2A-0x2E  text: length (0 = none), angle (0-255), scale (8.8: texture px per screen px),
                centre y, centre x (screen px; the 68000 places the sprites)
     0x30-0x3F  text: 16 characters (ASCII 32-95)
     0x40-0x4F  v4: each letter's own offset across the text (signed, 1/8 of a font pixel)
     0x50-0x5F  v4: each letter's own offset down the text (signed, 1/8 of a font pixel) - so the letters
                can wave, bounce, fall in, fly apart, inside the turned and scaled lettering (all 0: as v3)
     v5 (U-R-R v123): SOLAR STORMS - 512 particles of the engine's own, drawn into NT_B (where there is no glow)
     0x60-0x77  6 emitters (the burning stars): x, y (screen px, signed), R (px: they leave its limb), rate
                (0-15 particles each RENDER; 0 = off)
     0x78       a flare: when this changes, a burst from emitter 0x79 (0-5) at angle 0x7A (0-255, +-16), of
                0x7B particles (0-255), faster and longer-lived
     0x7C, 0x7D the player (screen px), 0x7E: half the size of its box (px, 0 = no hits) - a particle in the
                box is gone and counted (register 9)
     0x7F       how much they curl (their velocity turned each step): bits 1:0 the emitters', bits 3:2 a flare's -
                0 by 1/16, 1 by 1/32, 2 by 1/64 (a gentle roll), 3 not at all
     read: 9 particles that hit the player (counts up, wraps)   10 particles alive   status bit 5: any alive
   The 68000 copies the outputs to VRAM; the tiles they name (rings, glow) are its library
   (tools/make_fxlib.py: RING_BASE, GLOW_BASE, TXT_BASE below must match). */
#pragma once
#include <stdint.h>
#include <string.h>

namespace urrfx {

enum { RING_BASE = 32, GLOW_BASE = 496, TXT_BASE = 544, PART_BASE = 736, NREG = 128, NPART = 512, VERSION = 5 };   /* v3 (U-R-R v110): rounder rings; v4 (v120): letters of their own; v5 (v123): solar storms */
enum { NT_W = 40, NT_H = 28, NT_N = NT_W * NT_H, HS_N = 448, TXT_W = 3072 };

static const signed char sin8s_tab[256] = {
#include "sin8s.inc"
};
static const unsigned char font57[64][7] = {
#include "font57.inc"
};

static inline uint32_t isqrt22(uint32_t v) {      /* floor(sqrt(v)), v < 2^22: 11 steps, as the Verilog */
    uint32_t r = 0;
    for (int b = 10; b >= 0; b--) {
        uint32_t t = r | (1u << b);
        if (t * t <= v) r = t;
    }
    return r;
}
static inline int sector32(int dx, int dy) {      /* the direction of (dx, dy) in 32 steps, 0 = +x, 8 = +y */
    int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, q;   /* (11.25 degrees: tan of the half-steps * 256) */
    if (ay * 256 <= ax * 25) q = 0;
    else if (ay * 256 <= ax * 78) q = 1;
    else if (ay * 256 <= ax * 137) q = 2;
    else if (ay * 256 <= ax * 210) q = 3;
    else if (ax * 256 <= ay * 25) q = 8;
    else if (ax * 256 <= ay * 78) q = 7;
    else if (ax * 256 <= ay * 137) q = 6;
    else if (ax * 256 <= ay * 210) q = 5;
    else q = 4;
    if (dx >= 0 && dy >= 0) return q;
    if (dx < 0 && dy >= 0) return 16 - q;
    if (dx < 0) return 16 + q;
    return (32 - q) & 31;
}

static inline int asr(int v, int n) { return v >> n; }      /* arithmetic (floor), as Verilog's >>> */
static inline int sext(int v, int bits) { v &= (1 << bits) - 1; return v & (1 << (bits - 1)) ? v - (1 << bits) : v; }

struct Part { int16_t x, y; int16_t vx, vy; uint8_t life, curl, cm; };   /* x, y: 1/16 px; vx, vy: 10 bits; cm: curl 0-3 */

struct Engine {
    uint16_t live[NREG], reg[NREG];
    uint8_t sel, lo;
    uint16_t lastcmd, ncmd, renders;
    uint32_t tick;
    uint16_t nta[NT_N], ntb[NT_N], hs[HS_N], txt[TXT_W];
    Part P[NPART];
    uint16_t pptr, lfsr, lastseq, hits, alive;
    uint8_t pmap[NT_N];

    void reset() { memset(this, 0, sizeof *this); lfsr = 0xACE1; }
    Engine() { reset(); }

    static int s16(uint16_t v) { return (int16_t)v; }
    bool rings_on() const { for (int k = 0; k < 8; k++) if (reg[4 * k + 2] && (reg[4 * k + 3] & 3)) return true; return false; }
    bool glow_on() const { return reg[0x22] && reg[0x23]; }
    bool ripple_on() const { return reg[0x28] && reg[0x25]; }
    bool text_on() const { return reg[0x2A] != 0; }

    uint16_t status() const {
        return (uint16_t)((renders & 255) << 8 | (alive != 0) << 5 | text_on() << 4 | ripple_on() << 3 | glow_on() << 2 | rings_on() << 1);
    }

    /* a 16-bit cartridge read at a byte address in 0x3F8000-0x3FFFFF */
    uint16_t read(uint32_t a) {
        a &= 0x7FFE;
        if (a >= 0x7800) { uint16_t v = (a - 0x7800) >> 1; command(v); return v ^ 0x05A5; }
        if (a >= 0x7000) {
            switch ((a - 0x7000) >> 1) {
            case 0: return 0x5552; case 1: return 0x5231; case 2: return VERSION;
            case 3: return lastcmd; case 4: return ncmd; case 5: return (uint16_t)(tick >> 16);
            case 6: return status(); case 7: return reg[0x2D]; case 8: return reg[0x2E];
            case 9: return hits; case 10: return alive;
            default: return 0;
            }
        }
        if (a < 0x0000 + 2 * NT_N) return nta[a >> 1];
        if (a >= 0x1000 && a < 0x1000 + 2 * NT_N) return ntb[(a - 0x1000) >> 1];
        if (a >= 0x2000 && a < 0x2000 + 2 * HS_N) return hs[(a - 0x2000) >> 1];
        if (a >= 0x3000 && a < 0x3000 + 2 * TXT_W) return txt[(a - 0x3000) >> 1];
        return 0;
    }

    void command(uint16_t v) {
        lastcmd = v; ncmd++;
        if (v < 0x100) sel = (uint8_t)v;
        else if (v < 0x200) lo = (uint8_t)v;
        else if (v < 0x300) { if (sel < NREG) live[sel] = (uint16_t)((v & 255) << 8 | lo); sel++; }
        else if (v == 0x300) render();
        else if (v == 0x301) { memset(nta, 0, sizeof nta); memset(ntb, 0, sizeof ntb); memset(hs, 0, sizeof hs); memset(txt, 0, sizeof txt);
                               for (int i = 0; i < NPART; i++) P[i].life = 0; alive = 0; }
        else if (v == 0x302) memcpy(reg, live, sizeof reg);
    }

    uint16_t rng() { lfsr = (uint16_t)((lfsr >> 1) ^ ((lfsr & 1) ? 0xB400 : 0)); return lfsr; }
    void spawn(int e, int a, int sp, int life, int cm) {
        int c = sin8s_tab[(a + 64) & 255], s = sin8s_tab[a & 255], Rr = reg[0x62 + 4 * e] & 255;
        Part &p = P[pptr];
        p.x = (int16_t)sext(s16(reg[0x60 + 4 * e]) * 16 + asr(c * Rr, 3), 16);
        p.y = (int16_t)sext(s16(reg[0x61 + 4 * e]) * 16 + asr(s * Rr, 3), 16);
        p.vx = (int16_t)sext(asr(c * sp, 7), 10); p.vy = (int16_t)sext(asr(s * sp, 7), 10);
        p.life = (uint8_t)life; p.curl = rng() & 1; p.cm = (uint8_t)cm;
        pptr = (pptr + 1) & (NPART - 1);
    }
    /* v5: SOLAR STORMS - new particles off each emitter's limb (and a flare's burst), then every particle a
       step: curling (its velocity turned by 1/16 each step, one way or the other), older, gone when old or
       off the screen, or counted when it touches the player; drawn into pmap: in its 8 x 8 cell, a dot at
       one of 4 x 4 places, as hot as it is young (PART_BASE + heat * 16 + place) */
    void particles() {
        for (int e = 0; e < 6; e++) {
            int n = reg[0x63 + 4 * e] & 15;
            for (int k = 0; k < n; k++) { int a = rng() & 255, sp = 16 + (rng() & 31), life = 20 + (rng() & 31); spawn(e, a, sp, life, reg[0x7F] & 3); }
        }
        if (reg[0x78] != lastseq) {
            lastseq = reg[0x78];
            int e = reg[0x79], n = reg[0x7B] & 255;
            if (e < 6) for (int k = 0; k < n; k++) {
                int a = (reg[0x7A] + (rng() & 31) - 16) & 255, sp = 32 + (rng() & 63), life = 40 + (rng() & 63);
                spawn(e, a, sp, life, (reg[0x7F] >> 2) & 3);
            }
        }
        alive = 0;
        int hr = reg[0x7E] & 255, hx = s16(reg[0x7C]), hy = s16(reg[0x7D]);
        for (int i = 0; i < NPART; i++) {
            Part &p = P[i];
            if (!p.life) continue;
            int t1 = p.cm == 3 ? 0 : asr(p.vy, 4 + p.cm), t2 = p.cm == 3 ? 0 : asr(p.vx, 4 + p.cm), vx, vy;
            if (p.curl) { vx = p.vx - t1; vy = p.vy + t2; } else { vx = p.vx + t1; vy = p.vy - t2; }
            p.vx = (int16_t)sext(vx, 10); p.vy = (int16_t)sext(vy, 10);
            p.x = (int16_t)sext(p.x + p.vx, 16); p.y = (int16_t)sext(p.y + p.vy, 16);
            p.life--;
            int xp = asr(p.x, 4), yp = asr(p.y, 4);
            if (!p.life || xp < -16 || xp >= 336 || yp < -16 || yp >= 240) { p.life = 0; continue; }
            int ax = xp - hx, ay = yp - hy;
            if (hr && (ax < 0 ? -ax : ax) <= hr && (ay < 0 ? -ay : ay) <= hr) { p.life = 0; hits++; continue; }
            alive++;
            if (xp >= 0 && xp < 320 && yp >= 0 && yp < 224) {
                int heat = p.life >= 48 ? 3 : p.life >= 24 ? 2 : p.life >= 10 ? 1 : 0;
                pmap[(yp >> 3) * NT_W + (xp >> 3)] = (uint8_t)(1 + heat * 16 + ((xp >> 1) & 3) + (((yp >> 1) & 3) << 2));
            }
        }
    }

    void render() {
        renders++;
        /* NT_A: the rings.  Each cell's centre against each ring: on it if |d - r| <= 7 px; the tile is
           the library's arc for the ring's direction there (16, mod 180 degrees: 11.25 degree steps) and
           its offset from the cell's centre in half pixels (29) - so the arcs meet: round rings. */
        for (int cy = 0; cy < NT_H; cy++) for (int cx = 0; cx < NT_W; cx++) {
            uint16_t e = 0;
            for (int k = 0; k < 8 && !e; k++) {
                int r = reg[4 * k + 2], pal = reg[4 * k + 3] & 3;
                if (!r || !pal) continue;
                int dx = cx * 8 + 4 - s16(reg[4 * k]), dy = cy * 8 + 4 - s16(reg[4 * k + 1]);
                if (dx < -511 || dx > 511 || dy < -511 || dy > 511) continue;
                int d2 = (int)isqrt22((uint32_t)(dx * dx + dy * dy) * 4);     /* 2 * distance */
                int o = d2 - 2 * r;                                            /* half pixels */
                if (r > 1023 || o < -14 || o > 14) continue;
                int s = sector32(dx, dy);
                if (s >= 16) o = -o;
                e = (uint16_t)(pal << 13 | (RING_BASE + (s & 15) * 29 + o + 14));
            }
            nta[cy * NT_W + cx] = e;
            pmap[cy * NT_W + cx] = 0;
        }
        particles();
        /* NT_B: the glow, brightest in the middle, 30 half-steps (library tiles: two colours dithered) */
        for (int cy = 0; cy < NT_H; cy++) for (int cx = 0; cx < NT_W; cx++) {
            uint16_t e = 0;
            int R = reg[0x22], lev = reg[0x23];
            if (R && lev) {
                int dx = cx * 8 + 4 - s16(reg[0x20]), dy = cy * 8 + 4 - s16(reg[0x21]);
                if (dx >= -511 && dx <= 511 && dy >= -511 && dy <= 511) {
                    int d = (int)(isqrt22((uint32_t)(dx * dx + dy * dy) * 4) >> 1);
                    if (d < R) {
                        uint64_t l = ((uint64_t)(R - d) * (uint64_t)lev * reg[0x24]) >> 16;
                        if (l > 30) l = 30;
                        if (l) e = (uint16_t)(GLOW_BASE + l - 1);
                    }
                }
            }
            if (!e && pmap[cy * NT_W + cx]) e = (uint16_t)(PART_BASE + pmap[cy * NT_W + cx] - 1);   /* v5 */
            ntb[cy * NT_W + cx] = e;
        }
        /* HS: the ripple - every line round the centre line slides by a sine, fading out to the extent */
        for (int y = 0; y < 224; y++) {
            int h = 0, ext = reg[0x28], amp = reg[0x25];
            if (ext && amp) {
                int dy = y - s16(reg[0x27]), ady = dy < 0 ? -dy : dy;
                if (ady < ext) {
                    int env = (int)(((uint32_t)(ext - ady) * reg[0x29]) >> 8);
                    if (env > 256) env = 256;                                    /* 0..256 */
                    int s = sin8s_tab[(reg[0x26] + y * 8) & 255];
                    h = (amp * s * env) >> 15;
                }
            }
            hs[2 * y] = hs[2 * y + 1] = (uint16_t)h;
        }
        /* TXT: the lettering, turned by angle and scaled, about the middle of the 256 x 48 area */
        memset(txt, 0, sizeof txt);
        int L = reg[0x2A];
        if (L) {
            if (L > 16) L = 16;
            int W = L * 6 - 1;
            int c = sin8s_tab[(reg[0x2B] + 64) & 255], s = sin8s_tab[reg[0x2B] & 255], k = reg[0x2C] & 0x3FF;   /* scale: 10 bits */
            int32_t cs = c * k, sn = s * k;                         /* texture px per screen px, 15 frac bits */
            for (int oy = 0; oy < 48; oy++) {
                int Y = oy - 24;
                int32_t u = W * 16384 - 128 * cs - Y * sn;           /* at X = -128 */
                int32_t v = 7 * 16384 - 128 * sn + Y * cs;
                for (int ox = 0; ox < 256; ox++, u += cs, v += sn) {
                    int uf = u >> 12, vf = v >> 12, col = 0;          /* 1/8 font pixels */
                    for (int ch = 0; ch < L; ch++) {                 /* v4: the first letter (in order) here */
                        int ku = uf - 48 * ch - s16(reg[0x40 + ch]), kv = vf - s16(reg[0x50 + ch]);
                        if (ku < 0 || ku >= 40 || kv < 0 || kv >= 56) continue;
                        int cx = ku >> 3, tv = kv >> 3;
                        int a = reg[0x30 + ch] & 0x7F;
                        if (a >= 'a' && a <= 'z') a -= 32;
                        if (a < 32 || a > 95) a = 32;
                        if (font57[a - 32][tv] >> (4 - cx) & 1) col = 4 + (tv >> 1);
                        break;
                    }
                    if (col) {
                        int spr = (oy >= 24 ? 8 : 0) + (ox >> 5), ty = (oy >= 24 ? oy - 24 : oy) >> 3;
                        int tile = spr * 12 + ((ox >> 3) & 3) * 3 + ty;
                        int w = tile * 16 + (oy & 7) * 2 + ((ox >> 2) & 1);    /* word in the tile */
                        txt[w] |= (uint16_t)(col << (12 - 4 * (ox & 3)));
                    }
                }
            }
        }
    }
};

} // namespace urrfx
