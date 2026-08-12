/*
 * One Foe - an original single-boss bullet-hell for the Classic Macintosh
 * -----------------------------------------------------------------------
 * A tiny original arcade duel. You pilot a ship at the bottom of a 128x128
 * playfield (drawn at integer 2x = 256x256) and wear down one boss through
 * four attack phases. Everything is my own design.
 *
 * Engineering notes (see README / DEVLOG):
 *   - Fixed-timestep logic (LOGIC_HZ) decoupled from drawing, so the game
 *     runs at a constant speed no matter the frame rate.
 *   - Integer sine table built with Bhaskara's approximation (no FPU, no libm).
 *   - Bullets move in 1/16-pixel fixed point for smooth angles.
 *   - Everything composites to an offscreen bitmap, one CopyBits per frame.
 *   - Projectiles are told apart by SHAPE (filled circle / diamond / hollow
 *     square), not shade, so they stay readable in pure black & white.
 *
 * Controls:  Arrows move,  Z fire,  X focus (slow, precise),  Space start/retry,
 *            Cmd-Q quit.
 *
 * Tunables are grouped at the top. Built with Retro68.
 */

#include <Quickdraw.h>
#include <MacTypes.h>
#include <Windows.h>
#include <Fonts.h>
#include <Events.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <ToolUtils.h>
#include <Memory.h>
#include <string.h>
#include <stdio.h>

/* ---- tunables ---------------------------------------------------------- */
#define LOGIC_HZ    30            /* fixed logic ticks per second           */
#define TPL         (60/LOGIC_HZ) /* TickCount ticks per logic step         */
#define ENEMY_MAX   96            /* thin this if it gets choppy            */
#define PSHOT_MAX   24
#define BOSS_HP     320
#define PLAYER_LIVES 3

/* ---- geometry ---------------------------------------------------------- */
#define FIELD    128
#define SCALE    2
#define PLAYTOP  24
#define WIN_W    (FIELD*SCALE)               /* 256 */
#define WIN_H    (PLAYTOP + FIELD*SCALE)     /* 280 */
#define SX(l)    ((short)((l)*SCALE))
#define SY(l)    ((short)(PLAYTOP + (l)*SCALE))
#define FP       16

/* virtual key codes */
#define K_LEFT 0x7B
#define K_RIGHT 0x7C
#define K_DOWN 0x7D
#define K_UP   0x7E
#define K_Z    0x06
#define K_X    0x07
#define K_SPACE 0x31
#define KEYDOWN(km,k) ((((unsigned char*)(km))[(k)>>3] >> ((k)&7)) & 1)

enum { S_TITLE = 0, S_PLAY, S_WIN, S_LOSE };
enum { B_CIRCLE = 0, B_DIAMOND, B_SQUARE };

typedef struct { short x, y, vx, vy; unsigned char on, kind; } Shot;

static Shot enemy[ENEMY_MAX];
static Shot pshot[PSHOT_MAX];

static short px, py, lives, iframe, fireCd;
static short bossX, bossY;
static long  bossHP;
static short phase, lastPhase, phaseFlash;
static long  bt;
static short spiral, tosc;
static short state;
static long  score;

static GrafPort gOff;
static BitMap   gOffBits;
static Pattern  patStars, patBoss;

static short SINT[256];
static unsigned long rs = 2463534242UL;

/* fps */
static short fps = 0, fpsCount = 0;
static long  fpsMark = 0;

/* ---- math -------------------------------------------------------------- */

static short rnd(short n) { rs = rs*1103515245UL + 12345UL; return (short)(((rs>>16)&0x7fff)%(n>0?n:1)); }

static void buildSin(void)
{
    short i;
    for (i = 0; i < 256; i++) {
        double deg = i * 360.0 / 256.0;
        double s;
        if (deg <= 180.0) { double x = deg;         s =  4*x*(180-x)/(40500 - x*(180-x)); }
        else              { double x = deg - 180.0; s = -(4*x*(180-x)/(40500 - x*(180-x))); }
        SINT[i] = (short)(s * 256);
    }
}
static short sinI(short a) { return SINT[a & 255]; }
static short cosI(short a) { return SINT[(a + 64) & 255]; }

