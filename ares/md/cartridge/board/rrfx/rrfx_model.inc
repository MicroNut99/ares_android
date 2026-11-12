/* rrfx.c - the C model of Road Rash CD32X's renderer core (see rrfx.h).  The definition the RTL follows.

   The frame: the camera's 33 slices (road.c road_project), each slice's cross-section on the screen and the
   ceiling rule (scene.c scene_prepare), then far to near each strip's pieces as rows of one colour, the
   scenery standing on it and the things the game listed for it (scene.c scene_draw_part); then the picture as
   run-length words.  Integers only: 32-bit values that wrap, a x b >> 16 over the whole 64-bit product, a
   division that drops the fraction.  The stream decoder is OutRun CD32X's (fpga/orfx, V6). */
#include <string.h>
#include "rrfx.h"

#define W RRFX_W
#define H RRFX_H
#define MUL16(a, b) ((int32_t)(((int64_t)(a) * (b)) >> 16))
static int32_t sdiv(int32_t a, int32_t b) { return b ? a / b : 0; }

/* ---------------------------------------------------------------------------------------------- the cart */
#ifdef RRFX_MEM8        /* (an emulator: the cart's memory through its own reader) */
static uint32_t rd8(rrfx_t *f, uint32_t a) { f->n.mem_reads++; return RRFX_MEM8(a & 0xFFFFFF); }
#else
static uint32_t rd8(rrfx_t *f, uint32_t a) { f->n.mem_reads++; return f->mem[a & 0xFFFFFF]; }
#endif
static uint32_t rd16(rrfx_t *f, uint32_t a) { return rd8(f, a) << 8 | rd8(f, a + 1); }
static int32_t  rs16(rrfx_t *f, uint32_t a) { return (int16_t)rd16(f, a); }
static uint32_t rd32(rrfx_t *f, uint32_t a) { return rd16(f, a) << 16 | rd16(f, a + 2); }
static uint32_t reg32(const rrfx_t *f, int r) { return (uint32_t)f->reg[r] << 16 | f->reg[r + 1]; }

void rrfx_init(rrfx_t *f, const uint8_t *cart)
{
    memset(f, 0, sizeof(*f));
    f->mem = cart;
    f->sel_m = -1;
}

/* ---------------------------------------------------------------------------------------------- the stream */
void rrfx_sync(rrfx_t *f)
{
    f->data_left = 0;
    if (f->sel_m < -1) f->sel_m = -1;
    f->chk_pend = 0;
    f->sum = 0; f->cnt = 0;
    f->go_ok = 0; f->bad = 0; f->gap = 0; f->poison = 0;
    f->fresh = 1;
    f->n_pend = 0;
    f->q_tog = 0;
    f->q_key = 0;
}

static void fail(rrfx_t *f)
{
    if (!f->bad) f->n_bad++;
    f->bad = 1;
    f->go_ok = 0;
    f->n_pend = 0;
}

int rrfx_cmd(rrfx_t *f, int a17, uint16_t a)
{
    if (a17 == f->q_tog)
    {
        if (a == f->q_last) { f->n_dup++; return 0; }
        f->q_last = a; f->n_taken++;
        f->q_key = (uint16_t)(f->q_key + RRFX_KEY_STEP);
        f->fresh = 0;
        f->gap = 1; f->poison = 1;
        fail(f);
        return 2;
    }
    f->q_tog = a17; f->q_last = a; f->n_taken++;
    f->q_key = (uint16_t)(f->q_key + RRFX_KEY_STEP);
    rrfx_word(f, (uint16_t)(a ^ f->q_key));
    return 1;
}

static void mem_write(rrfx_t *f, int m, uint32_t a, uint16_t v)
{
    if (m == RRFX_M_REG) f->reg[a & (RRFX_R_COUNT - 1)] = v;
    else if (m == RRFX_M_THING) f->thing[a & (RRFX_THINGS * 8 - 1)] = v;
}

