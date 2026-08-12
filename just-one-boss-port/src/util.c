/*
 * util.c -- game-side helpers layered over p8.c.  See util.h.
 */
#include "util.h"
#include "p8.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void draw_sprite(num x, num y, int sx, int sy, int sw, int sh, int flip_x)
{
    /* Lua: sspr(sx,sy,sw,sh,x+0.5,y+0.5,sw,sh,flip) */
    p8_sspr(sx, sy, sw, sh, (int)(x + 0.5f), (int)(y + 0.5f), sw, sh, flip_x, 0);
}

void ent_sprite(Entity *e, num dx, num dy,
                int sx, int sy, int sw, int sh, int flip_x)
{
    draw_sprite(e->x + e->draw_offset_x - dx, e->y + e->draw_offset_y - dy,
                sx, sy, sw, sh, flip_x);
}

void print_centered(const char *s, num x, int y, int c)
{
    int n = 0;
    const char *p = s;
    while (*p++) n++;
    p8_print(s, (int)(x - 2 * n), y, c);   /* 4px/char -> half is 2px */
}

void color_wash(int c)
{
    int i;
    for (i = 1; i <= 15; i++) p8_pal(i, c);
}

/* ---- PICO-8-compatible-ish PRNG (deterministic LCG) ---- */
static unsigned long rng_state = 0x1234567u;
void p8_srand(unsigned long seed) { rng_state = seed ? seed : 1; }
static unsigned long rng_next(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return (rng_state >> 8) & 0xFFFFFFu;
}
num rnd_f(num maxv) { return (num)((double)rng_next() / (double)0x1000000) * maxv; }
int rnd_int(int lo, int hi) { return lo + (int)floor(rnd_f((num)(1 + hi - lo))); }
int rnd_dir(void) { return 2 * rnd_int(0, 1) - 1; }

/* ---- trig: PICO-8 measures angles in *turns* and flips y (screen-down) ----
 * 256-entry lookup table: software double-precision sin() costs ~1000+ cycles
 * per call on the FPU-less 68000 and these run every frame (idle bob, hats,
 * particles).  Visual precision at 1/256 turn is far below one pixel. */
static num   sin_lut[256];
static int   sin_lut_ready;
static void  sin_lut_init(void)
{
    int i;
    for (i = 0; i < 256; i++)
        sin_lut[i] = (num)sin((double)i * (2.0 * M_PI / 256.0));
    sin_lut_ready = 1;
}
static num lut_sin_turns(num turns)
{
    int idx;
    if (!sin_lut_ready) sin_lut_init();
    idx = (int)(turns * 256.0f);
    if (turns < 0 && (num)idx * (1.0f / 256.0f) > turns) idx--;   /* floor */
    return sin_lut[idx & 255];
}
num p8_sin(num turns) { return -lut_sin_turns(turns); }
num p8_cos(num turns) { return  lut_sin_turns(turns + 0.25f); }

void calc_idle_mult(Entity *e, int f, num n)
{
    e->idle_mult = fmid(0, 1, e->idle_mult + (e->is_idle ? 0.05f : -0.05f));
    e->draw_offset_x = e->idle_mult * 3 * p8_sin((num)f / 64);
    e->draw_offset_y = e->idle_mult * n * p8_sin((num)f / 32);
}

int imin(int a, int b) { return a < b ? a : b; }
int imax(int a, int b) { return a > b ? a : b; }
num fmid(num a, num b, num c)
{
    num lo = a < b ? a : b, hi = a < b ? b : a;
    if (c < lo) return lo;
    if (c > hi) return hi;
    return c;
}
