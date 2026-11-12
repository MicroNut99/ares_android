/* orfx.c - the C model of OutRun CD32X's renderer core (see orfx.h). The definition the RTL follows: a line at
   a time, the layers in Cannonball's order, then the line's pixels turned into 32X palette slots in screen
   order (the first pixel of a colour gets the next slot; equal 32X colours share a slot).

   Based on Cannonball (Copyright Chris White) and MAME's OutRun video (Copyright Aaron Giles): see
   cannonball/docs/license.txt. */
#include <string.h>
#include "orfx.h"

#define W ORFX_W
#define H ORFX_H

static uint32_t be32(const uint8_t* p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)(p[0] << 8 | p[1]); }

void orfx_init(orfx_t* f, const uint8_t* sprites, const uint8_t* tiles, const uint8_t* road)
{
    memset(f, 0, sizeof(*f));
    f->gfx_sprites = sprites; f->gfx_tiles = tiles; f->gfx_road = road;
    f->reg[ORFX_R_XCLAMP] = 192;
    f->sel_m = -1;
}

/* OUTRUN.PAK's road (512 bytes a pixel row, rows 0-511; the centre bit 2 is left out: the core puts it back) */
void orfx_road_pack(const uint8_t* roads512, uint8_t* out)
{
    for (int r = 0; r < 512; r++)
        for (int w = 0; w < 64; w++)
        {
            uint16_t v = 0;
            for (int i = 0; i < 8; i++) v = (uint16_t)(v << 2 | (roads512[r * 512 + w * 8 + i] & 3));
            out[(r * 64 + w) * 2] = (uint8_t)(v >> 8);
            out[(r * 64 + w) * 2 + 1] = (uint8_t)v;
        }
}

/* ---------------------------------------------------------------------------------------------- the stream */
void orfx_sync(orfx_t* f)
{
    f->data_left = 0;
    if (f->sel_m < -1) f->sel_m = -1;               /* (a SELECT waiting for its address: dropped) */
    f->chk_pend = 0;
    f->sum = 0; f->cnt = 0;
    f->go_ok = 0; f->bad = 0; f->gap = 0; f->poison = 0;
    f->fresh = 1;
    f->n_pend = 0;
    f->q_tog = 0;                                   /* the next word's A17 is 1 */
    f->q_key = 0;
}

static void fail(orfx_t* f)                         /* the unit fails (counted once) */
{
    if (!f->bad) f->n_bad++;
    f->bad = 1;
    f->go_ok = 0;
    f->n_pend = 0;                                  /* (its writes: dropped) */
}

int orfx_cmd(orfx_t* f, int a17, uint16_t a)
{
    if (a17 == f->q_tog)
    {
        if (a == f->q_last) { f->n_dup++; return 0; }   /* the same read seen twice */
        f->q_last = a; f->n_taken++;                /* a word was lost before this one: ignore all to the SYNC */
        f->q_key = (uint16_t)(f->q_key + ORFX_KEY_STEP);
        f->fresh = 0;
        f->gap = 1; f->poison = 1;
        fail(f);
        return 2;
    }
    f->q_tog = a17; f->q_last = a; f->n_taken++;
    f->q_key = (uint16_t)(f->q_key + ORFX_KEY_STEP);
    orfx_word(f, (uint16_t)(a ^ f->q_key));
    return 1;
}

static void mem_write(orfx_t* f, int m, uint32_t a, uint16_t v)
{
    switch (m)
    {
        case ORFX_M_TILE: f->tile[a & 0x7fff] = v; break;
        case ORFX_M_TEXT: f->text[a & 0x7ff] = v; break;
        case ORFX_M_SPR:  f->spr[a & 0x3ff] = v; break;
        case ORFX_M_ROAD: f->road[a & 0x7ff] = v; break;
        case ORFX_M_PAL:  f->pal[a & 0xfff] = v; break;
        case ORFX_M_REG:  f->reg[a & (ORFX_R_COUNT - 1)] = v; break;
    }
}