static short isqrt(long v)
{
    long x = 0, b = 1L << 15;
    if (v < 0) v = 0;
    while (b > v) b >>= 2;
    while (b) { if (v >= x + b) { v -= x + b; x = (x >> 1) + b; } else x >>= 1; b >>= 2; }
    return (short)x;
}

static void setPat(Pattern *p, const unsigned char *b) { memcpy(p, b, 8); }

/* ---- offscreen --------------------------------------------------------- */

static Boolean makeOffscreen(void)
{
    Rect b; short rb; long sz;
    SetRect(&b, 0, 0, WIN_W, WIN_H);
    rb = ((WIN_W + 15) / 16) * 2;
    sz = (long)rb * WIN_H;
    gOffBits.baseAddr = NewPtr(sz);
    if (gOffBits.baseAddr == NULL) return false;
    gOffBits.rowBytes = rb;
    gOffBits.bounds = b;
    OpenPort(&gOff);
    SetPortBits(&gOffBits);
    gOff.portRect = b;
    ClipRect(&b);
    RectRgn(gOff.visRgn, &b);
    EraseRect(&b);
    return true;
}

static void blit(WindowPtr w)
{
    Rect s = gOffBits.bounds;
    SetPort(w);
    CopyBits(&gOffBits, &w->portBits, &s, &w->portRect, srcCopy, NULL);
}

/* ---- spawning ---------------------------------------------------------- */

static void spawnE(short ox, short oy, short ang, short speed, unsigned char kind)
{
    short i;
    for (i = 0; i < ENEMY_MAX; i++)
        if (!enemy[i].on) {
            enemy[i].on = 1; enemy[i].kind = kind;
            enemy[i].x = ox * FP; enemy[i].y = oy * FP;
            enemy[i].vx = (short)((long)speed * cosI(ang) / 256);
            enemy[i].vy = (short)((long)speed * sinI(ang) / 256);
            return;
        }
}

static void spawnAim(short ox, short oy, short speed, unsigned char kind, short off)
{
    short dx = px - ox, dy = py - oy, dist, vx, vy, i;
    dist = isqrt((long)dx * dx + (long)dy * dy); if (dist < 1) dist = 1;
    vx = (short)((long)speed * dx / dist);
    vy = (short)((long)speed * dy / dist);
    if (off) {
        short c = cosI(off), s = sinI(off);
        short nvx = (short)(((long)vx * c - (long)vy * s) / 256);
        short nvy = (short)(((long)vx * s + (long)vy * c) / 256);
        vx = nvx; vy = nvy;
    }
    for (i = 0; i < ENEMY_MAX; i++)
        if (!enemy[i].on) {
            enemy[i].on = 1; enemy[i].kind = kind;
            enemy[i].x = ox * FP; enemy[i].y = oy * FP;
            enemy[i].vx = vx; enemy[i].vy = vy;
            return;
        }
}

static void spawnP(void)
{
    short i, made = 0;
    for (i = 0; i < PSHOT_MAX && made < 2; i++)
        if (!pshot[i].on) {
            pshot[i].on = 1;
            pshot[i].x = (px + (made ? 3 : -3)) * FP;
            pshot[i].y = py * FP;
            pshot[i].vx = 0; pshot[i].vy = -64;
            made++;
        }
}

static void clearEnemies(void) { short i; for (i = 0; i < ENEMY_MAX; i++) enemy[i].on = 0; }

/* ---- game reset -------------------------------------------------------- */

static void newGame(void)
{
    clearEnemies();
    { short i; for (i = 0; i < PSHOT_MAX; i++) pshot[i].on = 0; }
    px = 64; py = 108; lives = PLAYER_LIVES; iframe = 0; fireCd = 0;
    bossX = 64; bossY = 22; bossHP = BOSS_HP;
    phase = 0; lastPhase = 0; phaseFlash = 0; bt = 0; spiral = 0; tosc = 0;
    score = 0;
    state = S_PLAY;
}

