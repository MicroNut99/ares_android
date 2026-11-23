/* tomba.c - the Tomba CD32X engine's model (tomba.h says what it is). Its parts are the C the Verilog was checked
 * against (Tomba_CD32X tools/m1): the GTE is tgte.c's, the model unit tmodelu.sv's schedule over tmodel.c's
 * batches, the walk twalk.sv's, the decoder and the drawing tgpu.c's in its --fixed 1 --pal mode, written onto the
 * cart's memories as tomba_core.sv lays them out.
 *
 * MIT licence; Tomba CD32X, 2026.
 */
#include "tomba.h"
#include <string.h>
#ifdef TOMBA_STATS
uint64_t tb_n[16];   /* 0 PS1 RAM words, 1 pixels (pictures), 2 pixels (VRAM), 3 texels, 4 triangles, 5 rows, 6 GTE ops,
                        7 faces, 8 GP0 commands, 9 semi pixels (pictures), 10 upload / copy pixels, 11 rects */
#define TBN(i) (tb_n[i]++)
#else
#define TBN(i) ((void)0)
#endif

/* ------------------------------------------------------------------------------------------------ the memories */
static uint16_t psr(tomba_t* t, uint32_t w) { return t->psram_rd(t->ctx, w & 0x7FFFFFu); }
static void psw(tomba_t* t, uint32_t w, uint16_t v) { t->psram_wr(t->ctx, w & 0x7FFFFFu, v); }

/* the PS1 RAM through the page table: 32-bit words, the low halfword first */
static uint32_t ps1_word(tomba_t* t, uint32_t a, int* sram)
{
    uint16_t e = t->pt[(a >> 12) & 511];
    uint32_t in = ((a >> 2) & 0x3FFu) << 1;
    *sram = (e >> 12) & 1;
    return *sram ? ((uint32_t)(e & 0x7F) << 11 | in) : ((uint32_t)(e & 0xFFF) << 11 | in);
}
static uint32_t rd32(tomba_t* t, uint32_t a)
{
    int s; uint32_t w = ps1_word(t, a, &s);
    t->est += 14; t->nw++; TBN(0);
    if (s) return (uint32_t)t->sram[w & 0x3FFFF] | (uint32_t)t->sram[(w + 1) & 0x3FFFF] << 16;
    return (uint32_t)psr(t, w) | (uint32_t)psr(t, w + 1) << 16;
}
static void wr32(tomba_t* t, uint32_t a, uint32_t v)
{
    int s; uint32_t w = ps1_word(t, a, &s);
    t->est += 14; t->nw++; TBN(0);
    if (s) { t->sram[w & 0x3FFFF] = (uint16_t)v; t->sram[(w + 1) & 0x3FFFF] = (uint16_t)(v >> 16); }
    else { psw(t, w, (uint16_t)v); psw(t, w + 1, (uint16_t)(v >> 16)); }
}

/* VRAM (PSRAM words from TB_VBASE) and the 8-bit pictures (SRAM bytes, the even byte high) */
static uint16_t vrd(tomba_t* t, int x, int y) { return psr(t, TB_VBASE + (uint32_t)(y & 511) * 1024u + (uint32_t)(x & 1023)); }
static void vwr(tomba_t* t, int x, int y, uint16_t v) { psw(t, TB_VBASE + (uint32_t)(y & 511) * 1024u + (uint32_t)(x & 1023), v); }
static int in_fb(int x, int y) { return y >= 256 && y < 480 && x >= 384 && x < 1024; }
static uint32_t fbyte(int x, int y)
{
    int b = x >= 704;
    return (b ? 0x12000u : 0u) + (uint32_t)(y - 256) * 320u + (uint32_t)(x - (b ? 704 : 384));
}
static uint8_t fb_rd(tomba_t* t, int x, int y)
{
    uint32_t b = fbyte(x, y); uint16_t w = t->sram[(b >> 1) & 0x3FFFF];
    return (b & 1) ? (uint8_t)w : (uint8_t)(w >> 8);
}
static void fb_wr(tomba_t* t, int x, int y, uint8_t v)
{
    uint32_t b = fbyte(x, y); uint16_t* w = &t->sram[(b >> 1) & 0x3FFFF];
    *w = (b & 1) ? (uint16_t)((*w & 0xFF00) | v) : (uint16_t)((*w & 0x00FF) | v << 8);
}