void orfx_word(orfx_t* f, uint16_t w)
{
    int fresh = f->fresh;
    f->fresh = 0;
    if (f->poison) return;                          /* after a gap: nothing until the SYNC */
    if (f->chk_pend == 2) { f->chk_sok = w == f->sum; f->chk_pend = 1; return; }   /* CHECK's sum */
    if (f->chk_pend == 1)                           /* CHECK's count */
    {
        f->chk_pend = 0;
        f->n_chk++;
        if (f->chk_sok && w == f->cnt && !f->bad)
        {
            f->go_ok = 1;
            for (int i = 0; i < f->n_pend; i++) mem_write(f, f->pend_m[i], f->pend_a[i], f->pend_d[i]);
            f->n_pend = 0;
        }
        else fail(f);
        f->sum = 0; f->cnt = 0;
        return;
    }
    int cmdpos = !f->data_left && f->sel_m >= -1;
    if (cmdpos && (w & 0xf000) == ORFX_OP_CHECK) { f->chk_pend = 2; return; }
    if (!cmdpos || (w & 0xf000) == ORFX_OP_SELECT || (w & 0xf000) == ORFX_OP_DATA)
    {                                               /* summed: SELECT, DATA, their words; a GO must follow a CHECK */
        f->sum = (uint16_t)(f->sum + w); f->cnt++;
        f->go_ok = 0;
    }
    if (f->data_left)                               /* data for the selected memory: waits for the CHECK */
    {
        if (f->n_pend == ORFX_PEND) { fail(f); f->poison = 1; return; }
        f->pend_m[f->n_pend] = (int8_t)f->sel_m; f->pend_a[f->n_pend] = (uint16_t)f->sel_a; f->pend_d[f->n_pend] = w;
        f->n_pend++;
        f->sel_a++;
        f->data_left--;
        return;
    }
    if (f->sel_m < -1)                              /* the address after SELECT */
    {
        f->sel_m = -2 - f->sel_m;
        f->sel_a = w;
        return;
    }
    switch (w & 0xf000)
    {
        case ORFX_OP_SELECT: f->sel_m = -2 - (w & 7); break;
        case ORFX_OP_DATA:   f->data_left = w & 0xfff; break;
        case ORFX_OP_GO:                            /* only straight after a right CHECK */
            if (f->go_ok) orfx_draw(f, w & 3, f->line13);
            else fail(f);
            f->go_ok = 0;
            break;
        case ORFX_OP_SHOW: if (fresh) f->shown = w & 1; break;
        case ORFX_OP_LOAD: if (fresh) f->load_req = 1; break;
        default: break;                             /* NOP */
    }
}

/* ---------------------------------------------------------------------------------------------- a line */
static void road_background(orfx_t* f, int y, uint16_t* lb)
{
    int d0 = f->road[0x000 + y], d1 = f->road[0x100 + y], c = -1;
    switch (f->reg[ORFX_R_ROADCTL] & 3)
    {
        case 0: if (d0 & 0x800) c = d0 & 0x7f; break;
        case 1: if (d0 & 0x800) c = d0 & 0x7f; else if (d1 & 0x800) c = d1 & 0x7f; break;
        case 2: if (d1 & 0x800) c = d1 & 0x7f; else if (d0 & 0x800) c = d0 & 0x7f; break;
        case 3: if (d1 & 0x800) c = d1 & 0x7f; break;
    }
    if (c >= 0)
        for (int x = 0; x < W; x++) lb[x] = (uint16_t)(c | 0x780);
}

static int road_pix(orfx_t* f, int solid, int row, int h)     /* hpos h (12 bits) of a road row */
{
    if (h >= 0x200) return 3;
    if (solid) return 3;                                       /* Cannonball's dummy row: all 3 */
    uint16_t w = be16(f->gfx_road + (row * 64 + (h >> 3)) * 2);
    int v = (w >> (14 - 2 * (h & 7))) & 3;
    if (v == 3 && h >= 248 && h < 256) v = 7;                  /* HWRoad::decode_road's centre */
    return v;
}