void rrfx_word(rrfx_t *f, uint16_t w)
{
    int fresh = f->fresh;
    f->fresh = 0;
    if (f->poison) return;
    if (f->chk_pend == 2) { f->chk_sok = w == f->sum; f->chk_pend = 1; return; }
    if (f->chk_pend == 1)
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
    if (cmdpos && (w & 0xf000) == RRFX_OP_CHECK) { f->chk_pend = 2; return; }
    if (!cmdpos || (w & 0xf000) == RRFX_OP_SELECT || (w & 0xf000) == RRFX_OP_DATA)
    {
        f->sum = (uint16_t)(f->sum + w); f->cnt++;
        f->go_ok = 0;
    }
    if (f->data_left)
    {
        if (f->n_pend == RRFX_PEND) { fail(f); f->poison = 1; return; }
        f->pend_m[f->n_pend] = (int8_t)f->sel_m; f->pend_a[f->n_pend] = (uint16_t)f->sel_a; f->pend_d[f->n_pend] = w;
        f->n_pend++;
        f->sel_a++;
        f->data_left--;
        return;
    }
    if (f->sel_m < -1) { f->sel_m = -2 - f->sel_m; f->sel_a = w; return; }
    switch (w & 0xf000)
    {
        case RRFX_OP_SELECT: f->sel_m = -2 - (w & 7); break;
        case RRFX_OP_DATA:   f->data_left = w & 0xfff; break;
        case RRFX_OP_GO:
            if (f->go_ok) rrfx_draw(f, w & 1);
            else fail(f);
            f->go_ok = 0;
            break;
        case RRFX_OP_SHOW: if (fresh) f->shown = w & 1; break;
        default: break;
    }
}

/* ---------------------------------------------------------------------------------------------- the frame */
typedef struct { int32_t scale, wx, wy, y, xc; } Slice;
typedef struct { int32_t x[2][4], y[2][4], ex[2], ox[2], ry, bottom; } Cross;
typedef struct { int32_t xa, xb, da, db; } Job;

static int clampx(int v) { return v > 30000 ? 30000 : v < -30000 ? -30000 : v; }

static void rows(rrfx_t *f, int y, int count, Job *j, uint32_t c)       /* fill.s: count rows from row y */
{
    f->n.runs++;
    for (uint8_t *row = f->raw + y * W; count > 0; count--, row += W, j->xa += j->da, j->xb += j->db)
    {
        int l = j->xa >> 16, r = j->xb >> 16;
        if (l > r) { int k = l; l = r; r = k; }
        if (l < 0) l = 0;
        if (r > W) r = W;
        if (l >= r) continue;
        f->n.fill_words += (uint32_t)(r - l + 1) / 2;
        memset(row + l, (int)c, (size_t)(r - l));
    }
}

static void quad1(rrfx_t *f, int bottom, const int *px, const int *py, uint32_t c)     /* scene.c quad */
{
    int t = 0;
    if (py[1] < py[t]) t = 1;
    if (py[2] < py[t]) t = 2;
    if (py[3] < py[t]) t = 3;
    int ymin = py[t], ymax = py[0];
    if (py[1] > ymax) ymax = py[1];
    if (py[2] > ymax) ymax = py[2];
    if (py[3] > ymax) ymax = py[3];
    if (ymax > bottom) ymax = bottom;
    if (ymax <= 0 || ymin >= ymax) return;
    int xmin = px[0], xmax = px[0];
    for (int k = 1; k < 4; k++) { if (px[k] < xmin) xmin = px[k]; if (px[k] > xmax) xmax = px[k]; }
    if (xmax <= 0 || xmin >= W) return;
    f->n.pieces++;
    Job j;
    j.xa = j.xb = px[t] << 16; j.da = j.db = 0;
    int ia = t, ib = t, ya = ymin, yb = ymin, left = 4, y = ymin;
    while (y < ymax)
    {
        while (ya <= y && left > 0)
        {
            int n = (ia + 1) & 3; left--;
            int h = py[n] - py[ia];
            j.xa = px[ia] << 16;
            if (h > 0) { j.da = sdiv((px[n] - px[ia]) << 16, h); j.xa += j.da * (y - py[ia]); }
            ya = py[n]; ia = n;
        }
        while (yb <= y && left > 0)
        {
            int n = (ib + 3) & 3; left--;
            int h = py[n] - py[ib];
            j.xb = px[ib] << 16;
            if (h > 0) { j.db = sdiv((px[n] - px[ib]) << 16, h); j.xb += j.db * (y - py[ib]); }
            yb = py[n]; ib = n;
        }
        int end = ya < yb ? ya : yb;
        if (end > ymax || (ya <= y && yb <= y)) end = ymax;
        if (end <= y) end = y + 1;
        if (end <= 0) { j.xa += j.da * (end - y); j.xb += j.db * (end - y); y = end; continue; }
        if (y < 0) { j.xa -= j.da * y; j.xb -= j.db * y; y = 0; }
        rows(f, y, end - y, &j, c);
        y = end;
    }
}