/* ------------------------------------------------------------------------------------------------ the GTE (tgte.c) */
static uint8_t unr[257];
static void unr_init(void)
{
    int i;
    for (i = 0; i < 257; i++) { int v = ((0x40000 / (i + 0x100) + 1) / 2) - 0x101; unr[i] = v < 0 ? 0 : (uint8_t)v; }
}
static int clz16(uint16_t v) { int n = 0; while (n < 16 && !(v & 0x8000)) { v <<= 1; n++; } return n; }
static uint32_t divide(tomba_gte_t* g, uint32_t h, uint32_t sz3)
{
    if (h < sz3 * 2)
    {
        int z = clz16((uint16_t)sz3);
        uint64_t n = (uint64_t)h << z;
        uint32_t d = sz3 << z, u;
        int64_t dd;
        uint64_t r;
        u = unr[(d - 0x7FC0) >> 7] + 0x101;
        dd = (int64_t)((0x2000080 - (int64_t)d * u) >> 8);
        dd = (int64_t)((0x0000080 + dd * u) >> 8);
        r = (n * (uint64_t)dd + 0x8000) >> 16;
        if (r > 0x1FFFF) { g->flag |= 1u << 17; r = 0x1FFFF; }
        return (uint32_t)r;
    }
    g->flag |= 1u << 17;
    return 0x1FFFF;
}
static int64_t acc44(tomba_gte_t* g, int64_t v, int i)
{
    if (v > 0x7FFFFFFFFFFLL) g->flag |= 1u << (30 - i);
    else if (v < -0x80000000000LL) g->flag |= 1u << (27 - i);
    return (int64_t)((uint64_t)v << 20) >> 20;
}
static int32_t mac0(tomba_gte_t* g, int64_t v)
{
    if (v > 0x7FFFFFFFLL) g->flag |= 1u << 16;
    else if (v < -0x80000000LL) g->flag |= 1u << 15;
    return (int32_t)v;
}
static int16_t sat_ir(tomba_gte_t* g, int32_t v, int i, int lm)
{
    int32_t lo = lm ? 0 : -0x8000;
    if (v < lo) { g->flag |= 1u << (25 - i); return (int16_t)lo; }
    if (v > 0x7FFF) { g->flag |= 1u << (25 - i); return 0x7FFF; }
    return (int16_t)v;
}
static void rtp(tomba_gte_t* g, int k, int sf, int lm, int last)
{
    int64_t m3 = 0, sx, sy;
    int32_t z;
    uint32_t n;
    int i;
    for (i = 0; i < 3; i++)
    {
        int64_t a = acc44(g, (int64_t)g->tr[i] * 0x1000 + (int64_t)g->rt[i][0] * g->vx[k], i + 1);
        a = acc44(g, a + (int64_t)g->rt[i][1] * g->vy[k], i + 1);
        a = acc44(g, a + (int64_t)g->rt[i][2] * g->vz[k], i + 1);
        if (i == 2) m3 = a;
        g->mac[i + 1] = (int32_t)(a >> (sf ? 12 : 0));
    }
    g->ir[1] = sat_ir(g, g->mac[1], 1, lm);
    g->ir[2] = sat_ir(g, g->mac[2], 2, lm);
    {
        int32_t v = g->mac[3], lo = lm ? 0 : -0x8000, t3 = (int32_t)(m3 >> 12);
        g->ir[3] = (int16_t)(v < lo ? lo : v > 0x7FFF ? 0x7FFF : v);
        if (t3 < -0x8000 || t3 > 0x7FFF) g->flag |= 1u << 22;
    }
    z = (int32_t)(m3 >> 12);
    g->sz[0] = g->sz[1]; g->sz[1] = g->sz[2]; g->sz[2] = g->sz[3];
    if (z < 0) { g->flag |= 1u << 18; z = 0; } else if (z > 0xFFFF) { g->flag |= 1u << 18; z = 0xFFFF; }
    g->sz[3] = (uint16_t)z;
    n = divide(g, g->h, g->sz[3]);
    sx = mac0(g, (int64_t)n * g->ir[1] + g->ofx); sx >>= 16;
    sy = mac0(g, (int64_t)n * g->ir[2] + g->ofy); sy >>= 16;
    g->sx[0] = g->sx[1]; g->sx[1] = g->sx[2];
    g->sy[0] = g->sy[1]; g->sy[1] = g->sy[2];
    if (sx < -0x400) { g->flag |= 1u << 14; sx = -0x400; } else if (sx > 0x3FF) { g->flag |= 1u << 14; sx = 0x3FF; }
    if (sy < -0x400) { g->flag |= 1u << 13; sy = -0x400; } else if (sy > 0x3FF) { g->flag |= 1u << 13; sy = 0x3FF; }
    g->sx[2] = (int16_t)sx; g->sy[2] = (int16_t)sy;
    if (last)
    {
        int64_t d = (int64_t)n * g->dqa + g->dqb;
        int32_t i0;
        g->mac[0] = mac0(g, d);
        i0 = (int32_t)(d >> 12);
        if (i0 < 0) { g->flag |= 1u << 12; i0 = 0; } else if (i0 > 0x1000) { g->flag |= 1u << 12; i0 = 0x1000; }
        g->ir[0] = (int16_t)i0;
    }
}
/* tgeo.sv: RTPS, RTPT, NCLIP, MVMVA, AVSZ3, AVSZ4; anything else only clears FLAG. MVMVA as tgeo.sv has it: the
 * matrix RT only (mx 1-3: zeros), the vector TR only (cv 1-3: zeros), V0 for v 0-2 (k is 0 there), IR for v 3 */
static void gte_op(tomba_t* t, uint32_t ins)
{
    tomba_gte_t* g = &t->g;
    int sf = (ins >> 19) & 1, lm = (ins >> 10) & 1, c = ins & 0x3F, k, i;
    g->flag = 0; TBN(6);
    switch (c)
    {
        case 0x01: rtp(g, 0, sf, lm, 1); t->est += 40; break;
        case 0x30: for (k = 0; k < 3; k++) rtp(g, k, sf, lm, k == 2); t->est += 110; break;
        case 0x06:
            g->mac[0] = mac0(g, (int64_t)g->sx[0] * g->sy[1] + (int64_t)g->sx[1] * g->sy[2] + (int64_t)g->sx[2] * g->sy[0]
                                - (int64_t)g->sx[0] * g->sy[2] - (int64_t)g->sx[1] * g->sy[0] - (int64_t)g->sx[2] * g->sy[1]);
            t->est += 16; break;
        case 0x2D: case 0x2E:
        {
            int64_t s = (c == 0x2D) ? (int64_t)g->zsf3 * (g->sz[1] + g->sz[2] + g->sz[3])
                                    : (int64_t)g->zsf4 * (g->sz[0] + g->sz[1] + g->sz[2] + g->sz[3]);
            int64_t o;
            g->mac[0] = mac0(g, s);
            o = s >> 12;
            if (o < 0) { g->flag |= 1u << 18; o = 0; } else if (o > 0xFFFF) { g->flag |= 1u << 18; o = 0xFFFF; }
            g->otz = (uint16_t)o;
            t->est += 8; break;
        }
        case 0x12:
        {
            int mx = (ins >> 17) & 3, vv = (ins >> 15) & 3, cv = (ins >> 13) & 3;
            int16_t x, y, z;
            if (mx || cv || vv == 1 || vv == 2) { t->err |= 2; t->err_ins = ins; t->err_n++; }
            if (vv == 3) { x = g->ir[1]; y = g->ir[2]; z = g->ir[3]; } else { x = g->vx[0]; y = g->vy[0]; z = g->vz[0]; }
            for (i = 0; i < 3; i++)
            {
                int64_t a = acc44(g, (int64_t)(cv == 0 ? g->tr[i] : 0) * 0x1000 + (int64_t)(mx == 0 ? g->rt[i][0] : 0) * x, i + 1);
                a = acc44(g, a + (int64_t)(mx == 0 ? g->rt[i][1] : 0) * y, i + 1);
                a = acc44(g, a + (int64_t)(mx == 0 ? g->rt[i][2] : 0) * z, i + 1);
                g->mac[i + 1] = (int32_t)(a >> (sf ? 12 : 0));
            }
            for (i = 0; i < 3; i++) g->ir[i + 1] = sat_ir(g, g->mac[i + 1], i + 1, lm);
            t->est += 30; break;
        }
        default: t->est += 2; break;
    }
    if (g->flag & 0x7F87E000) g->flag |= 1u << 31;
}
static int16_t lo16(uint32_t v) { return (int16_t)(v & 0xFFFF); }
static int16_t hi16(uint32_t v) { return (int16_t)(v >> 16); }
/* the registers as tgeo.sv writes and reads them: 0-31 DR, 32-63 CR */
static void gte_wr(tomba_t* t, int r, uint32_t v)
{
    tomba_gte_t* g = &t->g;
    switch (r)
    {
        case 0: case 2: case 4: g->vx[r >> 1] = lo16(v); g->vy[r >> 1] = hi16(v); break;
        case 1: case 3: case 5: g->vz[r >> 1] = lo16(v); break;
        case 6: g->rgbc = v; break;
        case 7: g->otz = (uint16_t)v; break;
        case 8: case 9: case 10: case 11: g->ir[r - 8] = lo16(v); break;
        case 12: case 13: case 14: g->sx[r - 12] = lo16(v); g->sy[r - 12] = hi16(v); break;
        case 16: case 17: case 18: case 19: g->sz[r - 16] = (uint16_t)v; break;
        case 20: case 21: case 22: g->rgb[r - 20] = v; break;
        case 23: g->res1 = v; break;
        case 24: case 25: case 26: case 27: g->mac[r - 24] = (int32_t)v; break;
        case 32: g->rt[0][0] = lo16(v); g->rt[0][1] = hi16(v); break;
        case 33: g->rt[0][2] = lo16(v); g->rt[1][0] = hi16(v); break;
        case 34: g->rt[1][1] = lo16(v); g->rt[1][2] = hi16(v); break;
        case 35: g->rt[2][0] = lo16(v); g->rt[2][1] = hi16(v); break;
        case 36: g->rt[2][2] = lo16(v); break;
        case 37: case 38: case 39: g->tr[r - 37] = (int32_t)v; break;
        case 56: g->ofx = (int32_t)v; break;
        case 57: g->ofy = (int32_t)v; break;
        case 58: g->h = (uint16_t)v; break;
        case 59: g->dqa = lo16(v); break;
        case 60: g->dqb = (int32_t)v; break;
        case 61: g->zsf3 = lo16(v); break;
        case 62: g->zsf4 = lo16(v); break;
        case 63: g->flag = v; break;
        default: break;
    }
}
static uint32_t gte_rd(tomba_t* t, int r)
{
    tomba_gte_t* g = &t->g;
    switch (r)
    {
        case 0: case 2: case 4: return (uint16_t)g->vx[r >> 1] | (uint32_t)(uint16_t)g->vy[r >> 1] << 16;
        case 1: case 3: case 5: return (uint32_t)(int32_t)g->vz[r >> 1];
        case 6: return g->rgbc;
        case 7: return g->otz;
        case 8: case 9: case 10: case 11: return (uint32_t)(int32_t)g->ir[r - 8];
        case 12: case 13: case 14: return (uint16_t)g->sx[r - 12] | (uint32_t)(uint16_t)g->sy[r - 12] << 16;
        case 15: return (uint16_t)g->sx[2] | (uint32_t)(uint16_t)g->sy[2] << 16;
        case 16: case 17: case 18: case 19: return g->sz[r - 16];
        case 20: case 21: case 22: return g->rgb[r - 20];
        case 23: return g->res1;
        case 24: case 25: case 26: case 27: return (uint32_t)g->mac[r - 24];
        case 63: return g->flag;
        default: return 0;
    }
}