static void road_foreground(orfx_t* f, int y, uint16_t* lb)
{
    static const uint8_t pmap[2][8] = { { 0x80,0x81,0x81,0x87,0,0,0,0x00 }, { 0x81,0x81,0x81,0x8f,0,0,0,0x80 } };
    const uint16_t* rr = f->road;
    uint32_t d0 = rr[0x000 + y], d1 = rr[0x100 + y];
    if ((d0 & 0x800) && (d1 & 0x800)) return;
    int ctl = f->reg[ORFX_R_ROADCTL], control = ctl & 3;
    int i0 = (ctl & 4) ? y : (int)(d0 & 0x1ff), i1 = (ctl & 4) ? 0x100 + y : (int)(d1 & 0x1ff);
    int s0 = (d0 & 0x800) != 0, s1 = (d1 & 0x800) != 0;
    int r0 = 0x000 + ((d0 >> 1) & 0xff), r1 = 0x100 + ((d1 >> 1) & 0xff);
    int hpos0 = rr[0x200 + i0] & 0xfff, color0 = rr[0x600 + i0];
    int hpos1 = rr[0x400 + i1] & 0xfff, color1 = rr[0x600 + i1];
    uint16_t ct[32];
    ct[0x00] = 0x400 ^ 0x00 ^ ((color0 >> 0) & 1);
    ct[0x01] = 0x400 ^ 0x02 ^ ((color0 >> 1) & 1);
    ct[0x02] = 0x400 ^ 0x04 ^ ((color0 >> 2) & 1);
    ct[0x03] = (d0 & 0x200) ? ct[0x00] : (uint16_t)(0x420 ^ 0x00 ^ ((color0 >> 8) & 0xf));
    ct[0x07] = 0x400 ^ 0x06 ^ ((color0 >> 3) & 1);
    ct[0x10] = 0x400 ^ 0x08 ^ ((color1 >> 4) & 1);
    ct[0x11] = 0x400 ^ 0x0a ^ ((color1 >> 5) & 1);
    ct[0x12] = 0x400 ^ 0x0c ^ ((color1 >> 6) & 1);
    ct[0x13] = (d1 & 0x200) ? ct[0x10] : (uint16_t)(0x420 ^ 0x10 ^ ((color1 >> 8) & 0xf));
    ct[0x17] = 0x400 ^ 0x0e ^ ((color1 >> 7) & 1);
    const int s16_x = 0x5f8;
    hpos0 = (hpos0 - s16_x) & 0xfff;
    hpos1 = (hpos1 - s16_x) & 0xfff;
    if ((control == 0 && s0) || (control == 3 && s1)) return;
    f->n.road_words += (control == 1 || control == 2) ? 80 : 40;
    for (int x = 0; x < W; x++)
    {
        int p0 = road_pix(f, s0, r0, hpos0), p1 = road_pix(f, s1, r1, hpos1);
        uint16_t c;
        if (control == 0) c = ct[0x00 + p0];
        else if (control == 3) c = ct[0x10 + p1];
        else c = ((pmap[control - 1][p0] >> p1) & 1) ? ct[0x10 + p1] : ct[0x00 + p0];
        lb[x] = c;
        hpos0 = (hpos0 + 1) & 0xfff;
        hpos1 = (hpos1 + 1) & 0xfff;
    }
}

static void put8(uint16_t* lb, int x, uint32_t p0, uint32_t pal)
{
    for (int i = 0; i < 8; i++)
    {
        uint32_t c = (p0 >> (28 - 4 * i)) & 0xf;
        if (c && x + i >= 0 && x + i < W) lb[x + i] = (uint16_t)(pal + c);
    }
}