/* a piece whose far and near edges cross (two cross-sections that cross on the screen) is a bow tie, and the
   two chains would fill one loop of it only: it is drawn as two triangles */
static void quad(rrfx_t *f, int bottom, const int *px, const int *py, uint32_t c)
{
    int d0 = py[3] - py[0], d1 = py[2] - py[1];
    if ((d0 < 0 && d1 > 0) || (d0 > 0 && d1 < 0))
    {
        int tx[4], ty[4];
        tx[0] = px[0]; ty[0] = py[0]; tx[1] = px[1]; ty[1] = py[1]; tx[2] = tx[3] = px[2]; ty[2] = ty[3] = py[2];
        quad1(f, bottom, tx, ty, c);
        tx[1] = px[2]; ty[1] = py[2]; tx[2] = tx[3] = px[3]; ty[2] = ty[3] = py[3];
        quad1(f, bottom, tx, ty, c);
    }
    else quad1(f, bottom, px, py, c);
}

/* a picture's w x h bytes at src, its top left at (x0, y0), drawn dw x dh; 0 is see-through; remap: a rider's
   colour table (scene.c picture, sprite.c bob_draw) */
static void picture(rrfx_t *f, int bottom, uint32_t src, int w, int h, int x0, int y0, int dw, int dh, int mirror, uint32_t remap, int remapped)
{
    if (dw < 1 || dh < 1 || dw > 1200 || dh > 1200) return;
    if (x0 >= W || x0 + dw <= 0 || y0 >= bottom || y0 + dh <= 0) return;
    int32_t du = sdiv(w << 16, dw), dv = sdiv(h << 16, dh);
    int32_t u0 = 0, v = 0;
    int xs = x0, xe = x0 + dw, ys = y0, ye = y0 + dh;
    if (xs < 0) { u0 = -xs * du; xs = 0; }
    if (xe > W) xe = W;
    if (ys < 0) { v = -ys * dv; ys = 0; }
    if (ye > bottom) ye = bottom;
    if (mirror) { u0 = ((w << 16) - 1) - u0; du = -du; }
    f->n.pictures++;
    for (int y = ys; y < ye; y++, v += dv)
    {
        uint32_t s = src + (uint32_t)((v >> 16) * w);
        int32_t u = u0;
        for (int x = xs; x < xe; x++, u += du)
        {
            uint32_t c = rd8(f, s + (uint32_t)(u >> 16));
            f->n.picture_pixels++;
            if (c && remapped) c = rd8(f, remap + c);
            if (c) f->raw[y * W + x] = (uint8_t)c;
        }
    }
}

