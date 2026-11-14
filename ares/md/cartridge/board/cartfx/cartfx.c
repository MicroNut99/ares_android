/* cartfx.c - the compositor's reference model (cartfx.h says what it does) */
#include <string.h>
#include "cartfx.h"

void cfx_reset(cfx_t* fx, uint16_t key1, uint8_t id) {
    uint8_t (*psram)(void*, uint32_t) = fx->psram;
    uint16_t* sram = fx->sram;
    void* ctx = fx->ctx;
    memset(fx, 0, sizeof(*fx));
    fx->psram = psram; fx->sram = sram; fx->ctx = ctx;
    fx->key1 = key1; fx->id = id;
    fx->cols = 20; fx->rows = 22;
}

static int cfx_words(uint16_t w0) {      /* a command's length from its first word */
    switch ((w0 >> 10) & 15) {
    case CFX_OP_SPRITE: return 9;
    case CFX_OP_LAYER:  return 11;
    case CFX_OP_SCROLL: return 3;
    case CFX_OP_CELLS:  return 3;
    case CFX_OP_POKE:   return 5;
    case CFX_OP_UPLOAD: return 6;
    case CFX_OP_LUT:    return 3;
    case CFX_OP_SETUP:  return 7;
    default:            return 1;
    }
}

static uint16_t ps_word(cfx_t* fx, uint32_t a) {  /* a big-endian PSRAM word (a even) */
    a &= 0xFFFFFE;
    return (uint16_t)(fx->psram(fx->ctx, a) << 8 | fx->psram(fx->ctx, a + 1));
}

static uint32_t addr24(uint16_t hi, uint16_t lo) { return ((uint32_t)(hi & 0x7FF) << 13 | (lo & 0x1FFF)) & 0xFFFFFF; }
static uint32_t addr18(uint16_t hi, uint16_t lo) { return ((uint32_t)(hi & 0x1F) << 13 | (lo & 0x1FFF)) & 0x3FFFF; }
static int s13(uint16_t v) { v &= 0x1FFF; return v & 0x1000 ? (int)v - 0x2000 : (int)v; }

/* the layer's pixel at picture (px, py): -1 nothing, else the colour drawn */
static int cfx_layer_pix(cfx_t* fx, const cfx_layer_t* L, int px, int py) {
    uint32_t c, light = 0;
    if (!(L->flags & CFX_L_TILES)) {
        uint32_t W = L->w, H = L->h;
        if (W == 0 || H == 0) return -1;
        uint32_t lx = ((uint32_t)px + L->x) % W, ly = ((uint32_t)py + L->y) % H;
        uint32_t n = ly * W + lx;
        if (L->flags & CFX_L_4BPP) {
            uint8_t b = fx->psram(fx->ctx, (L->pix + n / 2) & 0xFFFFFF);
            c = (n & 1) ? (b & 15u) : (b >> 4);
        } else c = fx->psram(fx->ctx, (L->pix + n) & 0xFFFFFF);
    } else {
        uint32_t t = L->tile, MW = L->w, MH = L->h;
        if (t == 0 || MW == 0 || MH == 0) return -1;
        uint32_t lx = ((uint32_t)px + L->x) % (MW * t), ly = ((uint32_t)py + L->y) % (MH * t);
        uint32_t mx = lx / t, my = ly / t, u = lx % t, v = ly % t;
        uint32_t cell = my * MW + mx;
        uint16_t e = (L->flags & CFX_L_MAPSRAM) ? fx->sram[(L->map + cell) & 0x3FFFF]
                                                : ps_word(fx, (L->map + 2 * cell) & 0xFFFFFF);
        if ((e & 0xFFF) == 0) return -1;
        if (L->brk && (fx->sram[(fx->brk + cell / 16) & 0x3FFFF] >> (cell % 16) & 1)) return -1;
        if ((L->flags & CFX_L_HFLIP) && (e & 0x1000)) u = t - 1 - u;
        uint32_t n = (uint32_t)(e & 0xFFF) * t * t + v * t + u;
        if (L->flags & CFX_L_4BPP) {
            uint8_t b = fx->psram(fx->ctx, (L->pix + n / 2) & 0xFFFFFF);
            c = (n & 1) ? (b & 15u) : (b >> 4);
        } else c = fx->psram(fx->ctx, (L->pix + n) & 0xFFFFFF);
        light = e >> 12;
    }
    if (c == 0 && (!(L->flags & CFX_L_OPAQUE) || (L->flags & CFX_L_OVER))) return -1;
    c = (c + L->off) & 255;
    if ((L->flags & CFX_L_SHADE) && (L->flags & CFX_L_TILES)) c = fx->shade[light * 256 + c];
    return (int)c;
}