static void tile_layer(orfx_t* f, int y, uint16_t* lb, int page_index)
{
    uint32_t eff = f->text[0x740 + page_index];
    uint32_t xs = f->text[0x74c + page_index], ys = f->text[0x748 + page_index];
    if (xs & 0x8000) xs = f->text[0x7c0 + 0x20 * page_index];
    if (ys & 0x8000) ys = f->text[0x78b + 0x20 * page_index];
    int xclamp = (int16_t)f->reg[ORFX_R_XCLAMP];
    uint32_t sy = (uint32_t)y + (ys & 0x1ff);
    int my = (int)(sy >> 3) & 63, r = (int)(sy & 7);
    uint32_t o = (uint32_t)(xclamp - (int)(uint16_t)xs) & 0x3ff;
    int mx = (int)(o >> 3) & 127, x = -(int)(o & 7);
    for (int k = 0; k < 41; k++, x += 8, mx = (mx + 1) & 127)
    {
        uint32_t act = (my < 32) ? ((mx < 64) ? eff : eff >> 4) : ((mx < 64) ? eff >> 8 : eff >> 12);
        uint32_t data = f->tile[0x800 * (act & 15) + ((64 * my) & 0x7ff) + (mx & 0x3f)];
        f->n.map_words++;
        if (data >> 15) continue;                              /* priority 0 only (video.cpp) */
        uint32_t code = data & 0x1fff;
        code = ((uint32_t)f->reg[code >> 12 ? ORFX_R_BANK1 : ORFX_R_BANK0] * 0x1000 + (code & 0xfff)) & 0x1fff;
        if (!code || x >= W) continue;
        f->n.tile_words++;
        put8(lb, x, be32(f->gfx_tiles + ((code << 3) + r) * 4), ((data >> 6) & 0x7f) << 3);
    }
}

static void text_layer(orfx_t* f, int y, uint16_t* lb)
{
    int my = y >> 3, r = y & 7;
    for (int mx = 24; mx < 64; mx++)
    {
        uint32_t code = f->text[my * 64 + mx];
        f->n.map_words++;
        if (!(code >> 15)) continue;                           /* priority 1 only */
        uint32_t pal = ((code >> 9) & 7) << 3;
        code = ((code & 0x1ff) + f->reg[ORFX_R_BANK0] * 0x1000) & 0x1fff;
        if (!code) continue;
        f->n.tile_words++;
        put8(lb, 8 * mx - 192, be32(f->gfx_tiles + ((code << 3) + r) * 4), pal);
    }
}

