/* main.c - per-frame logic for the RMD64 practice patch. Runs from hud_shim at the end of frame-flush function, just before it kicks the RSP. Everything emits lands on top of the finished frame. */
#include "rmd64.h"
#include "font_rects.h"

/* GBI */

/* HUD has cursor live in register. */
static inline u32 *gfx(u32 *p, u32 w0, u32 w1)
{
    p[0] = w0;
    p[1] = w1;
    return p + 2;
}

#define XY(x, y) ((((u32)((x) << 2) & 0xFFF) << 12) | ((u32)((y) << 2) & 0xFFF))

static inline u32 *pipesync(u32 *p)       { return gfx(p, 0xE7000000, 0); }
static inline u32 *set_fill_cycle(u32 *p) { return gfx(p, 0xE3000A01, 0x00300000); }
static inline u32 *set_1cycle(u32 *p)     { return gfx(p, 0xE3000A01, 0x00000000); }
static inline u32 *set_fill_color(u32 *p, u16 c)
                                  { return gfx(p, 0xF7000000, ((u32)c << 16) | c); }
static inline u32 *scissor_full(u32 *p)
                                  { return gfx(p, 0xED000000 | XY(0, 0), XY(320, 240)); }

/* Render mode from header list at 0x8022D040. */
static inline u32 *set_rendermode(u32 *p) { return gfx(p, 0xE200001C, 0x0F0A4000); }

/* G_SETCIMG, RGBA 16-bit, physical address. */
static inline u32 *set_cimg(u32 *p, u32 phys, u32 width)
{
    return gfx(p, 0xFF100000 | ((width - 1) & 0xFFF), phys);
}

/* x2,y2 exclusive */
static inline u32 *rect(u32 *p, int x1, int y1, int x2, int y2)
{
    return gfx(p, 0xF6000000 | XY(x2 - 1, y2 - 1), XY(x1, y1));
}

/* text */

static u32 *glyph_rects(u32 *p, int gx, int gy, int sc,
                        const u8 (*tab)[4], int from, int to)
{
    int i;
    for (i = from; i < to; i++) {
        const u8 *r = tab[i];
        p = rect(p, gx + r[0] * sc,
                    gy + r[1] * sc,
                    gx + (r[0] + r[2]) * sc,
                    gy + (r[1] + r[3]) * sc);
    }
    return p;
}

#define GLYPH_ADV (4 * HUD_SCALE)
#define ROW_H     (5 * HUD_SCALE)

/* Draws n glyphs left to right from x, at scale sc. */
static u32 *text_sc(u32 *p, int x, int y, const u8 *g, int n, int sc)
{
    int i;
    for (i = 0; i < n; i++) {
        p = glyph_rects(p, x, y, sc, kGlyphRect,
                        kGlyphOfs[g[i]], kGlyphOfs[g[i] + 1]);
        x += 4 * sc;
    }
    return p;
}

/* The background-coloured half of the same string. */
static u32 *holes_sc(u32 *p, int x, int y, const u8 *g, int n, int sc)
{
    int i;
    for (i = 0; i < n; i++) {
        p = glyph_rects(p, x, y, sc, kHoleRect,
                        kHoleOfs[g[i]], kHoleOfs[g[i] + 1]);
        x += 4 * sc;
    }
    return p;
}

#define text(p, x, y, g, n)  text_sc((p), (x), (y), (g), (n), HUD_SCALE)
#define holes(p, x, y, g, n) holes_sc((p), (x), (y), (g), (n), HUD_SCALE)

#define TEXT_W(n) ((n) * GLYPH_ADV - HUD_SCALE)

/* HUD */

#define COL_WHITE   0xFFFF
#define COL_BACK    0x0001                  /* opaque black */
#define COL_GREEN   0x07C1
#define COL_AMBER   0xFD01
#define COL_YELLOW  0xFFC1
#define COL_RED     0xF801
#define COL_BLUE    0x443F
#define COL_PINK    0xFAB5                  /* room snapshots off */

#define N_CLOCK  11                         /* HH:MM:SS.ff */
#define N_COORD  7                          /* axis, sign, 5 digits */