/* the estimate's share of a layer in a cell (see est in cartfx.h): its set-up, its pixels at the PSRAM's word every
   6 clocks (3 clocks a pixel at 8 bits, 1.5 at 4), a tile map's spans and the entries read; in half clocks */
static uint32_t cfx_est_layer(const cfx_t* fx, const cfx_layer_t* L, int x0, int y0) {
    uint32_t h = 2 * 56, px = 0;
    if (!(L->flags & CFX_L_TILES)) {
        if (L->w == 0 || L->h == 0) return 2 * 6;
        px = 128; h += 2 * 8 * 8;                  /* each row's first word: a jump */
    } else {
        uint32_t t = L->tile, MW = L->w, MH = L->h, seen[48], ns = 0;
        if (t == 0 || MW == 0 || MH == 0) return 2 * 6;
        for (int r = 0; r < 8; r++) {
            uint32_t ly = ((uint32_t)(y0 + r) + L->y) % (MH * t);
            for (int i = 0; i < 16; ) {
                uint32_t lx = ((uint32_t)(x0 + i) + L->x) % (MW * t), u = lx % t, cell = (ly / t) * MW + lx / t;
                int span = (int)(t - u); if (span > 16 - i) span = 16 - i;
                uint16_t e = (L->flags & CFX_L_MAPSRAM) ? fx->sram[(L->map + cell) & 0x3FFFF]
                                                        : (uint16_t)(fx->psram(fx->ctx, (L->map + 2 * cell) & 0xFFFFFF) << 8 |
                                                                     fx->psram(fx->ctx, (L->map + 2 * cell + 1) & 0xFFFFFF));
                int brk = L->brk && (fx->sram[(fx->brk + cell / 16) & 0x3FFFF] >> (cell % 16) & 1);
                uint32_t k = 0; while (k < ns && seen[k] != cell) k++;
                if (k == ns) { if (ns < 48) seen[ns++] = cell; h += 2 * ((L->flags & CFX_L_MAPSRAM) ? 9 : 18) + (L->brk ? 2 * 10 : 0); }
                h += 2 * 2;
                if ((e & 0xFFF) != 0 && !brk) { px += (uint32_t)span; h += 2 * 8; }
                i += span;
            }
        }
    }
    return h + px * ((L->flags & CFX_L_4BPP) ? 3u : 6u);
}

static void cfx_compose(cfx_t* fx, int cx, int cy, int b) {
    uint8_t buf[8][16];
    memset(buf, 0, sizeof(buf));
    int x0 = cx * 16, y0 = cy * 8;
    uint32_t est = 2 * 90 + 2 * (uint32_t)fx->nspr;     /* the cell: its set-up and write-out; the list scanned */
    for (int l = 0; l < 4; l++) {
        const cfx_layer_t* L = &fx->layer[l];
        if (!(L->flags & CFX_L_ON) || (L->flags & CFX_L_OVER)) continue;
        est += cfx_est_layer(fx, L, x0, y0);
        for (int r = 0; r < 8; r++) for (int i = 0; i < 16; i++) {
            int c = cfx_layer_pix(fx, L, x0 + i, y0 + r);
            if (c >= 0) buf[r][i] = (uint8_t)c;
        }
    }
    for (int s = 0; s < fx->nspr; s++) {
        const cfx_sprite_t* S = &fx->spr[s];
        int drawn = 0;
        for (int r = 0; r < 8; r++) {
            int rowpx = 0;
            for (int i = 0; i < 16; i++) {
                int px = x0 + i, py = y0 + r;
                int di = px - S->x, dj = py - S->y;
                if (di < 0 || dj < 0 || di >= S->w || dj >= S->h) continue;
                rowpx++;
                int si = (S->flags & 1) ? S->w - 1 - di : di, sj = (S->flags & 2) ? S->h - 1 - dj : dj;
                uint8_t v = fx->psram(fx->ctx, (S->src + (uint32_t)sj * S->stride + (uint32_t)si) & 0xFFFFFF);
                if (v == 0) continue;
                uint8_t c = (uint8_t)(v + S->off);
                if (S->flags & 4) { c = fx->psram(fx->ctx, (fx->blend + (uint32_t)c * 256u + buf[r][i]) & 0xFFFFFF); est += 2 * 14; }
                buf[r][i] = c;
            }
            if (rowpx) { drawn = 1; est += 2 * 10 + 6 * (uint32_t)rowpx; }
        }
        if (drawn) est += 2 * 12;
    }
    for (int l = 0; l < 4; l++) {
        const cfx_layer_t* L = &fx->layer[l];
        if (!(L->flags & CFX_L_ON) || !(L->flags & CFX_L_OVER)) continue;
        est += cfx_est_layer(fx, L, x0, y0);
        for (int r = 0; r < 8; r++) for (int i = 0; i < 16; i++) {
            int c = cfx_layer_pix(fx, L, x0 + i, y0 + r);
            if (c >= 0) buf[r][i] = (uint8_t)c;
        }
    }
    fx->est += est / 2;
    for (int r = 0; r < 8; r++) for (int i = 0; i < 16; i += 2)
        fx->sram[cfx_pic_word(b, fx->cols, x0 + i, y0 + r)] = (uint16_t)(buf[r][i] << 8 | buf[r][i + 1]);
}