/* ---- boss AI ----------------------------------------------------------- */

static void bossFire(void)
{
    if (phase == 0) {                       /* rotating radial rings */
        if (bt % 16 == 0) {
            short k, base = (short)((bt / 16) * 6);
            for (k = 0; k < 12; k++) spawnE(bossX, bossY, base + k * 21, 18, B_CIRCLE);
        }
    } else if (phase == 1) {                /* aimed 5-fans */
        if (bt % 18 == 0) {
            short k; for (k = -2; k <= 2; k++) spawnAim(bossX, bossY, 22, B_DIAMOND, k * 10);
        }
    } else if (phase == 2) {                /* twin spiral */
        if (bt % 3 == 0) {
            spawnE(bossX, bossY, spiral, 20, B_CIRCLE);
            spawnE(bossX, bossY, (short)(spiral + 128), 20, B_CIRCLE);
            spiral += 11;
        }
    } else {                                /* walls with a gap + aimed */
        if (bt % 20 == 0) {
            short gap = 6 + rnd(96), x;
            for (x = 4; x < 124; x += 8)
                if (x < gap || x > gap + 22) spawnE(x, 8, 64, 16, B_SQUARE);
        }
        if (bt % 26 == 0) { short k; for (k = -1; k <= 1; k++) spawnAim(bossX, bossY, 24, B_DIAMOND, k * 12); }
    }
}

/* ---- fixed-step logic -------------------------------------------------- */

static void stepLogic(void)
{
    unsigned char km[16];
    short i, spd, np;

    tosc++;
    bossX = 64 + (short)((long)sinI(tosc) * 42 / 256);
    bossY = 22 + (short)((long)sinI((short)(tosc * 2)) * 4 / 256);

    /* phase from HP */
    if (bossHP > BOSS_HP*3/4) np = 0;
    else if (bossHP > BOSS_HP/2) np = 1;
    else if (bossHP > BOSS_HP/4) np = 2;
    else np = 3;
    if (np != lastPhase) { phase = np; lastPhase = np; clearEnemies(); phaseFlash = 12; if (iframe < 20) iframe = 20; }
    if (phaseFlash > 0) phaseFlash--;

    /* input */
    GetKeys(km);
    spd = KEYDOWN(km, K_X) ? 1 : 2;
    if (KEYDOWN(km, K_LEFT))  px -= spd;
    if (KEYDOWN(km, K_RIGHT)) px += spd;
    if (KEYDOWN(km, K_UP))    py -= spd;
    if (KEYDOWN(km, K_DOWN))  py += spd;
    if (px < 4) px = 4; if (px > FIELD-4) px = FIELD-4;
    if (py < 4) py = 4; if (py > FIELD-4) py = FIELD-4;

    if (fireCd > 0) fireCd--;
    if (KEYDOWN(km, K_Z) && fireCd == 0) { spawnP(); fireCd = 4; }

    if (iframe > 0) iframe--;

    /* boss shoots (not during the brief phase flash) */
    bt++;
    if (phaseFlash == 0) bossFire();

    /* move enemy bullets, test player hit */
    for (i = 0; i < ENEMY_MAX; i++) {
        if (!enemy[i].on) continue;
        enemy[i].x += enemy[i].vx;
        enemy[i].y += enemy[i].vy;
        {
            short lx = enemy[i].x >> 4, ly = enemy[i].y >> 4;
            if (lx < -6 || lx > FIELD+6 || ly < -6 || ly > FIELD+6) { enemy[i].on = 0; continue; }
            if (iframe == 0) {
                short ddx = lx - px, ddy = ly - py;
                if ((long)ddx*ddx + (long)ddy*ddy <= 9) {   /* hit radius ~3 */
                    lives--; iframe = 50; clearEnemies();
                    if (lives <= 0) state = S_LOSE;
                    break;
                }
            }
        }
    }

    /* move player shots, test boss hit */
    for (i = 0; i < PSHOT_MAX; i++) {
        if (!pshot[i].on) continue;
        pshot[i].y += pshot[i].vy;
        {
            short lx = pshot[i].x >> 4, ly = pshot[i].y >> 4;
            if (ly < 0) { pshot[i].on = 0; continue; }
            {
                short ddx = lx - bossX, ddy = ly - bossY;
                if (ddx > -11 && ddx < 11 && ddy > -8 && ddy < 8) {
                    pshot[i].on = 0; bossHP--; score += 1;
                    if (bossHP <= 0) { bossHP = 0; state = S_WIN; }
                }
            }
        }
    }
}