/* ------------------------------------------------------------------------------------------------ the model unit */
static const uint32_t fstride[8] = {32, 40, 40, 52, 40, 48, 64, 84};
static const uint32_t vfirst[8] = {8, 8, 16, 20, 4, 4, 12, 16};
static const uint32_t vlast[8] = {0, 32, 0, 44, 0, 28, 0, 40};
static const uint32_t psize[8] = {32, 40, 40, 52, 20, 24, 28, 36};
static const uint32_t lenw[8] = {0x7000000, 0x9000000, 0x9000000, 0xc000000, 0x4000000, 0x5000000, 0x6000000, 0x8000000};

/* tmodelu.sv: f faces, ot the model's ordering table, x the vertex stream (mode 2), cot CURRENT_OT (0x80000000 |),
 * np NEXT_PRIM; returns NEXT_PRIM after it */
static uint32_t model_draw(tomba_t* t, int mode, int lit, int ft4_ovr, int g4_ovr)
{
    uint32_t f = t->A[0], ot = t->A[1], vtx = t->A[2], g4tab = t->A[5];
    uint32_t cot = 0x80000000u | t->COT, p = (t->np_hi << 21) | t->NP;
    uint16_t box[4] = {(uint16_t)t->A[3], (uint16_t)(t->A[3] >> 16), (uint16_t)t->A[4], (uint16_t)(t->A[4] >> 16)};
    uint64_t e0 = t->est; uint32_t w0 = t->nw, faces = 0;
    int b;
    for (b = 0; b < 8; b++)
    {
        uint32_t n = rd32(t, f);
        if (mode != 2) vtx = f + 4;
        if (n == 0 || (lit && b >= 4))
        {
            if (lit && b >= 4 && n) t->unsup = 1;
            f += 4 + n * fstride[b]; vtx += n * fstride[b];
            continue;
        }
        f += 4;
        if (mode != 2) vtx = f;
        for (; n; n--, f += fstride[b], vtx += fstride[b])
        {
            uint32_t rec[21], vr[8], xy[4] = {0, 0, 0, 0}, otz, head, g4c[4] = {0, 0, 0, 0}, w;
            int quad = vlast[b] != 0, i;
            for (i = 0; (uint32_t)i * 4 < fstride[b]; i++) rec[i] = rd32(t, f + (uint32_t)i * 4);
            if (mode != 2)
            {
                for (i = 0; i < 6; i++) vr[i] = rec[vfirst[b] / 4 + (uint32_t)i];
                vr[6] = rec[vlast[b] / 4]; vr[7] = rec[vlast[b] / 4 + 1];
            }
            else
            {
                for (i = 0; i < 6; i++) vr[i] = rd32(t, vtx + vfirst[b] + (uint32_t)i * 4);
                if (quad) { vr[6] = rd32(t, vtx + vlast[b]); vr[7] = rd32(t, vtx + vlast[b] + 4); }
            }
            for (i = 0; i < 6; i++) gte_wr(t, i, vr[i]);
            faces++; TBN(7);
            gte_op(t, 0x4a280030u);
            if (t->g.flag & 0x80000000u) continue;
            gte_op(t, 0x4b400006u);
            if (t->g.mac[0] <= 0) continue;
            for (i = 0; i < 3; i++) xy[i] = gte_rd(t, 12 + i);
            if (quad)
            {
                gte_wr(t, 0, vr[6]); gte_wr(t, 1, vr[7]);
                gte_op(t, 0x4a180001u);
                if (t->g.flag & 0x80000000u) continue;
                xy[3] = gte_rd(t, 14);
            }
            {
                int yok = 0, xok = 0;
                for (i = 0; i < (quad ? 4 : 3); i++) { if ((uint16_t)(xy[i] >> 16) < 0xE0) yok = 1; if ((uint16_t)xy[i] < 0x140) xok = 1; }
                if (!(yok && xok)) continue;
            }
            gte_op(t, quad ? 0x4b68002eu : 0x4b58002du);
            {
                uint32_t o = ((uint32_t)gte_rd(t, 7) << 2) + ot;
                if (b == 1 && ft4_ovr) otz = cot + 12;
                else if (o - cot >= 0xCA0u) continue;
                else otz = o;
            }
            if (b == 7 && g4_ovr) for (i = 0; i < 4; i++) g4c[i] = rd32(t, g4tab + ((0x12u - n) << 4) + (uint32_t)i * 4);
            head = rd32(t, otz);
            wr32(t, otz, p);
            /* the primitive, word by word (tmodelu.sv's pword) */
#define F16(o) ((uint16_t)(((o) & 2) ? rec[(o) >> 2] >> 16 : rec[(o) >> 2]))
            for (i = 0; (uint32_t)i * 4 < psize[b]; i++)
            {
                w = 0;
                if (i == 0) w = head | lenw[b];
                else switch (b)
                {
                case 0: { const uint32_t v[8] = {0, rec[1], xy[0], (uint32_t)F16(0) << 16 | F16(0x0e), xy[1], (uint32_t)F16(2) << 16 | F16(0x16), xy[2], F16(0x1e)}; w = v[i]; break; }
                case 1: { const uint32_t v[10] = {0, rec[1], xy[0], (uint32_t)F16(0) << 16 | (ft4_ovr ? box[0] : F16(0x0e)), xy[1],
                                                  (uint32_t)F16(2) << 16 | (ft4_ovr ? box[1] : F16(0x16)), xy[2], ft4_ovr ? box[2] : F16(0x1e), xy[3],
                                                  ft4_ovr ? box[3] : F16(0x26)}; w = v[i]; break; }
                case 2: { const uint32_t v[10] = {0, rec[1], xy[0], (uint32_t)F16(0) << 16 | F16(0x16), rec[2], xy[1], (uint32_t)F16(2) << 16 | F16(0x1e),
                                                  rec[3], xy[2], F16(0x26)}; w = v[i]; break; }
                case 3: { const uint32_t v[13] = {0, rec[1], xy[0], (uint32_t)F16(0) << 16 | F16(0x1a), rec[2], xy[1], (uint32_t)F16(2) << 16 | F16(0x22),
                                                  rec[3], xy[2], F16(0x2a), rec[4], xy[3], F16(0x32)}; w = v[i]; break; }
                case 4: { const uint32_t v[5] = {0, rec[0], xy[0], xy[1], xy[2]}; w = v[i]; break; }
                case 5: { const uint32_t v[6] = {0, rec[0], xy[0], xy[1], xy[2], xy[3]}; w = v[i]; break; }
                case 6: { const uint32_t v[7] = {0, rec[0], xy[0], rec[1], xy[1], rec[2], xy[2]}; w = v[i]; break; }
                default: { const uint32_t v[9] = {0, g4_ovr ? g4c[0] : rec[0], xy[0], g4_ovr ? g4c[1] : rec[1], xy[1],
                                                  g4_ovr ? g4c[2] : rec[2], xy[2], g4_ovr ? g4c[3] : rec[3], xy[3]}; w = v[i]; break; }
                }
                wr32(t, p + (uint32_t)i * 4, w);
            }
#undef F16
            p += psize[b];
        }
    }
    /* the time: tb_core.cpp's 1,290 logged draws (three areas) take 21 clocks a PS1 RAM word and 269 a face, to 0.4 % */
    t->est = e0 + (uint64_t)(t->nw - w0) * 21u + (uint64_t)faces * 269u;
    return p;
}