static void sprites(orfx_t* f, int y, uint16_t* lb)
{
    const uint16_t* s = f->spr;
    for (int d = 0; d < 0x400; d += 8)
    {
        f->n.spr_scan++;
        if (s[d] & 0x8000) break;
        uint32_t w0 = s[d], w2 = s[d + 2], w3 = s[d + 3], w4 = s[d + 4], w5 = s[d + 5];
        if (((w3 >> 12) & 3) != 3) continue;                   /* render(8): priority 3 */
        if (w0 & 0x5000) continue;
        int height = (int)(w5 >> 8) + 1;
        int top = (int)(w0 & 0x1ff) - 0x100;
        int ydelta = (w4 & 0x8000) ? 1 : -1;
        int k = ydelta > 0 ? y - top : top - y;
        if (k < 0 || k >= height) continue;
        f->n.spr_lines++;
        int bank = (int)((w0 >> 9) & 3);                       /* % 4 banks */
        uint32_t addr = s[d + 1];
        uint32_t pitch = ((w2 >> 1) | ((w4 & 0x1000) << 3)) >> 8;
        int xpos = s[d + 6];
        int shadow = (w3 >> 14) & 1;
        uint32_t vzoom = w3 & 0x7ff, hzoom = w4 & 0x7ff;
        int flip = (int)((~w4 >> 14) & 1);
        int xdelta = (w4 & 0x2000) ? 1 : -1;
        uint32_t color = 0x800 + ((w5 & 0x7f) << 4);
        if (xpos < 0x80 && xdelta < 0) xpos += 0x200;
        xpos -= 0xbe;
        if (vzoom < 0x40) vzoom = 0x40;
        if (hzoom < 0x40) hzoom = 0x40;
        uint32_t laddr = addr + pitch * ((uint32_t)k * vzoom >> 9);
        const uint8_t* bankp = f->gfx_sprites + (uint32_t)bank * 0x40000;
        uint16_t cur = (uint16_t)(flip ? laddr + 1 : laddr - 1);
        uint32_t xacc = 0, lw = 0;
        int x = xpos;
        while ((xdelta > 0 && x < W) || (xdelta < 0 && x >= 0))
        {
            cur = (uint16_t)(flip ? cur - 1 : cur + 1);
            uint32_t px = be32(bankp + (uint32_t)cur * 4);
            f->n.spr_words++;
            if (++lw > f->n.spr_line_max) f->n.spr_line_max = lw;
            for (int i = 0; i < 8; i++)
            {
                uint32_t pix = (px >> (flip ? 4 * i : 28 - 4 * i)) & 0xf;
                f->n.spr_nibbles++;
                while (xacc < 0x200)
                {
                    if (x >= 0 && x < W && pix != 0 && pix != 15)
                    {
                        f->n.spr_pixels++;
                        if (shadow && pix == 0xa) lb[x] = (uint16_t)((lb[x] & 0xfff) + 0x1000);
                        else lb[x] = (uint16_t)(pix | color);
                    }
                    x += xdelta;
                    xacc += hzoom;
                }
                xacc -= 0x200;
            }
            if (flip ? (px & 0x0f000000) == 0x0f000000 : (px & 0xf0) == 0xf0) break;
        }
    }
}

/* ------------------------------------------------------------------------------------------- the colours */
static uint16_t to32x(const orfx_t* f, uint32_t p)
{
    uint16_t c = f->pal[p & 0xfff];
    if (p & 0x1000) c = (uint16_t)((c >> 1) & 0x3def);
    return c ? c : 0x0400;
}