#define CLOCK_Y  6
#define COORD_Y  20                         /* three rows, ROW_H + 2 apart */
#define COORD_DY (ROW_H + 2)
#define PAD_Y    (COORD_Y + 3 * COORD_DY + 4)

#define CLOCK_X  (HUD_RIGHT - TEXT_W(N_CLOCK))

/* The light, and the outline drawn round it while room snapshots are off. */
#define LIGHT_X1 (CLOCK_X - 12)
#define LIGHT_Y1 (CLOCK_Y + 2)
#define LIGHT_X2 (CLOCK_X - 7)
#define LIGHT_Y2 (CLOCK_Y + ROW_H - 2)

/* "PRAC PATCH" watermark location shown on title and result sreen */
#define N_MARK   10
#define MARK_X   (HUD_RIGHT - (N_MARK * 4 - 1))
#define MARK_Y   222
static const u8 kMark[N_MARK] = {
    GL_P, GL_R, GL_A, GL_C, GL_SPACE, GL_P, GL_A, GL_T, GL_C, GL_H
};
#define COORD_X  (HUD_RIGHT - TEXT_W(N_COORD))

/* Decides which framebuffer this frame will be rendered into. Returns 0 if neither and the caller then draws into both */
static u32 render_target(void)
{
    u32 fb = MMIO32(G_FB_PTR) & 0x1FFFFFFF;   /* virtual to physical RDRAM */

    if (fb == FB_PHYS_LO || fb == FB_PHYS_LO + FB_STRIDE)
        return fb;
    return 0;
}

/* Shows the "PRAC ROM" mark. Shown on title and result screen */
static int on_mark_screen(void)
{
    if (MMIO32(G_TOPLOOP) == TOPLOOP_TITLE) {
        s8 t = (s8)MMIO8(G_TITLE_STATE);
        return t == TITLE_PRESS || t == TITLE_MENU;
    }
    return MMIO8(G_EVT_RUN) != 0 &&
           MMIO8(G_EVT_ID) == EVT_ENDING &&
           MMIO8(G_EVT_SHOT) == SHOT_RESULTS &&
           MMIO8(G_EVT_SUB) >= SUB_TIME_SHOWN;
}

struct hud {
    u8  clock[N_CLOCK];
    int light;      /* Status cube: 0 none, 1 armed, 2 taken, 3 gave up, 4 working */
    int qfail;            /* what wasn't quiet if it failed */
    int autooff;          /* room-entry snapshots disabled: outline the light */
    int mark;             /* draw "PRAC ROM" */
};

/* Everything the HUD draws, into one framebuffer */
static u32 *hud_pass(u32 *p, const struct hud *h, u32 cimg, u32 fbw)
{
    p = pipesync(p);
    p = set_cimg(p, cimg, fbw);

    p = set_fill_color(p, COL_BACK);
    p = rect(p, CLOCK_X - 1, CLOCK_Y - 1, HUD_RIGHT + 1, CLOCK_Y + ROW_H + 1);
#if !HUD_MINIMAL
    if (h->coords)
        p = rect(p, COORD_X - 1, COORD_Y - 1, HUD_RIGHT + 1,
                 COORD_Y + 3 * COORD_DY - 2 + 1);
    p = rect(p, PAD_X - 1, PAD_Y - 1, HUD_RIGHT + 1, PAD_Y + PAD_H + 1);
#endif

    if (h->mark)
        p = rect(p, MARK_X - 2, MARK_Y - 2, HUD_RIGHT + 2, MARK_Y + 5 + 2);

    /* room entry savestates off: a pink box one pixel bigger than the status box*/
    if (h->autooff) {
        p = pipesync(p);
        p = set_fill_color(p, COL_PINK);
        p = rect(p, LIGHT_X1 - 1, LIGHT_Y1 - 1, LIGHT_X2 + 1, LIGHT_Y2 + 1);
        if (!h->light) {
            p = pipesync(p);
            p = set_fill_color(p, COL_BACK);
            p = rect(p, LIGHT_X1, LIGHT_Y1, LIGHT_X2, LIGHT_Y2);
        }
    }

    if (h->light) {
        p = pipesync(p);
        p = set_fill_color(p, h->light == 4 ? COL_BLUE :
                              h->light == 3 ? COL_RED   :
                              h->light == 2 ? COL_AMBER : COL_GREEN);
        p = rect(p, LIGHT_X1, LIGHT_Y1, LIGHT_X2, LIGHT_Y2);

        /* debug for red box:1 graphics task, 2 audio task queued, 3 audio task running, 4 audio thread parked, 5 PI manager, 6 PI busy */
        if (h->light == 3) {
            int i;
            for (i = 0; i < h->qfail && i < 6; i++)
                p = rect(p, CLOCK_X - 17 - i * 4, CLOCK_Y + 3,
                         CLOCK_X - 15 - i * 4, CLOCK_Y + 6);
        }
    }

    p = pipesync(p);
    p = set_fill_color(p, COL_WHITE);
    p = text(p, CLOCK_X, CLOCK_Y, h->clock, N_CLOCK);
    if (h->mark)
        p = text_sc(p, MARK_X, MARK_Y, kMark, N_MARK, 1);

    /* Cut-outs for every box-mode glyph drawn above, in one pass */
    p = pipesync(p);
    p = set_fill_color(p, COL_BACK);
    p = holes(p, CLOCK_X, CLOCK_Y, h->clock, N_CLOCK);
    if (h->mark)
        p = holes_sc(p, MARK_X, MARK_Y, kMark, N_MARK, 1);
    return p;
}