/* ------------------------------------------------------------------------------------------------ the drawing (tgpu.c) */
static const int BAYER16[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
static const int DITHER[4][4] = {{-4, 0, -3, 1}, {2, -2, 3, -1}, {-3, 1, -4, 0}, {3, -1, 2, -2}};
static int s11(uint32_t v) { v &= 0x7FF; return (v & 0x400) ? (int)v - 0x800 : (int)v; }
static int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static uint8_t quant(tomba_t* t, int x, int y, int r8, int g8, int b8)
{
    int th = BAYER16[y & 3][x & 3];
    int r = (r8 + th) >> 4, g = (g8 + th) >> 4, b = (b8 + th) >> 4;
    if (r > 15) r = 15;
    if (g > 15) g = 15;
    if (b > 15) b = 15;
    return t->lut[(r << 8) | (g << 4) | b];
}
static void blend5(int abr, int br, int bgc, int bbc, int* rr, int* gg, int* bb)
{
    switch (abr)
    {
        case 0: *rr = (br + *rr) >> 1; *gg = (bgc + *gg) >> 1; *bb = (bbc + *bb) >> 1; break;
        case 1: *rr += br; *gg += bgc; *bb += bbc; break;
        case 2: *rr = br - *rr; *gg = bgc - *gg; *bb = bbc - *bb; break;
        default: *rr = br + (*rr >> 2); *gg = bgc + (*gg >> 2); *bb = bbc + (*bb >> 2); break;
    }
    *rr = *rr < 0 ? 0 : *rr > 31 ? 31 : *rr; *gg = *gg < 0 ? 0 : *gg > 31 ? 31 : *gg; *bb = *bb < 0 ? 0 : *bb > 31 ? 31 : *bb;
}
/* a pixel into the 8-bit pictures: no dither, no mask; semi-transparency reads the colour below through the palette */
static void plot8(tomba_t* t, int x, int y, int r, int g, int b, int tx, int semi, int raw)
{
    int rr, gg, bb, isemi;
    if (tx >= 0)
    {
        if (tx == 0) return;
        isemi = semi && (tx & 0x8000);
        if (raw) { rr = (tx & 31) << 3; gg = ((tx >> 5) & 31) << 3; bb = ((tx >> 10) & 31) << 3; }
        else { rr = ((tx & 31) << 3) * r >> 7; gg = (((tx >> 5) & 31) << 3) * g >> 7; bb = (((tx >> 10) & 31) << 3) * b >> 7; }
    }
    else { isemi = semi; rr = r; gg = g; bb = b; }
    rr = clamp255(rr); gg = clamp255(gg); bb = clamp255(bb);
    if (isemi)
    {
        uint16_t bg = t->pal[fb_rd(t, x, y)];
        rr >>= 3; gg >>= 3; bb >>= 3;
        blend5(t->abr, bg & 31, (bg >> 5) & 31, (bg >> 10) & 31, &rr, &gg, &bb);
        rr <<= 3; gg <<= 3; bb <<= 3;
        t->est += 1; TBN(9);
    }
    fb_wr(t, x, y, quant(t, x, y, rr, gg, bb));
}
static uint16_t texel(tomba_t* t, int u, int v, int cx, int cy, int depth, int px, int py)
{
    u = (u & ~(t->twmx * 8)) | ((t->twox & t->twmx) * 8);
    v = (v & ~(t->twmy * 8)) | ((t->twoy & t->twmy) * 8);
    u &= 255; v &= 255; TBN(3);
    t->est += 4u + (t->ntex++ & 1u);                 /* 4.5 clocks a texel (the cache's misses on average) */
    if (depth == 0) { uint16_t w = vrd(t, px + (u >> 2), py + v); return vrd(t, cx + ((w >> ((u & 3) * 4)) & 15), cy); }
    if (depth == 1) { uint16_t w = vrd(t, px + (u >> 1), py + v); return vrd(t, cx + ((w >> ((u & 1) * 8)) & 255), cy); }
    return vrd(t, px + u, py + v);
}
/* a pixel: into the pictures inside them, into VRAM (15 bits) outside */
static void plot(tomba_t* t, int x, int y, int r, int g, int b, int tx, int semi, int raw, int dith)
{
    uint16_t bg, out;
    int rr, gg, bb, isemi;
    t->est += 1;
    if (in_fb(x, y)) { TBN(1); plot8(t, x, y, r, g, b, tx, semi, raw); return; }
    t->est += 8; TBN(2);
    bg = vrd(t, x, y);
    if (t->mask_check && (bg & 0x8000)) return;
    if (tx >= 0)
    {
        if (tx == 0) return;
        isemi = semi && (tx & 0x8000);
        if (raw) { rr = (tx & 31) << 3; gg = ((tx >> 5) & 31) << 3; bb = ((tx >> 10) & 31) << 3; }
        else { rr = ((tx & 31) << 3) * r >> 7; gg = (((tx >> 5) & 31) << 3) * g >> 7; bb = (((tx >> 10) & 31) << 3) * b >> 7; }
    }
    else { isemi = semi; rr = r; gg = g; bb = b; }
    if (dith && t->dither_on) { int d = DITHER[y & 3][x & 3]; rr += d; gg += d; bb += d; }
    rr = clamp255(rr) >> 3; gg = clamp255(gg) >> 3; bb = clamp255(bb) >> 3;
    if (isemi) blend5(t->abr, bg & 31, (bg >> 5) & 31, (bg >> 10) & 31, &rr, &gg, &bb);
    out = (uint16_t)(rr | (gg << 5) | (bb << 10));
    if (tx >= 0) out |= (uint16_t)(tx & 0x8000);
    if (t->mask_set) out |= 0x8000;
    vwr(t, x, y, out);
}

typedef struct { int x, y, r, g, b, u, v; } tb_vtx;
#define TL(e, p, q) ((e) == 0 && !(((q)->y - (p)->y) < 0 || (((q)->y == (p)->y) && ((q)->x - (p)->x) > 0)))
/* a triangle: plane equations at pixel corners, the top-left rule; gradients in 16.16, divisions truncated */
static void tri(tomba_t* t, const tb_vtx* a, const tb_vtx* b, const tb_vtx* c, int tex, int gour, int semi, int raw, int cx, int cy,
                int depth, int px, int py)
{
    int64_t area = (int64_t)(b->x - a->x) * (c->y - a->y) - (int64_t)(c->x - a->x) * (b->y - a->y);
    int minx, maxx, miny, maxy, x, y, k;
    const tb_vtx *v0 = a, *v1 = b, *v2 = c;
    int64_t gx[5], gy[5], f0[5];
    t->est += 60; TBN(4);
    if (area == 0) return;
    if (area < 0) { v1 = c; v2 = b; area = -area; }
    minx = a->x; if (b->x < minx) minx = b->x; if (c->x < minx) minx = c->x;
    maxx = a->x; if (b->x > maxx) maxx = b->x; if (c->x > maxx) maxx = c->x;
    miny = a->y; if (b->y < miny) miny = b->y; if (c->y < miny) miny = c->y;
    maxy = a->y; if (b->y > maxy) maxy = b->y; if (c->y > maxy) maxy = c->y;
    if (maxx - minx >= 1024 || maxy - miny >= 512) return;
    if (minx < t->dx0) minx = t->dx0;
    if (maxx > t->dx1 + 1) maxx = t->dx1 + 1;
    if (miny < t->dy0) miny = t->dy0;
    if (maxy > t->dy1 + 1) maxy = t->dy1 + 1;
    {
        const int a0[5] = {v0->r, v0->g, v0->b, v0->u, v0->v}, a1[5] = {v1->r, v1->g, v1->b, v1->u, v1->v},
                  a2[5] = {v2->r, v2->g, v2->b, v2->u, v2->v};
        for (k = 0; k < 5; k++)
        {
            int64_t nx = (int64_t)(a1[k] - a0[k]) * (v2->y - v0->y) - (int64_t)(a2[k] - a0[k]) * (v1->y - v0->y);
            int64_t ny = (int64_t)(a2[k] - a0[k]) * (v1->x - v0->x) - (int64_t)(a1[k] - a0[k]) * (v2->x - v0->x);
            gx[k] = nx * 65536 / area; gy[k] = ny * 65536 / area; f0[k] = (int64_t)a0[k] * 65536;
        }
    }
    for (y = miny; y < maxy; y++)
    {
        TBN(5);
        for (x = minx; x < maxx; x++)
        {
            int64_t w0 = (int64_t)(v2->x - v1->x) * (y - v1->y) - (int64_t)(v2->y - v1->y) * (x - v1->x);
            int64_t w1 = (int64_t)(v0->x - v2->x) * (y - v2->y) - (int64_t)(v0->y - v2->y) * (x - v2->x);
            int64_t w2 = (int64_t)(v1->x - v0->x) * (y - v0->y) - (int64_t)(v1->y - v0->y) * (x - v0->x);
            int f[5];
            if (w0 < 0 || w1 < 0 || w2 < 0) continue;
            if (TL(w0, v1, v2) || TL(w1, v2, v0) || TL(w2, v0, v1)) continue;
            for (k = 0; k < 5; k++) f[k] = (int)((f0[k] + gx[k] * (x - v0->x) + gy[k] * (y - v0->y) + 0x8000) >> 16);
            if (!gour) { f[0] = a->r; f[1] = a->g; f[2] = a->b; }
            for (k = 0; k < 3; k++) f[k] = clamp255(f[k]);
            plot(t, x, y, f[0], f[1], f[2], tex ? texel(t, f[3], f[4], cx, cy, depth, px, py) : -1, semi, raw, gour || (tex && !raw));
        }
    }
}
static void rect(tomba_t* t, int x, int y, int w, int h, int r, int g, int b, int tex, int u0, int v0, int cx, int cy, int semi, int raw)
{
    int xx, yy;
    t->est += 20; TBN(11);
    for (yy = 0; yy < h; yy++)
    {
        int py = y + yy;
        if (py < t->dy0 || py > t->dy1) continue;
        t->est += 4;
        for (xx = 0; xx < w; xx++)
        {
            int pxl = x + xx;
            if (pxl < t->dx0 || pxl > t->dx1) continue;
            plot(t, pxl, py, r, g, b, tex ? texel(t, u0 + xx, v0 + yy, cx, cy, t->tdepth, t->tpx, t->tpy) : -1, semi, raw, 0);
        }
    }
}
static int cmd_len(uint32_t w)
{
    int c = (int)(w >> 24);
    if (c >= 0x20 && c <= 0x3F)
    {
        int nv = (c & 8) ? 4 : 3, tex = (c & 4) != 0, gour = (c & 0x10) != 0;
        return 1 + nv * (1 + tex) + (gour ? nv - 1 : 0);
    }
    if (c >= 0x40 && c <= 0x5F) return (c & 8) ? -1 : ((c & 0x10) ? 4 : 3);
    if (c >= 0x60 && c <= 0x7F) { int sz = (c >> 3) & 3; return 2 + ((c & 4) ? 1 : 0) + (sz == 0 ? 1 : 0); }
    if (c == 0x02) return 3;
    if (c >= 0x80 && c <= 0x9F) return 4;
    if (c >= 0xA0 && c <= 0xBF) return 3;
    if (c >= 0xC0 && c <= 0xDF) return 3;
    return 1;
}
static void exec(tomba_t* t, const uint32_t* p)
{
    int c = (int)(p[0] >> 24);
    t->est += 4; TBN(8);
    if (c >= 0x20 && c <= 0x3F)
    {
        int nv = (c & 8) ? 4 : 3, tex = (c & 4) != 0, gour = (c & 0x10) != 0, semi = (c & 2) != 0, raw = (c & 1) != 0;
        tb_vtx v[4]; int i, j = 0, cx = 0, cy = 0, depth = t->tdepth, px = t->tpx, py = t->tpy;
        uint32_t col = p[j++];
        for (i = 0; i < nv; i++)
        {
            if (gour && i) col = p[j++];
            v[i].r = (int)(col & 255); v[i].g = (int)((col >> 8) & 255); v[i].b = (int)((col >> 16) & 255);
            v[i].x = s11(p[j]) + t->offx; v[i].y = s11(p[j] >> 16) + t->offy; j++;
            if (tex)
            {
                v[i].u = (int)(p[j] & 255); v[i].v = (int)((p[j] >> 8) & 255);
                if (i == 0) { cx = (int)((p[j] >> 16) & 63) * 16; cy = (int)((p[j] >> 22) & 511); }
                if (i == 1)
                {
                    int tp = (int)(p[j] >> 16);
                    px = (tp & 15) * 64; py = ((tp >> 4) & 1) * 256; t->abr = (tp >> 5) & 3; depth = (tp >> 7) & 3;
                    t->tpx = px; t->tpy = py; t->tdepth = depth;
                }
                j++;
            }
            else v[i].u = v[i].v = 0;
        }
        tri(t, &v[0], &v[1], &v[2], tex, gour, semi, raw, cx, cy, depth, px, py);
        if (nv == 4) tri(t, &v[1], &v[2], &v[3], tex, gour, semi, raw, cx, cy, depth, px, py);
        return;
    }
    if (c >= 0x60 && c <= 0x7F)
    {
        int tex = (c & 4) != 0, sz = (c >> 3) & 3, j = 1, x, y, w, h, u = 0, vv = 0, cx = 0, cy = 0;
        uint32_t col = p[0];
        x = s11(p[j]) + t->offx; y = s11(p[j] >> 16) + t->offy; j++;
        if (tex) { u = (int)(p[j] & 255); vv = (int)((p[j] >> 8) & 255); cx = (int)((p[j] >> 16) & 63) * 16; cy = (int)((p[j] >> 22) & 511); j++; }
        if (sz == 0) { w = (int)(p[j] & 0x3FF); h = (int)((p[j] >> 16) & 0x1FF); } else w = h = (sz == 1) ? 1 : (sz == 2) ? 8 : 16;
        rect(t, x, y, w, h, (int)(col & 255), (int)((col >> 8) & 255), (int)((col >> 16) & 255), tex, u, vv, cx, cy, (c & 2) != 0, (c & 1) != 0);
        return;
    }
    if (c == 0x02)
    {
        int x = (int)(p[1] & 0x3F0), y = (int)((p[1] >> 16) & 0x1FF), w = (int)(((p[2] & 0x3FF) + 15) & ~15u), h = (int)((p[2] >> 16) & 0x1FF), xx, yy;
        uint16_t col = (uint16_t)(((p[0] >> 3) & 31) | (((p[0] >> 11) & 31) << 5) | (((p[0] >> 19) & 31) << 10));
        for (yy = 0; yy < h; yy++) for (xx = 0; xx < w; xx++)
        {
            int fx = (x + xx) & 1023, fy = (y + yy) & 511;
            if (in_fb(fx, fy)) { fb_wr(t, fx, fy, quant(t, fx, fy, (int)(p[0] & 0xF8), (int)((p[0] >> 8) & 0xF8), (int)((p[0] >> 16) & 0xF8))); t->est += 1; }
            else { vwr(t, fx, fy, col); t->est += 8; }
        }
        return;
    }
    if (c >= 0x80 && c <= 0x9F)
    {
        int sx = (int)(p[1] & 0x3FF), sy = (int)((p[1] >> 16) & 0x1FF), tx = (int)(p[2] & 0x3FF), ty = (int)((p[2] >> 16) & 0x1FF);
        int w = (int)(p[3] & 0xFFFF), h = (int)(p[3] >> 16), xx, yy;
        w = ((w - 1) & 0x3FF) + 1; h = ((h - 1) & 0x1FF) + 1;
        for (yy = 0; yy < h; yy++) for (xx = 0; xx < w; xx++)
        {
            uint16_t s = vrd(t, sx + xx, sy + yy), d = vrd(t, tx + xx, ty + yy);
            t->est += 26;
            if (t->mask_check && (d & 0x8000)) continue;
            vwr(t, tx + xx, ty + yy, (uint16_t)(s | (t->mask_set ? 0x8000 : 0)));
        }
        return;
    }
    if (c >= 0xA0 && c <= 0xBF)
    {
        t->up_x = (int)(p[1] & 0x3FF); t->up_y = (int)((p[1] >> 16) & 0x1FF);
        t->up_w = (int)(((p[2] & 0xFFFF) - 1) & 0x3FF) + 1;
        t->up_h = (int)((((p[2] >> 16) - 1) & 0x1FF)) + 1;
        t->up_i = 0; t->up_n = (uint32_t)(t->up_w * t->up_h);
        return;
    }
    switch (c)
    {
        case 0xE1: t->tpx = (int)(p[0] & 15) * 64; t->tpy = (int)((p[0] >> 4) & 1) * 256; t->abr = (int)((p[0] >> 5) & 3);
                   t->tdepth = (int)((p[0] >> 7) & 3); t->dither_on = (int)((p[0] >> 9) & 1); break;
        case 0xE2: t->twmx = (int)(p[0] & 31); t->twmy = (int)((p[0] >> 5) & 31); t->twox = (int)((p[0] >> 10) & 31); t->twoy = (int)((p[0] >> 15) & 31); break;
        case 0xE3: t->dx0 = (int)(p[0] & 0x3FF); t->dy0 = (int)((p[0] >> 10) & 0x1FF); break;
        case 0xE4: t->dx1 = (int)(p[0] & 0x3FF); t->dy1 = (int)((p[0] >> 10) & 0x1FF); break;
        case 0xE5: t->offx = s11(p[0]); t->offy = s11(p[0] >> 11); break;
        case 0xE6: t->mask_set = (int)(p[0] & 1); t->mask_check = (int)((p[0] >> 1) & 1); break;
        default: break;
    }
}
static void gp0(tomba_t* t, uint32_t w)
{
    if (t->up_i < t->up_n)
    {
        int k;
        for (k = 0; k < 2 && t->up_i < t->up_n; k++, t->up_i++)
        {
            int x = t->up_x + (int)(t->up_i % (uint32_t)t->up_w), y = t->up_y + (int)(t->up_i / (uint32_t)t->up_w);
            uint16_t d = vrd(t, x, y);
            t->est += 10;
            if (!(t->mask_check && (d & 0x8000))) vwr(t, x, y, (uint16_t)((w >> (16 * k)) | (t->mask_set ? 0x8000u : 0u)));
        }
        return;
    }
    if (t->in_line) { if ((w & 0xF000F000u) == 0x50005000u) t->in_line = 0; return; }
    if (t->fn == 0)
    {
        t->need = cmd_len(w);
        if (t->need < 0) { t->in_line = 1; return; }
    }
    t->fifo[t->fn++] = w;
    if (t->fn >= t->need) { exec(t, t->fifo); t->fn = 0; }
}

/* DrawOTag (twalk.sv): from the head, each node's tag gives the words after it (bits 31-24) and the next node
 * (23-0); it ends at a next node with bit 23 set */
static void draw_otag(tomba_t* t, uint32_t head)
{
    uint32_t node = head & 0x1FFFFF, nodes = 0;
    for (;;)
    {
        uint32_t tag = rd32(t, node), cnt = tag >> 24, k;
        nodes++;
        for (k = 0; k < cnt; k++) gp0(t, rd32(t, node + 4 + k * 4));
        if (tag & 0x800000u) break;
        node = tag & 0x1FFFFF;
        if (nodes > 1000000) { t->err |= 1; break; }
    }
    t->nodes = (uint16_t)nodes;
}


/* ------------------------------------------------------------------------------------------------ build 12: the CPU */
static uint32_t cpu_rd32(void* ctx, uint32_t a)
{
    tomba_t* t = (tomba_t*)ctx; int s; uint32_t w = ps1_word(t, a & 0x1FFFFF, &s);
    if (s) return (uint32_t)t->sram[w & 0x3FFFF] | (uint32_t)t->sram[(w + 1) & 0x3FFFF] << 16;
    return (uint32_t)psr(t, w) | (uint32_t)psr(t, w + 1) << 16;
}
static void cpu_wr32(void* ctx, uint32_t a, uint32_t v, uint32_t be)
{
    tomba_t* t = (tomba_t*)ctx; int s, k; uint32_t w = ps1_word(t, a & 0x1FFFFF, &s);
    for (k = 0; k < 2; k++)
    {
        uint32_t b = (be >> (2 * k)) & 3; uint16_t h = (uint16_t)(v >> (16 * k)), m, o;
        if (!b) continue;                                  /* (a half the write leaves alone is skipped) */
        m = (uint16_t)((b & 1 ? 0x00FF : 0) | (b & 2 ? 0xFF00 : 0));
        o = s ? t->sram[(w + k) & 0x3FFFF] : psr(t, w + k);
        h = (uint16_t)((o & ~m) | (h & m));
        if (s) t->sram[(w + k) & 0x3FFFF] = h; else psw(t, w + k, h);
    }
}
static int g16(int r) { r &= 31; return r == 4 || r == 12 || r == 20 || r == 26 || r == 27 || r == 29 || r == 30; }
static uint32_t cpu_gte_rd(void* ctx, int r)
{
    tomba_t* t = (tomba_t*)ctx;
    if (r >= 32 && r != 63) return t->gsh[r & 31];
    return gte_rd(t, r);
}
static void cpu_gte_wr(void* ctx, int r, uint32_t v)
{
    tomba_t* t = (tomba_t*)ctx;
    if (r >= 32 && r != 63) t->gsh[r & 31] = g16(r) ? (uint32_t)(int32_t)(int16_t)v : v;
    gte_wr(t, r, v);
}
static int cpu_gte_op(void* ctx, uint32_t ins)
{
    tomba_t* t = (tomba_t*)ctx; uint64_t e = t->est; int c = ins & 0x3F, n;
    if (c != 0x01 && c != 0x06 && c != 0x12 && c != 0x2D && c != 0x2E && c != 0x30) return -1;   /* (tgeo's six) */
    gte_op(t, ins);
    n = (int)(t->est - e); t->est = e;
    return n;
}
int64_t tomba_cpu_run(tomba_t* t, int64_t clocks)
{
    if (!t->b12 || t->cpu.stop) return 0;
    return tmips_run(&t->cpu, clocks);
}

/* ------------------------------------------------------------------------------------------------ the commands */
void tomba_reset(tomba_t* t, uint16_t key1, uint8_t id)
{
    uint16_t (*rd)(void*, uint32_t) = t->psram_rd;
    void (*wr)(void*, uint32_t, uint16_t) = t->psram_wr;
    uint16_t* sram = t->sram; void* ctx = t->ctx;
    memset(t, 0, sizeof *t);
    t->psram_rd = rd; t->psram_wr = wr; t->sram = sram; t->ctx = ctx;
    t->key1 = key1; t->id = id;
    t->dx1 = 1023; t->dy1 = 511;
    if (!unr[0]) unr_init();
    if (id == TB_ID12)
    {
        t->b12 = 1;
        t->cpu.ctx = t; t->cpu.rd32 = cpu_rd32; t->cpu.wr32 = cpu_wr32;
        t->cpu.gte_rd = cpu_gte_rd; t->cpu.gte_wr = cpu_gte_wr; t->cpu.gte_op = cpu_gte_op;
        t->cpu.mem_clk = 16; t->cpu.line_clk = 14;
        tmips_reset(&t->cpu, 0x80010000u);
    }
}

void tomba_word(tomba_t* t, uint16_t w)
{
    t->est = 2;
    t->taken++;
    if ((w & 0x6000u) == 0x2000u) { t->D = ((t->D << 13) | (w & 0x1FFFu)) & 0x7FFFFFFFFFull; return; }
    {
        uint32_t D = (uint32_t)t->D;
        int o = w & 0x7F, i = (w >> 7) & 63;
        if (t->b12) switch (o)
        {
            case 0: case 10: case 11: case 14: return;          /* (the model unit's: gone) */
            case 1: if (t->cpu.stop) cpu_gte_wr(t, i, D); return;
            case 2: if (t->cpu.stop) gte_op(t, D); return;
            case 8: if (t->cpu.stop) t->latch = cpu_gte_rd(t, i); return;
            case 17: t->latch = tmips_reg(&t->cpu, i); return;
            case 18: if (t->cpu.stop) tmips_set(&t->cpu, i, D); return;
            case 19:
                switch (i & 7)
                {
                    case 0: if (t->cpu.stop) tmips_go(&t->cpu, 0, 0); break;
                    case 1: if (t->cpu.stop) tmips_go(&t->cpu, 1, D); break;
                    case 2: tmips_halt(&t->cpu); break;
                    case 3: if (t->cpu.stop) tmips_flush(&t->cpu); break;
                    case 4: t->latch = t->cpu.h_pc; break;
                    case 5: t->latch = t->cpu.h_ir; break;
                    case 6: t->latch = t->cpu.h_ea; break;
                    default: break;
                }
                return;
            case 20: t->latch = cpu_rd32(t, t->xa); t->xa = (t->xa + 4) & 0x1FFFFF; t->est += 14; return;
            case 21: cpu_wr32(t, t->xa, D, 0xF); t->xa = (t->xa + 4) & 0x1FFFFF; t->est += 14; return;
            case 22: t->xa = D & 0x1FFFFC; return;
            default: break;
        }
        switch (o)
        {
            case 0: t->A[i & 7] = D; break;
            case 1: gte_wr(t, i, D); break;
            case 2: gte_op(t, D); break;
            case 8: t->latch = gte_rd(t, i); break;
            case 3: gp0(t, D); break;
            case 4: t->pal[(D >> 16) & 0xFF] = (uint16_t)D; break;
            case 5: t->lut[(D >> 8) & 0xFFF] = (uint8_t)D; break;
            case 6: break;
            case 7: t->shown = i & 1; break;
            case 9: t->pt[(D >> 16) & 511] = (uint16_t)(D & 0x1FFF); break;
            case 10:
                switch (i & 3)
                {
                    case 0: t->W = t->NP; t->P0 = t->NP; break;
                    case 1: wr32(t, t->W, D); t->W = (t->W + 4) & 0x1FFFFF; break;
                    case 2:
                    {
                        uint32_t ba = D & 0x1FFFFC, b = rd32(t, ba), p0 = rd32(t, t->P0);
                        wr32(t, t->P0, (p0 & 0xFF000000u) | (b & 0xFFFFFFu));
                        wr32(t, ba, (b & 0xFF000000u) | t->P0);
                        t->NP = t->W;
                        break;
                    }
                    default: t->NP = D & 0x1FFFFF; t->np_hi = D >> 21; break;
                }
                break;
            case 11:
            {
                uint32_t np = model_draw(t, i & 3, (i >> 2) & 1, (i >> 3) & 1, (i >> 4) & 1);
                t->NP = np & 0x1FFFFF;
                break;
            }
            case 12:
            {
                uint32_t n = (uint32_t)(t->D >> 21) & 0xFFF, k;
                t->COT = D & 0x1FFFFF;
                for (k = 0; k < n; k++) wr32(t, t->COT + k * 4, k == 0 ? 0x00FFFFFFu : ((t->COT + (k - 1) * 4) & 0x1FFFFF));
                t->clr = (int)n;
                break;
            }
            case 13: draw_otag(t, D & 0x1FFFFF); break;
            case 14: t->latch = t->NP; break;
            case 15: t->latch = (uint32_t)t->nodes << 16 | (uint32_t)(t->unsup ? 2 : 0); break;
            case 16: t->win_off = (unsigned)i; break;
            default: break;
        }
    }
}

int tomba_read(tomba_t* t, uint32_t a, uint16_t* data)
{
    a &= 0x3FFFFEu;
    if (!t->on)
    {
        uint16_t w;
        if (a < TB_CMD_BASE) return 0;
        w = (uint16_t)((a - TB_CMD_BASE) >> 1);
        if (t->key == 0 && w == TB_KEY0) t->key = 1;
        else if (t->key == 1 && w == t->key1) t->key = 2;
        else if (t->key == 2 && w == TB_KEY2) { t->key = 0; t->on = 1; *data = tomba_status(t); return 1; }
        else t->key = w == TB_KEY0 ? 1 : 0;
        return 0;
    }
    if (a < TB_WIN_BASE) return 0;
    if (a < TB_CMD_BASE)
    {
        uint32_t b = (t->shown ? 0x12000u : 0u) + (uint32_t)t->win_off * 2048u + (a - TB_WIN_BASE);
        *data = t->sram[(b >> 1) & 0x3FFFF];
        return 1;
    }
    {
        uint16_t w = (uint16_t)((a - TB_CMD_BASE) >> 1);
        if (tomba_queued(w)) tomba_word(t, w);
        if (w >= 1 && w <= 15)
        {
            *data = w == 1 ? (uint16_t)t->latch : w == 2 ? (uint16_t)(t->latch >> 16) : w == 3 ? t->lost : w == 6 ? t->taken :
                    (w == 4 && t->b12 && t->cpu.stop) ? 0x1000 :
                    (w == 14 && t->b12) ? (uint16_t)((t->cpu.stop ? 0x8000 : 0) | (t->cpu.why & 7) << 12) : 0;
            return 1;
        }
        *data = tomba_status(t);
        return 1;
    }
}