/* ---- drawing ----------------------------------------------------------- */

static void drawDiamond(short sx, short sy, short r)
{
    PolyHandle p = OpenPoly();
    MoveTo(sx, sy - r); LineTo(sx + r, sy); LineTo(sx, sy + r); LineTo(sx - r, sy); LineTo(sx, sy - r);
    ClosePoly(); FillPoly(p, &qd.black); KillPoly(p);
}

static void drawBullet(Shot *b)
{
    short sx = SX(b->x >> 4), sy = SY(b->y >> 4);
    Rect r;
    if (b->kind == B_CIRCLE)      { SetRect(&r, sx-3, sy-3, sx+3, sy+3); PaintOval(&r); }
    else if (b->kind == B_DIAMOND){ drawDiamond(sx, sy, 4); }
    else                          { SetRect(&r, sx-3, sy-3, sx+4, sy+4); FrameRect(&r); }  /* hollow square */
}

static void drawPlayer(void)
{
    short sx = SX(px), sy = SY(py);
    PolyHandle p;
    if (iframe && (iframe & 2)) return;                  /* blink when hit */
    p = OpenPoly();
    MoveTo(sx, sy - 6); LineTo(sx + 5, sy + 5); LineTo(sx - 5, sy + 5); LineTo(sx, sy - 6);
    ClosePoly(); FillPoly(p, &qd.black); KillPoly(p);
    /* single-pixel hitbox shown as a white dot */
    { Rect h; SetRect(&h, sx-1, sy-1, sx+1, sy+1); EraseRect(&h); }
}

static void drawBoss(void)
{
    short sx = SX(bossX), sy = SY(bossY);
    Rect r; PolyHandle p;
    /* dithered body diamond */
    p = OpenPoly();
    MoveTo(sx, sy - 16); LineTo(sx + 22, sy); LineTo(sx, sy + 16); LineTo(sx - 22, sy); LineTo(sx, sy - 16);
    ClosePoly();
    if (phaseFlash && (phaseFlash & 1)) FillPoly(p, &qd.ltGray); else FillPoly(p, &patBoss);
    FramePoly(p); KillPoly(p);
    /* solid core marker */
    SetRect(&r, sx-4, sy-4, sx+4, sy+4); PaintOval(&r);
}

static void drawCenter(short y, char *s)
{
    short w = TextWidth(s, 0, (short)strlen(s));
    MoveTo((WIN_W - w) / 2, y);
    DrawText(s, 0, (short)strlen(s));
}