static void cfx_go(cfx_t* fx) {
    int b = fx->shown ^ 1, n = 0;
    fx->est = 2u * 24u * 26u;                /* the rows: the marks read, the cell map written */
    for (int cy = 0; cy < CFX_ROWS; cy++) {
        uint32_t m = 0;
        if (cy < fx->rows) m = fx->full ? ((1u << fx->cols) - 1) : (fx->mark[cy] & ((1u << fx->cols) - 1));
        for (int cx = 0; cx < fx->cols; cx++) if (m >> cx & 1) { cfx_compose(fx, cx, cy, b); n++; }
        fx->sram[CFX_MAPWORD(b, 2 * cy)] = (uint16_t)(m & 0xFFFF);
        fx->sram[CFX_MAPWORD(b, 2 * cy + 1)] = (uint16_t)(m >> 16);
    }
    fx->cnt[1]++; fx->cnt[2] = (uint16_t)n; fx->cnt[3] = (uint16_t)fx->nspr;
}

static void cfx_mark_sprite(cfx_t* fx, const cfx_sprite_t* S) {
    int PW = fx->cols * 16, PH = fx->rows * 8;
    int x0 = S->x, y0 = S->y, x1 = S->x + S->w - 1, y1 = S->y + S->h - 1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > PW - 1) x1 = PW - 1;
    if (y1 > PH - 1) y1 = PH - 1;
    if (x0 > x1 || y0 > y1) return;
    for (int cy = y0 >> 3; cy <= y1 >> 3; cy++)
        for (int cx = x0 >> 4; cx <= x1 >> 4; cx++) fx->mark[cy] |= 1u << cx;
}