/* ---------------------------------------------------------------------------------------------- a frame */
void orfx_draw(orfx_t* f, int buf, uint16_t* line13)
{
    static uint8_t map[0x2000];                 /* pixel value -> slot, this frame (0: none yet) */
    static uint8_t hash[512];                   /* 32X colour -> slot */
    uint16_t lb[W];
    int next = ORFX_SLOT_FIRST;
    const int rle = (buf >> 1) & 1;
    buf &= 1;
    memset(map, 0, sizeof(map));
    memset(hash, 0, sizeof(hash));
    memset(&f->n, 0, sizeof(f->n));
    f->overflow[buf] = 0;
    memset(f->slotpal[buf], 0, sizeof(f->slotpal[buf]));
    for (int y = 0; y < H; y++)
    {
        memset(lb, 0, sizeof(lb));              /* (Cannonball keeps the last frame where nothing draws) */
        if (f->reg[ORFX_R_FLAGS] & 1)
        {
            road_background(f, y, lb);
            tile_layer(f, y, lb, 1);            /* background layer */
            tile_layer(f, y, lb, 0);            /* foreground layer */
            if (f->reg[ORFX_R_FLAGS] & 2) road_foreground(f, y, lb);
            sprites(f, y, lb);
            text_layer(f, y, lb);
        }
        if (line13) memcpy(line13 + y * W, lb, sizeof(lb));
        uint8_t* out = f->pic[buf] + y * W;
        for (int x = 0; x < W; x++)
        {
            uint32_t p = lb[x] & 0x1fff;
            uint32_t s = map[p];
            if (!s)
            {
                uint16_t col = to32x(f, p);
                uint32_t h = ((uint32_t)col * 40503u >> 16) & 511;
                f->n.new_colours++;
                while (hash[h] && f->slotpal[buf][hash[h]] != col) { h = (h + 1) & 511; f->n.hash_probes++; }
                if (hash[h]) s = hash[h];
                else if (next <= ORFX_SLOT_LAST)
                {
                    s = (uint32_t)next++;
                    hash[h] = (uint8_t)s;
                    f->slotpal[buf][s] = col;
                }
                else { s = ORFX_SLOT_LAST; f->overflow[buf]++; }
                map[p] = (uint8_t)s;
            }
            out[x] = (uint8_t)s;
        }
    }
    f->slots[buf] = next - ORFX_SLOT_FIRST;
    memcpy(f->rawpic, f->pic[buf], sizeof(f->rawpic));
    memset(f->lt[buf], 0, sizeof(f->lt[buf]));
    if (rle)                                    /* the same picture as runs: (pixels - 1) << 8 | slot */
    {
        static uint16_t w[ORFX_RLE_MAX];
        uint32_t n = 0;
        int ovf = 0;
        for (int y = 0; y < H; y++)
        {
            const uint8_t* l = f->rawpic + y * W;
            f->lt[buf][y] = (uint16_t)n;
            for (int x = 0; x < W; )
            {
                int e = x + 1;
                while (e < W && l[e] == l[x] && e - x < 256) e++;
                if (n < ORFX_RLE_MAX) w[n++] = (uint16_t)((e - x - 1) << 8 | l[x]); else ovf = 1;
                x = e;
            }
        }
        f->lt[buf][224] = (uint16_t)n;
        f->lt[buf][225] = (uint16_t)(1 | ovf << 1);
        for (uint32_t i = 0; i < n; i++) { f->pic[buf][2 * i] = (uint8_t)(w[i] >> 8); f->pic[buf][2 * i + 1] = (uint8_t)w[i]; }
    }
    else f->lt[buf][224] = ORFX_RLE_MAX;        /* (bytes / 2; not run-length) */
    {   /* the picture's sum (kit 12): its words two at a time as 32-bit numbers, the first the high half (an odd last
           word: the high half alone), added up modulo 2^32 - entries 226 (high) and 227; flags bit 2: it is there */
        const uint32_t nw = f->lt[buf][224];
        uint32_t sum = 0;
        for (uint32_t i = 0; i < nw; i++)
        {
            const uint32_t v = (uint32_t)f->pic[buf][2 * i] << 8 | f->pic[buf][2 * i + 1];
            sum += (i & 1) ? v : v << 16;
        }
        f->lt[buf][225] |= 4;
        f->lt[buf][226] = (uint16_t)(sum >> 16);
        f->lt[buf][227] = (uint16_t)sum;
    }
    f->frames++;
}

/* The RTL's clocks for the last frame at 50 MHz (an estimate: the RTL's own counts replace it once it runs).
   A line: the road from the PSRAM (5 clocks a word), the tile maps and graphics from the SRAM (2 clocks a word,
   tile graphics 2 words), the sprite list scan (1 clock an entry), sprite words from the PSRAM (2 words, 10
   clocks, fetched ahead) against one clock a nibble or a pixel, the line out (2 clocks a word, 160 words), the
   colours (one clock a pixel, 6 for a new colour + 2 a probe). */
uint32_t orfx_cycles(const orfx_t* f)
{
    uint32_t road = f->n.road_words * 5;
    uint32_t tiles = f->n.map_words * 2 + f->n.tile_words * 4;
    uint32_t spr = f->n.spr_scan + f->n.spr_lines * 8;
    uint32_t sprw = f->n.spr_words * 10, sprp = f->n.spr_nibbles + f->n.spr_pixels;
    spr += sprw > sprp ? sprw : sprp;
    uint32_t out = H * (160 * 2) + H * W + f->n.new_colours * 6 + f->n.hash_probes * 2;
    return road + tiles + spr + out;
}
