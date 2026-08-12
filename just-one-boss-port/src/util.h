/*
 * util.h -- small game-side helpers that the Lua cart defined globally
 * (draw_sprite, print_centered, color_wash, ternary math, rnd).  These sit on
 * top of the p8 layer, not inside it.
 */
#ifndef UTIL_H
#define UTIL_H

#include "engine.h"   /* num */

/* draw an arbitrary sw x sh region of the sheet at (x,y), +0.5 round like Lua */
void draw_sprite(num x, num y, int sx, int sy, int sw, int sh, int flip_x);
/* the entity method self:draw_sprite(dx,dy,...): anchors the sheet region at
 * (self.x + draw_offset_x - dx, self.y + draw_offset_y - dy) */
void ent_sprite(Entity *e, num dx, num dy,
                int sx, int sy, int sw, int sh, int flip_x);
void print_centered(const char *s, num x, int y, int c);
void color_wash(int c);           /* pal(1..15) -> c */

/* PICO-8-style rng */
void  p8_srand(unsigned long seed);
num   rnd_f(num maxv);            /* [0, maxv) */
int   rnd_int(int lo, int hi);    /* inclusive */
int   rnd_dir(void);              /* -1 or +1 */

/* misc math the game uses */
num   p8_sin(num turns);          /* PICO-8 sin: input in turns, y-down */
num   p8_cos(num turns);

/* idle-bob: eases idle_mult toward is_idle and sets draw_offset_x/y (Lua's
 * calc_idle_mult).  n = vertical bob amplitude. */
void  calc_idle_mult(Entity *e, int f, num n);
int   imin(int a, int b);
int   imax(int a, int b);
num   fmid(num a, num b, num c);  /* median of 3 */

#endif /* UTIL_H */