static void cfx_run(cfx_t* fx) {
    const uint16_t* c = fx->cmd;
    int op = (c[0] >> 10) & 15, arg = c[0] & 0x3FF;
    switch (op) {
    case CFX_OP_BEGIN:
        fx->nspr = 0; fx->full = arg & 1; fx->err = 0;
        memset(fx->mark, 0, sizeof(fx->mark));
        break;
    case CFX_OP_SPRITE: {
        if (fx->nspr >= CFX_MAX_SPR) { fx->err = 1; break; }
        cfx_sprite_t* S = &fx->spr[fx->nspr];
        S->flags = (uint8_t)(arg & 7);
        S->x = (int16_t)s13(c[1]); S->y = (int16_t)s13(c[2]);
        S->w = c[3] & 0x1FF; S->h = c[4] & 0x1FF; S->stride = c[5] & 0x1FFF;
        S->src = addr24(c[6], c[7]); S->off = (uint8_t)c[8];
        if (S->w == 0 || S->h == 0) break;
        fx->nspr++;
        cfx_mark_sprite(fx, S);
        break;
    }
    case CFX_OP_LAYER: {
        cfx_layer_t* L = &fx->layer[arg & 3];
        L->flags = (uint16_t)arg;
        L->pix = addr24(c[1], c[2]);
        L->w = c[3] & 0x1FFF; L->h = c[4] & 0x1FFF;
        L->off = (uint8_t)c[5]; L->brk = (uint8_t)(c[5] >> 8 & 1);
        L->tile = c[6] & 63;
        L->map = (arg & CFX_L_MAPSRAM) ? addr18(c[7], c[8]) : addr24(c[7], c[8]);
        L->x = c[9] & 0x1FFF; L->y = c[10] & 0x1FFF;
        break;
    }
    case CFX_OP_SCROLL: { cfx_layer_t* L = &fx->layer[arg & 3]; L->x = c[1] & 0x1FFF; L->y = c[2] & 0x1FFF; break; }
    case CFX_OP_CELLS:
        if (arg < CFX_ROWS) fx->mark[arg] |= (uint32_t)(c[1] & 0x1FFF) | (uint32_t)(c[2] & 0xFF) << 13;
        break;
    case CFX_OP_POKE: fx->sram[addr18(c[1], c[2])] = (uint16_t)((c[3] & 255) << 8 | (c[4] & 255)); break;
    case CFX_OP_UPLOAD: {
        uint32_t src = addr24(c[1], c[2]) & ~1u, dst = addr18(c[3], c[4]);
        int n = c[5] & 0x1FFF;
        for (int i = 0; i < n; i++) fx->sram[(dst + (uint32_t)i) & 0x3FFFF] = ps_word(fx, src + 2u * (uint32_t)i);
        break;
    }
    case CFX_OP_LUT: {
        uint32_t src = addr24(c[1], c[2]);
        for (int i = 0; i < 4096; i++) fx->shade[i] = fx->psram(fx->ctx, (src + (uint32_t)i) & 0xFFFFFF);
        break;
    }
    case CFX_OP_SETUP: {
        int cols = c[1] & 0x1FFF, rows = c[2] & 0x1FFF;
        fx->cols = cols < 1 ? 1 : cols > CFX_COLS ? CFX_COLS : cols;
        fx->rows = rows < 1 ? 1 : rows > CFX_ROWS ? CFX_ROWS : rows;
        fx->blend = addr24(c[3], c[4]); fx->brk = addr18(c[5], c[6]);
        break;
    }
    case CFX_OP_SHOW: fx->shown = arg & 1; break;
    case CFX_OP_GO:   cfx_go(fx); break;
    case CFX_OP_OFF:  fx->on = 0; fx->key = 0; break;
    default:          fx->err = 1; break;
    }
}

static void cfx_word(cfx_t* fx, uint16_t w) {
    if (w & CFX_START) { fx->ncmd = 0; fx->need = cfx_words(w); }
    else if (fx->ncmd == 0) { fx->err = 1; return; }      /* an argument with no command: dropped */
    fx->cmd[fx->ncmd++] = (w & CFX_START) ? w : (uint16_t)(w & 0x1FFF);
    if (fx->ncmd < fx->need) return;
    cfx_run(fx);
    fx->ncmd = 0;
}

int cfx_read(cfx_t* fx, uint32_t addr, uint16_t* data) {
    addr &= 0x3FFFFE;
    if (addr >= CFX_CMD_BASE) {
        uint16_t w = (uint16_t)((addr - CFX_CMD_BASE) >> 1);
        if (!fx->on) {
            const uint16_t keys[3] = { CFX_KEY0, fx->key1, CFX_KEY2 };
            fx->key = w == keys[fx->key] ? fx->key + 1 : w == CFX_KEY0 ? 1 : 0;
            if (fx->key < 3) return 0;
            fx->on = 1; fx->key = 0; fx->ncmd = 0;
        } else if (w >= 0x2000) cfx_word(fx, w);
        if (w >= 1 && w < 16 && fx->on) {
            fx->cnt[8] = CFX_KEY2;
            *data = fx->cnt[w];
            return 1;
        }
        *data = (uint16_t)(fx->id << 8 | (fx->shown ? CFX_ST_SHOWN : 0) | (fx->err ? CFX_ST_ERR : 0));
        return 1;
    }
    if (fx->on && addr >= CFX_PIC_BASE && addr < CFX_CMD_BASE) {
        *data = fx->sram[((uint32_t)fx->shown * 0x8000u + ((addr - CFX_PIC_BASE) >> 1)) & 0x3FFFF];
        return 1;
    }
    return 0;
}