static void render(void)
{
    short i;
    Rect r, play;
    char buf[80];

    SetPort(&gOff);
    PenNormal();
    r = gOffBits.bounds;
    EraseRect(&r);

    SetRect(&play, 0, PLAYTOP, WIN_W, WIN_H);
    FillRect(&play, &patStars);                 /* faint starfield backdrop */
    FrameRect(&play);

    if (state == S_TITLE) {
        drawCenter(96, "ONE FOE");
        drawCenter(130, "Press Space to fight");
        drawCenter(150, "Arrows move   Z fire   X focus");
        drawCenter(170, "Beat all four boss phases");
    } else {
        drawBoss();
        for (i = 0; i < ENEMY_MAX; i++) if (enemy[i].on) drawBullet(&enemy[i]);
        for (i = 0; i < PSHOT_MAX; i++) if (pshot[i].on) {
            short sx = SX(pshot[i].x >> 4), sy = SY(pshot[i].y >> 4);
            Rect b; SetRect(&b, sx-1, sy-4, sx+1, sy+4); PaintRect(&b);   /* thin bar */
        }
        drawPlayer();

        if (state == S_WIN)  { drawCenter(120, "YOU WIN");   drawCenter(150, "Press Space to play again"); }
        if (state == S_LOSE) { drawCenter(120, "GAME OVER"); drawCenter(150, "Press Space to retry"); }
    }

    /* HUD */
    SetRect(&r, 0, 0, WIN_W, PLAYTOP - 1); EraseRect(&r);
    MoveTo(4, 10);
    sprintf(buf, "ONE FOE  Lives %d  Phase %d  %d fps", lives, phase + 1, fps);
    DrawText(buf, 0, (short)strlen(buf));
    /* boss HP bar */
    SetRect(&r, 4, 14, WIN_W - 4, 20); FrameRect(&r);
    if (bossHP > 0) {
        short w = (short)((long)(WIN_W - 10) * bossHP / BOSS_HP);
        SetRect(&r, 5, 15, 5 + w, 19); PaintRect(&r);
    }
}

/* ---- main -------------------------------------------------------------- */

int main(void)
{
    WindowPtr win;
    EventRecord ev;
    Rect r;
    long acc;
    short sw, sh, winL, winT;
    unsigned char stars[8] = {0x00,0x40,0x00,0x08,0x00,0x02,0x00,0x20};
    unsigned char boss[8]  = {0xAA,0x55,0xAA,0x55,0xAA,0x55,0xAA,0x55};

    InitGraf(&qd.thePort);
    InitFonts(); InitWindows(); InitMenus(); TEInit(); InitDialogs(NULL); InitCursor();

    sw = qd.screenBits.bounds.right; sh = qd.screenBits.bounds.bottom;
    winL = (sw - WIN_W) / 2; if (winL < 4) winL = 4;
    winT = (sh - WIN_H) / 2 + 4; if (winT < 40) winT = 40;
    SetRect(&r, winL, winT, winL + WIN_W, winT + WIN_H);
    win = NewWindow(NULL, &r, "\pOne Foe", true, documentProc, (WindowPtr)-1L, true, 0);
    SetPort(win);

    buildSin();
    setPat(&patStars, stars); setPat(&patBoss, boss);
    if (!makeOffscreen()) { SysBeep(20); return 1; }
    state = S_TITLE;

    acc = TickCount();
    fpsMark = TickCount();
    for (;;) {
        if (WaitNextEvent(everyEvent, &ev, 1, NULL)) {
            if (ev.what == keyDown || ev.what == autoKey) {
                char c = (char)(ev.message & charCodeMask);
                if ((ev.modifiers & cmdKey) && (c == 'q' || c == 'Q')) break;
                if (c == ' ' && state != S_PLAY) { newGame(); acc = TickCount(); }
            } else if (ev.what == mouseDown) {
                WindowPtr wp; short part = FindWindow(ev.where, &wp);
                if (part == inGoAway && TrackGoAway(wp, ev.where)) break;
                else if (part == inDrag) DragWindow(wp, ev.where, &qd.screenBits.bounds);
            } else if (ev.what == updateEvt) {
                BeginUpdate((WindowPtr)ev.message); blit(win); EndUpdate((WindowPtr)ev.message);
            }
        }

        /* fixed-timestep logic, catch up as needed */
        {
            long now = TickCount(), guard = 0;
            while (now - acc >= TPL && guard < 6) {
                if (state == S_PLAY) stepLogic();
                acc += TPL; guard++;
            }
            if (now - acc > TPL * 8) acc = now;   /* don't spiral if stalled */
        }

        render();
        blit(win);

        /* fps meter */
        fpsCount++;
        if (TickCount() - fpsMark >= 60) { fps = fpsCount; fpsCount = 0; fpsMark = TickCount(); }
    }

    if (gOffBits.baseAddr) DisposePtr(gOffBits.baseAddr);
    DisposeWindow(win);
    return 0;
}