static void draw_hud(void)
{
    struct hud h;
    u32 fbw = MMIO16(G_FB_WIDTH);

    if (fbw == 0 || fbw > 640)
        fbw = 320;

    {
        u32 ticks = MMIO32(G_PLAYTIME);
        if (ticks) ticks--;        /* the game's own clocks format the count as it was before this frame's increment, match them so the time doesn't start 1 frame ahead */
        u32 secs  = ticks / 30;
        u32 ff = ticks % 30, ss = secs % 60;
        u32 mm = (secs / 60) % 60, hh = secs / 3600;

        if (hh > 99) hh = 99;
        h.clock[0] = (u8)(hh / 10);   h.clock[1]  = (u8)(hh % 10);
        h.clock[2] = GL_COLON;
        h.clock[3] = (u8)(mm / 10);   h.clock[4]  = (u8)(mm % 10);
        h.clock[5] = GL_COLON;
        h.clock[6] = (u8)(ss / 10);   h.clock[7]  = (u8)(ss % 10);
        h.clock[8] = GL_DOT;
        h.clock[9] = (u8)(ff / 10);   h.clock[10] = (u8)(ff % 10);
    }

    h.qfail = (int)VAR(VAR_QFAIL);
    h.autooff = (int)VAR(VAR_AUTOOFF);
    h.mark = on_mark_screen();
    /* Blue goes over all other status to show that your combo was recognized, stays after release if action is waiting for quiet */
    h.light = (VAR(VAR_BLUE) || VAR(VAR_PENDACT)) ? 4
            : VAR(VAR_REDFLASH) ? 3
            : VAR(VAR_FLASH)    ? 2
            : VAR(VAR_VALID)    ? 1 : 0;

    {
        u32 target = render_target();
        if (target && MMIO8(G_GAME_MODE) != 0) {
            u32 *p = (u32 *)MMIO32(G_DL_CURSOR);

            p = pipesync(p);
            p = set_rendermode(p);
            p = set_fill_cycle(p);
            p = scissor_full(p);

            p = hud_pass(p, &h, target, fbw);

            p = pipesync(p);
            p = set_1cycle(p);

            MMIO32(G_DL_CURSOR) = (u32)p;
        }
    }

    if (VAR(VAR_FLASH))
        VAR(VAR_FLASH) = VAR(VAR_FLASH) - 1;
    if (VAR(VAR_REDFLASH))
        VAR(VAR_REDFLASH) = VAR(VAR_REDFLASH) - 1;
}

/* input */

/* L wherever Z is expected. forgot about our d-pad OEM gamers that definitely still exist */
static u16 pad_combo(void)
{
    u16 raw = MMIO16(G_PAD_HELD);

    if (raw & BTN_L)
        raw |= BTN_Z;
    return raw;
}