void rrfx_draw(rrfx_t *f, int buf)
{
    static Slice s[RRFX_SLICES];
    static Cross cross[RRFX_SLICES];
    const uint32_t position = reg32(f, RRFX_R_POS_H), samples = f->reg[RRFX_R_SAMPLES];
    const int32_t cam_x = (int16_t)f->reg[RRFX_R_CAM_X], cam_h = (int16_t)f->reg[RRFX_R_CAM_H];
    const uint32_t route = reg32(f, RRFX_R_ROUTE_H), scene = reg32(f, RRFX_R_SCENE_H), obj = reg32(f, RRFX_R_OBJ_H);
    const uint32_t img = reg32(f, RRFX_R_IMG_H), hz = reg32(f, RRFX_R_HORIZON_H), depths = reg32(f, RRFX_R_DEPTH_H);
    const uint32_t remap = reg32(f, RRFX_R_REMAP_H);
    const uint32_t c_road1 = f->reg[RRFX_R_COL_ROAD] >> 8, c_road0 = f->reg[RRFX_R_COL_ROAD] & 255;
    const uint32_t c_line = f->reg[RRFX_R_COL_LINE] >> 8, c_yellow = f->reg[RRFX_R_COL_LINE] & 255;
    const uint32_t c_grass = f->reg[RRFX_R_COL_GRASS] >> 8, c_far = f->reg[RRFX_R_COL_GRASS] & 255;
    const uint32_t c_side = f->reg[RRFX_R_COL_SIDE] >> 8, c_rail = f->reg[RRFX_R_COL_SIDE] & 255;
    const uint32_t first = position >> 8;
    const int32_t frac = (int32_t)(position & 0xFF);
    memset(&f->n, 0, sizeof(f->n));
    buf &= 1;

    /* the slices (road.c road_project) */
    int n = 0;
    if (first < samples)
    {
        uint32_t p = route + first * 8;
        int32_t rise = (int8_t)rd8(f, p + 1);
        int32_t y = -((frac * rise) >> 8);
        int32_t dx = ((int8_t)rd8(f, p) * (256 - frac)) / 256;
        int32_t x = 0, depth = 256 - frac;
        for (n = 0; n < RRFX_SLICES && first + (uint32_t)n < samples; n++, p += 8)
        {
            if (n) { dx += (int8_t)rd8(f, p); rise = (int8_t)rd8(f, p + 1); }
            int32_t sc = (int32_t)rd32(f, depths + (uint32_t)((depth & ~1) >> 1) * 4);
            int32_t cx = x - cam_x;
            s[n].scale = sc; s[n].wx = cx; s[n].wy = y;
            s[n].y = (int32_t)((uint32_t)(cam_h - y) * (uint32_t)sc + ((uint32_t)RRFX_HORIZON << 16));
            s[n].xc = (int32_t)((uint32_t)cx * (uint32_t)sc + (160u << 16));
            x += dx; y += rise; depth += 256;
        }
    }

    /* each slice's cross-section on the screen, and the ceiling (scene.c scene_prepare) */
    int ceiling = H, far_low = 0;
    for (int i = 0; i < n; i++)
    {
        uint32_t at = first + (uint32_t)i; if (at >= samples) at = samples - 1;
        uint32_t sm = scene + at * 52, rs = route + at * 8;
        int32_t sc = s[i].scale;
        Cross *c = &cross[i];
        int low = -32768;
        for (int side = 0; side < 2; side++)
        {
            uint32_t sd = sm + 4 + (uint32_t)side * 20;
            for (int k = 0; k < 4; k++)
            {
                int32_t wx = s[i].wx + rs16(f, sd + (uint32_t)k * 2);
                if (wx > 30000) wx = 30000;
                if (wx < -30000) wx = -30000;
                c->x[side][k] = clampx(MUL16(wx, sc) + 160);
                int y = clampx(MUL16(cam_h - s[i].wy - rs16(f, sd + 8 + (uint32_t)k * 2), sc) + RRFX_HORIZON);
                c->y[side][k] = y;
                if (y > low) low = y;
            }
            int32_t ex = s[i].wx + (side ? (int32_t)rd8(f, rs + 3) * 4 : -((int32_t)rd8(f, rs + 2) * 4));
            c->ex[side] = clampx(MUL16(ex, sc) + 160);
            int32_t wo = s[i].wx + rs16(f, sd + 6) + (side ? 6000 : -6000);
            c->ox[side] = clampx(MUL16(wo, sc) + 160);
        }
        int ry = clampx(s[i].y >> 16);
        c->ry = ry;
        if (ry > low) low = ry;
        uint32_t kind = rd8(f, sm);
        if (i > 0 && low < ceiling && (kind == 4 || kind == 1)) ceiling = low < 0 ? 0 : low;   /* (hills and mountains reach the screen's sides) */
        c->bottom = ceiling;
        if ((i == n - 1 || !(kind == 4 || kind == 1)) && low > far_low) far_low = low;   /* where no ground reaches the sides */
    }
    int sky = ceiling;
    if (far_low + 1 > sky) sky = far_low + 1 > H ? H : far_low + 1;      /* beyond the farthest slice nothing is drawn: the far ground */

    /* the horizon picture, the far ground under it down to the ceiling */
    {
        int top = RRFX_HZ_ROWS;                 /* all of it: buildings leave gaps the ceiling rule does not know */
        uint32_t sx = f->reg[RRFX_R_SCROLL] % RRFX_HZ_W;
        for (int y = 0; y < top; y++)
            for (int x = 0; x < W; x++)
                f->raw[y * W + x] = (uint8_t)rd8(f, hz + (uint32_t)((RRFX_HZ_ROW0 + y) * RRFX_HZ_W) + (sx + (uint32_t)x) % RRFX_HZ_W);
        for (int y = RRFX_HZ_ROWS; y < sky; y++) memset(f->raw + y * W, (int)c_far, W);
    }

    /* far to near (scene.c scene_draw_part) */
    int thing = 0;
    const int things = f->reg[RRFX_R_THINGS] > RRFX_THINGS ? RRFX_THINGS : f->reg[RRFX_R_THINGS];
    for (int i = n - 2; i >= 0; i--)
    {
        int step = 1;
        if (i >= RRFX_FAR) { if (i & 1) step = 0; else if (i + 2 < n) step = 2; }
        uint32_t at = first + (uint32_t)i; if (at >= samples) at = samples - 1;
        uint32_t sm = scene + at * 52;
        const Cross *a = &cross[i + (step ? step : 1)], *b = &cross[i];
        const Slice *fs = &s[i + (step ? step : 1)], *e = &s[i];
        int fy = a->ry, ny = b->ry;
        int bottom = step ? b->bottom : 0;
        uint32_t band = ((first + (uint32_t)i) >> 1) & 1;
        uint32_t kind = rd8(f, sm);
        int px[4], py[4];

        if (bottom > 0)
            for (int side = 0; side < 2; side++)
            {
                uint32_t col = sm + 4 + (uint32_t)side * 20 + 16;
                if (kind == 4 || kind == 1)
                {
                    uint32_t c = rd8(f, col + 3);
                    px[0] = a->x[side][3]; py[0] = a->y[side][3]; px[1] = a->ox[side]; py[1] = a->y[side][3];
                    px[2] = b->ox[side]; py[2] = b->y[side][3]; px[3] = b->x[side][3]; py[3] = b->y[side][3];
                    quad(f, bottom, px, py, c ? c : c_grass);
                }
                for (int k = 3; k >= 1; k--)
                {
                    uint32_t c = rd8(f, col + (uint32_t)k);
                    if (!c) { if (kind != 4 && kind != 1) continue; c = c_grass; }   /* a hill band with no material: ground */
                    px[0] = a->x[side][k - 1]; py[0] = a->y[side][k - 1]; px[1] = a->x[side][k]; py[1] = a->y[side][k];
                    px[2] = b->x[side][k]; py[2] = b->y[side][k]; px[3] = b->x[side][k - 1]; py[3] = b->y[side][k - 1];
                    quad(f, bottom, px, py, c);
                }
                if (ny > fy)
                {
                    uint32_t c = rd8(f, col);
                    px[0] = a->ex[side]; py[0] = fy; px[1] = a->x[side][0]; py[1] = a->y[side][0];
                    px[2] = b->x[side][0]; py[2] = b->y[side][0]; px[3] = b->ex[side]; py[3] = ny;
                    quad(f, bottom, px, py, c ? c : c_side);
                }
            }

        if (ny > fy && fy < bottom && ny > 0)
        {
            int lines = i < RRFX_FAR + 6;
            px[0] = a->ex[0]; px[1] = a->ex[1]; px[2] = b->ex[1]; px[3] = b->ex[0];
            py[0] = py[1] = fy; py[2] = py[3] = ny;
            quad(f, bottom, px, py, band ? c_road1 : c_road0);
            int ft = MUL16(6, fs->scale), nt = MUL16(6, e->scale);
            if (ft < 1) ft = 1;
            if (nt < 1) nt = 1;
            int fxc = fs->xc >> 16, nxc = e->xc >> 16;
            int ll = (int)((rd8(f, sm + 2) + 8u) >> 4), lr = (int)((rd8(f, sm + 3) + 8u) >> 4);
#define LINE(fx, nx, c) do { px[0] = (fx); px[1] = (fx) + ft; px[2] = (nx) + nt; px[3] = (nx); quad(f, bottom, px, py, (c)); } while (0)
            if (lines) { LINE(a->ex[0] + ft, b->ex[0] + nt, c_line); LINE(a->ex[1] - 2 * ft, b->ex[1] - 2 * nt, c_line); }
            if (ll > 0 && lr > 0)
            {
                int fo = MUL16(10, fs->scale), no = MUL16(10, e->scale);
                LINE(fxc - fo - ft, nxc - no - nt, c_yellow);
                if (no > nt) LINE(fxc + fo, nxc + no, c_yellow);
            }
            if (lines && ((first + (uint32_t)i) & 3) < 2)
            {
                int fl = MUL16(250, fs->scale), nl = MUL16(250, e->scale);
                for (int k = 1; k < lr; k++) LINE(fxc + fl * k, nxc + nl * k, c_line);
                for (int k = 1; k < ll; k++) LINE(fxc - fl * k, nxc - nl * k, c_line);
            }
#undef LINE
        }

        e = &s[i]; fs = &s[i + 1]; a = &cross[i + 1];
        uint32_t rail = rd8(f, sm + 47);
        if (rail && cross[i].bottom > 0)
            for (int side = 0; side < 2; side++)
            {
                if (!(rail & (1u << side))) continue;
                int off = (int8_t)rd8(f, sm + 48); if (!side) off = -off;
                int rh = (int)rd8(f, sm + 49);
                int fx = a->x[side][0] + MUL16(off, fs->scale), nx = b->x[side][0] + MUL16(off, e->scale);
                int fh = MUL16(rh, fs->scale), nh = MUL16(rh, e->scale);
                px[0] = fx; py[0] = a->y[side][0] - fh; px[1] = fx; py[1] = a->y[side][0] - fh / 2;
                px[2] = nx; py[2] = b->y[side][0] - nh / 2; px[3] = nx; py[3] = b->y[side][0] - nh;
                quad(f, cross[i].bottom, px, py, c_rail);
            }
        uint32_t count = rd8(f, sm + 46);
        if (count)                                              /* the scenery on this sample (scene.c draw_objects) */
        {
            int32_t d = 256 - frac + i * 256;
            if (d > 146)
            {
                uint32_t o = obj + (rd16(f, sm + 50) << 16 | rd16(f, sm + 44)) * 8;
                int32_t sc = (int32_t)rd32(f, depths + (uint32_t)(((d + 128) & ~1) >> 1) * 4);
                int32_t wx = (e->wx + fs->wx) / 2, wy = (e->wy + fs->wy) / 2;
                for (uint32_t k = 0; k < count; k++, o += 8)
                {
                    uint32_t en = img + 8 + rd16(f, o + 4) * 16;
                    int w = (int)rd16(f, en + 4), h = (int)rd16(f, en + 6), half = (int)rd8(f, en + 14);
                    int hx = rs16(f, en + 8), hy = rs16(f, en + 10);
                    int32_t scale = (int32_t)((uint32_t)((int32_t)rd8(f, o + 6) * sc) >> 2);
                    int mirror = (int)(rd8(f, o + 7) & 1);
                    int ax = MUL16(wx + rs16(f, o), sc) + 160;
                    int x = ax + MUL16(mirror ? hx : -hx, scale);
                    int y = MUL16(cam_h - wy - rs16(f, o + 2), sc) - MUL16(hy, scale) + RRFX_HORIZON;
                    int dw = MUL16(w << half, scale), dh = MUL16((h << half) + 1, scale);
                    if (mirror) x -= dw;
                    picture(f, H, img + rd32(f, en), w, h, x, y, dw, dh, mirror, 0, 0);
                }
            }
        }
        while (thing < things && (f->thing[thing * 8] >> 8) > (uint32_t)i) thing++;        /* (a list out of order: skipped) */
        for (; thing < things && (f->thing[thing * 8] >> 8) == (uint32_t)i; thing++)       /* what the game put on this strip */
        {
            const uint16_t *t = &f->thing[thing * 8];
            picture(f, H, (uint32_t)t[6] << 16 | t[7], t[5] >> 8, t[5] & 255, (int16_t)t[1], (int16_t)t[2], (int16_t)t[3], (int16_t)t[4],
                    t[0] & 1, remap + ((uint32_t)(t[0] >> 4) & 15) * 256, (t[0] >> 1) & 1);
        }
    }

    /* the picture as run-length words: (pixels - 1) << 8 | colour; a run ends where the colour changes, at 256
       pixels, at the line's end */
    memset(f->lt[buf], 0, sizeof(f->lt[buf]));
    uint32_t words = 0; int ovf = 0;
    for (int y = 0; y < H; y++)
    {
        const uint8_t *l = f->raw + y * W;
        f->lt[buf][y] = (uint16_t)words;
        for (int x = 0; x < W; )
        {
            int e = x + 1;
            while (e < W && l[e] == l[x] && e - x < 256) e++;
            if (words < RRFX_RLE_MAX) { f->pic[buf][2 * words] = (uint8_t)(e - x - 1); f->pic[buf][2 * words + 1] = l[x]; words++; }
            else ovf = 1;
            x = e;
        }
    }
    f->lt[buf][224] = (uint16_t)words;
    f->lt[buf][225] = (uint16_t)(1 | ovf << 1);
    f->n.rle_words = words;
    f->drawn = buf;
    f->frames++;
}