/* Z/L + R + A                -> restore
 * Z/L + R + C-down + A       -> set point
 * Z/L + R + A + B            -> room-entry snapshots on/off
 *
 * Restore is a subset of the other two, so you hold it for COMBO_SETTLE before it fires. VAR_FIRED then blocks a second combo until the first combo is fully released */
static void poll_action(void)
{
    u16 c    = pad_combo() & COMBO_MASK;
    u16 prev = (u16)VAR(VAR_PREVCOMBO);
    u32 hold = (c == prev) ? VAR(VAR_HOLD) + 1 : 0;
    int act  = ACT_NONE;

    VAR(VAR_PREVCOMBO) = c;
    VAR(VAR_HOLD) = hold;

    /* Light up as soon as the combo is recognised, to prevent wondering if the game got it while it waits for quiet */
    VAR(VAR_BLUE) = (!VAR(VAR_FIRED) &&
                     (c == COMBO_RESTORE || c == COMBO_CAPTURE)) ? 1 : 0;

    if (c == 0) {
        VAR(VAR_FIRED) = 0;
        return;
    }
    if (VAR(VAR_FIRED))
        return;

    /* The combo toggle is a superset of the restore too, so like the capture it acts the instant it is complete, and needs no quiet frame. */
    if (c == COMBO_TOGGLE) {
        VAR(VAR_FIRED) = 1;
        VAR(VAR_AUTOOFF) = !VAR(VAR_AUTOOFF);
        return;
    }

    if (c == COMBO_CAPTURE)
        act = ACT_CAPTURE;
    else if (c == COMBO_RESTORE && hold >= COMBO_SETTLE)
        act = ACT_RESTORE;

    if (act != ACT_NONE) {
        VAR(VAR_FIRED) = 1;
        VAR(VAR_PENDACT) = (u32)act;
        VAR(VAR_AGE) = 0;
    }
}

/*  per frame -- */

int hud_frame(void)
{
    int act;

    if (VAR(VAR_MAGIC) != MAGIC_VALUE)
        return ACT_NONE;

    /* Easy and Hard unlocked, permanently. Held every frame so it sticks */
    if (MMIO16(G_DIFF_EASY) != 1) MMIO16(G_DIFF_EASY) = 1;
    if (MMIO16(G_DIFF_HARD) != 1) MMIO16(G_DIFF_HARD) = 1;

    draw_hud();

    poll_action();
    
    /* a room finished loading and queues a savestate, wins over a pending restore */
    if (VAR(VAR_PENDING)) {
        u32 area = (u32)MMIO16(G_AREA_LOADED) + 1;   /* +1: 0 means "none yet" */
        int subroom = (VAR(VAR_PENDMODE) == 3 && area == VAR(VAR_LASTAREA));

        VAR(VAR_PENDING) = 0;
        VAR(VAR_LASTAREA) = area;

        if (!subroom && !VAR(VAR_AUTOOFF)) {
            VAR(VAR_PENDACT) = ACT_CAPTURE;
            VAR(VAR_AGE) = 0;
        }

    }

    /* waits for a quiet frame, if it doesn't come in time it shows a red status box */
    act = (int)VAR(VAR_PENDACT);
    if (act != ACT_NONE) {
        if (act == ACT_RESTORE && !VAR(VAR_VALID)) {
            VAR(VAR_PENDACT) = 0;
            act = ACT_NONE;
        } else if (VAR(VAR_AGE) >= PEND_TIMEOUT) {
            VAR(VAR_PENDACT) = 0;
            VAR(VAR_REDFLASH) = 30;
            act = ACT_NONE;
        } else {
            VAR(VAR_AGE) = VAR(VAR_AGE) + 1;
        }
    }

    /* lets the shim know if we need a full register file or not */
    VAR(VAR_ARMED) = (VAR(VAR_PENDACT) || VAR(VAR_PENDING) ||
                      (pad_combo() & COMBO_RESTORE) == COMBO_RESTORE)
                     ? 1 : 0;

    return act;
}
