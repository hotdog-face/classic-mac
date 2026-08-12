/*
 * entities.c -- entity class registry + ported entity behaviours.
 *
 * Each Lua entity class becomes C draw/update (and optional init/on_hurt/
 * on_death) functions registered into g_classes[].  Per-type state lives in the
 * Entity scratch bag (n[]/iv[]/ref[]) aliased with #defines.  See PORTING-PLAN.
 *
 * PHASE 3 (playable slice: title -> curtains -> move -> collect tutorial tiles):
 *   4 curtains  5/6 screen+title  10 player_figment  11 player
 *   12 player_health  13 boss_health  14 magic_tile_spawn  15 magic_tile
 *   21 particle  27 poof  28 pain  29 points
 * The boss (22) and its attack entities are Phase 4 -- magic_tile's boss-
 * milestone branches are stubbed where marked.
 */
#include "entities.h"
#include "engine.h"
#include "game.h"
#include "p8.h"
#include "util.h"
#include "mac_input.h"
#include <math.h>

/* sfx(): route to the platform audio hook (Sound Manager on the Mac, no-op on
 * the host build).  The PICO-8 channel arg is ignored -- the Classic plays one
 * channel, latest-wins (see mac_audio.c). */
static void sfx(int n, int chan) { (void)chan; platform_sfx(n); }

/* forward decls (Phase 4 boss, referenced by the tutorial milestone) */
static void boss_intro(Entity *e);
static void boss_health_milestone(int health);
static void player_get_hurt(Entity *e);
static void victory_on_activated(Entity *self);
static void gameover_continue(Entity *self);
static void trigger_game_over(Entity *player);
static void trigger_victory(void);

/* ===================================================================
 * class 4: curtains  (layer 14, pause-immune)  iv0 anim, n0 amount_closed
 * =================================================================== */
#define cur_anim          iv[0]
#define cur_amount_closed n[0]

void curtains_set_anim(Entity *self, int open)
{
    self->cur_anim = open;
    self->default_counter = 100;
}
static void curtains_draw_one(Entity *self, num x, int dir)
{
    int x2;
    num amt = self->cur_amount_closed;
    /* fully open, the fill is a black-on-black edge sliver -- skipping it
     * matters a lot: it was touching all 128 rows every frame, forcing the
     * dirty-row renderer to repaint the whole screen during gameplay */
    if (amt < 2) return;
    p8_rectfill((int)(x - 10 * dir), 0, (int)(x + dir * amt), 127, 0);
    /* pleat lines only while the curtain is actually drawn across the stage --
     * fully open they were just speckle columns over the play area */
    if (amt < 6) return;
    for (x2 = 10; x2 <= 63; x2 += 14) {
        int x3 = (int)(x + 0.5f + dir * x2 * (1 + amt / 62) / 2);
        int y1 = (int)(60 + 40 * p8_cos((num)x2 / 90 - 0.02f));
        p8_line(x3, 11, x3, y1, 6);
    }
}
static void curtains_draw(Entity *self)
{
    curtains_draw_one(self, 1, 1);
    curtains_draw_one(self, 125, -1);
}
static void curtains_update(Entity *self)
{
    num t = easing_apply(EASE_OUT_IN, (num)self->default_counter / 100);
    self->cur_amount_closed = 62 * t;
    if (self->cur_anim != 1) self->cur_amount_closed = 62 - self->cur_amount_closed;
}

/* ===================================================================
 * class 5/6: screen base + title_screen
 *   iv0 is_active, iv1 frames_until_active
 * =================================================================== */
#define scr_is_active           iv[0]
#define scr_frames_until_active iv[1]

/* draw_prompt: "press [X] to <text>" blinking once active; returns shown? */
static int screen_draw_prompt(Entity *self, const char *text)
{
    if (self->frames_alive % 30 < 22 && self->scr_is_active) {
        char buf[40];
        int i = 0;
        const char *pre = "press    to ";
        while (pre[i]) { buf[i] = pre[i]; i++; }
        { const char *t = text; while (*t) buf[i++] = *t++; }
        buf[i] = 0;
        print_centered(buf, 63, 99, 13);
        { int n = 0; const char *t = text; while (*t++) n++;
          p8_spr(190, 63 - 2 * n, 98, 1, 1, 0, 0); }   /* button glyph */
        return 1;
    }
    return 0;
}

static void title_on_activated(Entity *self)
{
    (void)self;
    /* music(0); */ sfx(9, 3);            /* game start */
    g_hard_mode = 0;                       /* normal start; the hard-mode
                                              branch re-sets these after */
    g_score_data_index = 0;
    g_time_data_index = 1;
    g_score = (g_starting_phase > 0) ? 40 : 0;
    g_timer_seconds = 0;
    start_game(g_starting_phase);
}

/* dispatch a screen's "activated" behaviour by class */
static void screen_activate(Entity *self)
{
    switch (self->class_id) {
    case 6: title_on_activated(self); break;
    case 7: show_title_screen(1); break;        /* credits -> title */
    case 8: victory_on_activated(self); break;
    case 9: gameover_continue(self); break;
    default: break;
    }
}

static void screen_check_activation(Entity *self)
{
    if (decrement_counter_prop(&self->scr_frames_until_active))
        self->scr_is_active = 1;
    if (self->scr_is_active && p8_btnp(1)) {   /* advance = RIGHT (PICO-8 btn 1) */
        sfx(24, 3);
        self->scr_is_active = 0;
        slide(self, 1);
        screen_activate(self);
        if (self->class_id != 6) self->frames_to_death = 130;  /* slide off + die */
    }
}

static void title_draw(Entity *self)
{
    ent_sprite(self, 23, -29, 0, 71, 47, 16, 0);   /* logo top */
    ent_sprite(self, 23, -47, 0, 88, 47, 40, 0);   /* logo bottom */
    if (screen_draw_prompt(self, "start") && g_dget(0) > 0) {
        /* hard mode unlocks once a win is on record (scores now persist) */
        print_centered("or    for hard mode", 63, 108, 13);
        p8_spr(190, 36, 107, 1, 1, 1, 0);          /* flipped = left arrow */
    }
}
static void title_update(Entity *self)
{
    screen_check_activation(self);
    if (self->scr_is_active && p8_btnp(0) && g_dget(0) > 0) {  /* LEFT = hard */
        sfx(24, 3);
        self->scr_is_active = 0;
        slide(self, -1);
        title_on_activated(self);
        g_hard_mode = 1;                           /* after: activation resets it */
        g_score_data_index = 2;
        g_time_data_index = 3;
    }
}
static void title_init(Entity *self)
{
    self->x = 63;
    self->scr_frames_until_active = 5;
}

/* ===================================================================
 * class 10: player_figment
 * =================================================================== */
static void figment_draw(Entity *self)
{
    ent_sprite(self, 5, 6, 89, self->frames_alive < 190 ? 13 : 5, 11, 8, 0);
}

/* ===================================================================
 * class 11: player   (default_counter = bump frames)
 * =================================================================== */
#define pl_facing        iv[0]
#define pl_step_frames   iv[1]
#define pl_step_dir      iv[2]   /* -1 = none */
#define pl_next_step_dir iv[3]   /* -1 = none */
#define pl_teeter_frames iv[4]
#define pl_stun_frames   iv[5]
#define pl_prev_col      iv[6]
#define pl_prev_row      iv[7]

static void player_undo_step(Entity *e)
{
    e->x = 10 * e->pl_prev_col - 5;
    e->y = 8 * e->pl_prev_row - 4;
    e->pl_step_frames = 0;
    e->pl_step_dir = -1;
    e->pl_next_step_dir = -1;
}
static int player_step(Entity *e, int dir)
{
    if (e->pl_step_dir == -1 && e->pl_teeter_frames <= 0 &&
        e->default_counter <= 0 && e->pl_stun_frames <= 0) {
        if (g_boss_health && ((Entity *)g_boss_health)->n[0] <= 0 &&
            g_boss_phase <= 0 && !g_boss)
            sfx(29, 1);                     /* player step */
        e->pl_facing = dir;
        e->pl_step_dir = dir;
        e->pl_step_frames = 4;
        e->pl_next_step_dir = -1;
        return 1;
    }
    return 0;
}
static void player_queue_step(Entity *e, int dir)
{
    if (!player_step(e, dir)) e->pl_next_step_dir = dir;
}
static void player_bump(Entity *e)
{
    sfx(20, 2);
    player_undo_step(e);
    e->default_counter = 11;
    freeze_and_shake_screen(0, 5);
}
static void player_apply_step(Entity *e)
{
    int dir = e->pl_step_dir, dist = e->pl_step_frames;
    if (dir != -1) {
        if (dir > 1) e->vy += (2 * dir - 5) * (dist > 2 ? dist - 1 : dist);
        else         e->vx += 2 * dir * dist - dist;
        if (decrement_counter_prop(&e->pl_step_frames)) {
            e->pl_step_dir = -1;
            if (e->pl_next_step_dir != -1) {
                player_step(e, e->pl_next_step_dir);
                player_apply_step(e);
            }
        }
    }
}
static void player_check_inputs(Entity *e)
{
    int dir;
    for (dir = 0; dir < 4; dir++)
        if (p8_btnp(dir)) player_queue_step(e, dir);
}
static void player_init(Entity *e)
{
    e->x = 45; e->y = 20;
    e->pl_facing = 0;
    e->pl_step_dir = -1;
    e->pl_next_step_dir = -1;
    e->hurtbox_channel = 1;      /* player */
}
static void player_update(Entity *e)
{
    decrement_counter_prop(&e->pl_stun_frames);
    decrement_counter_prop(&e->pl_teeter_frames);

    player_check_inputs(e);
    if (e->pl_next_step_dir != -1 && e->pl_step_dir == -1)
        player_step(e, e->pl_next_step_dir);

    e->pl_prev_col = entity_col(e);
    e->pl_prev_row = entity_row(e);

    if (e->pl_stun_frames <= 0) {
        int col, row;
        Entity *occ;
        e->vx = 0; e->vy = 0;
        player_apply_step(e);
        entity_apply_velocity(e);
        col = entity_col(e); row = entity_row(e); occ = get_tile_occupant(e);
        if (e->pl_prev_col != col || e->pl_prev_row != row) {
            if (col < 1 || col > 8 || row < 1 || row > 5) {
                sfx(19, 3);                 /* teeter */
                player_undo_step(e);
                e->pl_teeter_frames = 11;
            }
            /* bump into an occupant, or into the invisible mid-line wall
             * while the player reflection exists (each of you owns a half) */
            if (occ || (g_player_reflection &&
                        ((e->pl_prev_col < 5) != (col < 5)))) {
                player_bump(e);
                if (occ) occ->on_bumped(occ);   /* e.g. smash a coin */
            }
        }
    }
    e->skip_default_move = 1;                /* Lua `return true` */
}
/* shared body renderer: the player draws (12,13,0), the reflection (11,3,3) */
static void player_draw_body(Entity *e, int pc, int sc, int tc)
{
    if (e->invincibility_frames % 4 < 2 || e->pl_stun_frames > 0) {
        int facing = e->pl_facing;
        int sx = 0, sy = 0, sh = 8, dx = 3 + 4 * facing, dy = 6;
        int flipped = (facing == 0);
        int c = (e->pl_teeter_frames % 4 < 2) ? 8 : 9;
        if (facing == 2)      { sy = 8;  sh = 11; dx = 5; }
        else if (facing == 3) { sy = 19; sh = 11; dx = 5; dy = 9; }
        if (e->pl_step_frames > 0) sx = 44 - 11 * e->pl_step_frames;
        if (e->pl_teeter_frames > 0 || e->default_counter > 0) {
            sx = 66;
            if (e->default_counter <= 0) { p8_palt(c, 1); p8_pal(17 - c, sc); sx = 44; }
            if (facing > 1) dy += 13 - 5 * facing; else dx += 4 - facing * 8;
            if (e->pl_teeter_frames < 3 && e->default_counter < 3) sx = 55;
        }
        if (e->pl_stun_frames > 0) { sx = 78; sy = 11; sh = 10; dx = 5; dy = 8;
            flipped = (e->pl_stun_frames % 6 > 2); }
        p8_pal(12, pc); p8_pal(13, sc); p8_pal(1, tc);
        ent_sprite(e, dx, dy, sx, sy, 11, sh, flipped);
    }
}
/* the hero draws as pure SOLID white with black line detail (no dithering,
 * owner direction); the reflection keeps a light stipple so the ghost copy
 * stays visually distinct */
static void player_draw(Entity *e) { player_draw_body(e, 7, 7, 0); }

/* ===================================================================
 * class 16: player_reflection (extends player)
 * A mirrored copy on the LEFT half: x = 80 - player.x, facing flipped.
 * It collects tiles (hitbox 2 from the sprite flags) and getting hit hurts
 * the real player.  While it exists the player can't cross the mid-line.
 * =================================================================== */
static void preflect_copy(Entity *r)
{
    static const int mir[4] = { 1, 0, 2, 3 };
    Entity *p = g_player;
    if (!p) return;
    r->x = 80 - p->x;
    r->y = p->y;
    r->pl_facing         = mir[p->pl_facing & 3];
    r->pl_step_frames    = p->pl_step_frames;
    r->pl_stun_frames    = p->pl_stun_frames;
    r->pl_teeter_frames  = p->pl_teeter_frames;
    r->default_counter   = p->default_counter;
    r->invincibility_frames = p->invincibility_frames;
    r->frames_alive      = p->frames_alive;
}
static void preflect_init(Entity *e)
{
    e->hurtbox_channel = 1;          /* attacks hurt it (and thus the player) */
    preflect_copy(e);
    spawn_entity(27, e->x, e->y);    /* poof in */
    sfx(11, 2);
}
static void preflect_update(Entity *e)
{
    int pc = entity_col(e), pr = entity_row(e);
    preflect_copy(e);
    if (pc != entity_col(e) || pr != entity_row(e)) {
        Entity *occ = get_tile_occupant(e);
        if (occ) {                    /* reflection walked into an obstacle */
            occ->on_bumped(occ);
            if (g_player) player_bump(g_player);
            preflect_copy(e);
        }
    }
    e->skip_default_move = 1;
}
static void preflect_on_hurt(Entity *e)
{
    spawn_entity(28, e->x, e->y);     /* pain flash at the reflection */
    if (g_player) player_get_hurt(g_player);
    preflect_copy(e);
}
static void preflect_draw(Entity *e) { player_draw_body(e, 11, 3, 3); }

/* ===================================================================
 * class 12: player_health   iv0 hearts, iv1 anim(0 none/1 gain/2 lose), iv2 visible
 * =================================================================== */
#define ph_hearts  iv[0]
#define ph_anim    iv[1]
#define ph_visible iv[2]
static void health_init(Entity *e)
{
    e->x = 63; e->y = 122; e->ph_hearts = 4;
}
/* 7x6 procedural heart icons (solid = a life, outline = an empty slot) --
 * line-art replacements for the sprite hearts, which dithered to near-
 * invisible on the black stage.  One byte per row, bit 6 = leftmost. */
static const unsigned char kHeartFull[6] = {
    0x36, 0x7F, 0x7F, 0x3E, 0x1C, 0x08
};
static const unsigned char kHeartLine[6] = {
    0x36, 0x49, 0x41, 0x22, 0x14, 0x08
};
static void draw_heart(int x, int y, const unsigned char *rows, int c)
{
    int r, b;
    for (r = 0; r < 6; r++)
        for (b = 0; b < 7; b++)
            if (rows[r] & (1 << (6 - b)))
                p8_pset(x + b, y + r, c);
}
static void health_draw(Entity *e)
{
    int i;
    if (!e->ph_visible) return;
    for (i = 1; i <= 4; i++) {
        int x = 46 + 9 * (i - 1), y = 119, dc = e->default_counter;
        if (e->ph_anim == 2 && i == e->ph_hearts + 1) {
            /* the heart being lost blinks solid/outline as it drains */
            draw_heart(x, y, (dc >= 15 || (dc + 1) % 4 < 2) ? kHeartFull
                                                            : kHeartLine, 7);
        } else if (i <= e->ph_hearts) {
            draw_heart(x, y, kHeartFull, 7);
        } else {
            draw_heart(x, y, kHeartLine, 7);
        }
    }
}
static void health_update(Entity *e) { if (e->counter_reached_zero) e->ph_anim = 0; }

/* ===================================================================
 * class 13: boss_health   n0 health, iv0 visible, iv1 rainbow_frames
 *   (default_counter = drain frames)
 * =================================================================== */
#define bh_health   n[0]
#define bh_visible  iv[0]
#define bh_rainbow  iv[1]
static void boss_health_draw(Entity *e)
{
    if (!e->bh_visible) return;
    /* solid white outline + solid fill (lines-and-solids style) */
    p8_rect(33, 2, 93, 8, e->bh_rainbow > 0 ? g_rainbow_color : 7);
    { int h = (int)e->bh_health;
      int x1 = 32 + h; if (x1 < 33) x1 = 33; if (x1 > 92) x1 = 92;
      p8_rectfill(33, 2, x1, 8, 7); }
}
static void boss_health_update(Entity *e)
{
    decrement_counter_prop(&e->bh_rainbow);
    if (e->default_counter > 0) e->bh_health -= 1;
}

/* ===================================================================
 * class 14: magic_tile_spawn   (grows a rect over its last 10 frames)
 * =================================================================== */
static void tile_spawn_draw(Entity *e)
{
    int f  = e->frames_alive;
    int f2 = e->frames_to_death;
    if (f2 == 10) sfx(8, 3);
    if (f2 <= 10) {
        int r = f2 + 3;
        p8_rect((int)e->x - r - 1, (int)e->y - r, (int)e->x + r + 1, (int)e->y + r,
                f < 4 ? 6 : 7);           /* bright shrinking telegraph */
    }
}
static void tile_spawn_on_death(Entity *e)
{
    freeze_and_shake_screen(0, 1);
    spawn_entity(15, e->x, e->y);                 /* magic_tile */
    spawn_particle_burst(e, 0, 4, 16, 4);
}

/* ===================================================================
 * class 15: magic_tile   (collectible; hurtbox 2 = pickup)
 * =================================================================== */
static void tile_init(Entity *e) { e->hurtbox_channel = 2; }
static void tile_draw(Entity *e)
{
    /* solid white tile marker (the rainbow swap dithered; the collect target
     * should be the most readable thing on the floor) */
    p8_pal(1, 7);
    ent_sprite(e, 4, 3, 55, 38, 9, 7, 0);
}
static void tile_update(Entity *e)
{
    if (get_tile_occupant(e)) { entity_die(e); spawn_magic_tile(10); }
}
static void tile_on_hurt(Entity *e)
{
    int health_change, i;
    num pre_health;
    Entity *bh = g_boss_health;
    freeze_and_shake_screen(2, 2);
    e->hurtbox_channel = 0;
    e->frames_to_death = 6;
    g_score_mult = g_score_mult + 1; if (g_score_mult > 8) g_score_mult = 8;
    sfx(9, 3);
    g_score += g_score_mult;

    /* points popup (entity 29) */
    { Entity *p = spawn_entity(29, e->x, e->y - 7); if (p) p->iv[0] = g_score_mult; }

    health_change = (g_boss_phase == 0) ? 12 : 6;
    pre_health = bh ? bh->bh_health : 0;

    /* SLICE simplification: fill the boss bar over the next frames instead of
     * choreographing per-particle promise flights (that promise-with-params
     * pattern is a Phase-4 task).  Cosmetic burst kept for feel. */
    spawn_particle_burst(e, 0, g_boss_phase >= 5 ? 15 : 25, 16, 10);
    if (bh) {
        for (i = 0; i < health_change; i++) {
            if (bh->bh_health < 60) {
                bh->bh_health = fmid(0, 60, bh->bh_health + 1);
                bh->bh_visible = 1;
                bh->bh_rainbow = 15;
                boss_health_milestone((int)bh->bh_health);  /* Phase 4 */
            }
        }
    }

    /* respawn the next tile (20-frame grace).  IMPORTANT: the Lua original
     * checks against the bar value BEFORE this tile's health lands (its
     * particles are still in flight), so use pre_health -- checking the
     * post-increment value made the final intro-triggering tile never spawn
     * and stalled the tutorial at 48/60 with a passive boss. */
    if (bh && health_change + pre_health < 60 && g_boss_phase < 5)
        spawn_magic_tile(((g_boss_phase < 1) ? 100 : 120) - imin(e->frames_alive, 20));
}

/* ===================================================================
 * class 21: particle   (a moving line segment)
 * =================================================================== */
#define pa_color  iv[0]
#define pa_prev_x n[0]
#define pa_prev_y n[1]
#define pa_gravity  n[2]
#define pa_friction n[3]
static void particle_update(Entity *e)
{
    e->vy += e->pa_gravity;
    e->vx *= e->pa_friction;
    e->vy *= e->pa_friction;
    e->pa_prev_x = e->x;
    e->pa_prev_y = e->y;
}
static void particle_draw(Entity *e)
{
    int c = (e->pa_color == 16) ? g_rainbow_color : e->pa_color;
    p8_line((int)e->x, (int)e->y, (int)e->pa_prev_x, (int)e->pa_prev_y, c);
}

/* helper: Lua's spawn_particle_burst.  Lives here (not game.c) so it can set the
 * particle's fields and then run the first-frame update+move, exactly like the
 * Lua init did after args were applied. */
void spawn_particle_burst(Entity *src, num dy, int count, int color, num speed)
{
    int i;
    for (i = 1; i <= count; i++) {
        num angle  = (i + rnd_f(0.7f)) / count;
        num pspeed = speed * (0.5f + rnd_f(0.7f));
        Entity *p = spawn_entity(21, src->x, src->y - dy);
        if (!p) continue;
        p->vx = pspeed * p8_cos(angle);
        p->vy = pspeed * p8_sin(angle) - speed / 2;
        p->pa_color = color;
        p->pa_gravity = 0.1f;
        p->pa_friction = 0.75f;
        p->frames_to_death = rnd_int(13, 19);
        particle_update(p);          /* first-frame init (set prev, apply drag) */
        entity_apply_velocity(p);
    }
}

/* ===================================================================
 * class 27/28/29: poof, pain, points
 * =================================================================== */
static void poof_draw(Entity *e)
{
    ent_sprite(e, 8, 8, 64 + 16 * (e->frames_alive / 3), 31, 16, 14, 0);
}
static void pain_draw(Entity *e) { ent_sprite(e, 11, 16, 105, 45, 23, 26, 0); }
static void points_draw(Entity *e)
{
    char buf[8];
    int p = e->iv[0], n = 0;
    /* "<points>00" */
    if (p >= 10) buf[n++] = '0' + (p / 10) % 10;
    buf[n++] = '0' + p % 10;
    buf[n++] = '0'; buf[n++] = '0'; buf[n] = 0;
    print_centered(buf, e->x + 1, (int)e->y, g_rainbow_color);
}
static void points_init(Entity *e) { e->vy = -0.5f; }

/* ===================================================================
 * PHASE 4a: the boss (magic_mirror), its hands, the laser attack, cards,
 * and player damage.  The full multi-phase promise choreography (cards/
 * flowers/coins/hats/reflections/hard-mode) is Phase 4b and needs the
 * action-sequence VM; here the boss runs an explicit laser-attack state
 * machine so the fight is real and can hurt the player.
 * =================================================================== */

/* ---- player damage (Lua player:on_hurt/get_hurt) ---- */
static void player_get_hurt(Entity *e)
{
    Entity *ph = g_player_health;
    if (e->invincibility_frames > 0) return;
    sfx(17, 0);                          /* player hurt */
    e->render_layer = 11;
    freeze_and_shake_screen(6, 10);
    if (ph) { ph->ph_anim = 2; ph->default_counter = 20; }
    e->invincibility_frames = 60;
    e->pl_stun_frames = 19;
    g_score_mult = 0;
    if (ph && decrement_counter_prop(&ph->ph_hearts))
        trigger_game_over(e);          /* out of hearts */
}
static void player_on_hurt(Entity *e)
{
    spawn_entity(28, e->x, e->y);        /* pain */
    player_get_hurt(e);
}

/* ---- class 22: magic_mirror (boss) ---- */
#define bo_expression        iv[0]
#define bo_visible           iv[1]
#define bo_really_visible    iv[2]
#define bo_wearing_top_hat   iv[3]
#define bo_is_cracked        iv[4]
#define bo_dark_color        iv[5]
#define bo_light_color       iv[6]
#define bo_tl                iv[7]   /* which timeline slot this boss ticks */
#define bo_home_x            n[0]
#define bo_home_y            n[1]
#define bo_is_reflection     n[2]    /* nonzero = green-toned mirror copy */

/* ---- class 24: magic_mirror_hand ---- */
#define hand_pose        iv[0]
#define hand_visible     iv[1]
#define hand_is_right    iv[2]
#define hand_is_reflect  iv[3]
#define hand_dir         iv[4]
#define hand_dark        iv[5]
#define hand_light       iv[6]
#define hand_bouquet     n[0]    /* nonzero = holding the bouquet */
#define hand_wand        n[1]    /* nonzero = holding a wand */

/* card 17 / coin 19 scratch (declared here so the timeline handlers can set them) */
#define card_red    iv[0]
#define coin_health iv[0]
#define coin_landed iv[1]
#define coin_tx     n[0]
#define coin_ty     n[1]

static int g_conjure_counter;   /* Lua conjure_flowers_counter */

/* -------------------------------------------------------------------
 * Boss action-sequence runner (Phase 4b).  The Lua boss is choreographed
 * with promise_sequence(step, step, ...) where each step calls a method on
 * the boss or a hand and the next step waits for it.  We model that as a flat
 * TIMELINE of steps, each tagged with an actor; instant steps (set pose/expr)
 * run in one tick, timed steps (move/wait/charge) hold for their duration.
 * This covers phase-1's sequential choreography; concurrent sequences
 * (reflections, hard mode) are a later extension.
 * ------------------------------------------------------------------- */
enum {
    OP_WAIT, OP_MOVE, OP_SET_EXPR, OP_SET_POSE, OP_SET_IDLE,
    OP_APPEAR, OP_DISAPPEAR, OP_SPAWN_CARD, OP_CHARGE_LASER,
    OP_FIRE_LASER, OP_RETURN_MOVES,
    OP_CONJURE_FLOWERS, OP_BLOOM_FLOWERS, OP_SPAWN_COIN, OP_DESPAWN_COINS,
    OP_THROW_HAT, OP_SPAWN_REFLECTION, OP_DIE_SELF,
    /* phase-change cinematics */
    OP_SHAKE, OP_SPAWN_TILE, OP_SPAWN_HEART, OP_REEL_PULSE,
    OP_BOUQUET, OP_WAND, OP_SPELL_BURST, OP_SPAWN_PREFLECT, OP_SPAWN_BREFLECT,
    OP_DON_HAT
};

static void flower_bloom(Entity *e);   /* fwd */
typedef struct {
    int op;
    Entity *actor;                 /* NULL == the boss */
    num x, y, a0, a1, a2, a3;      /* move target + optional bezier anchors */
    int has_anchor, dur, ease, rel, i0;
} Step;

/* CONCURRENT timelines: the boss, the persistent boss_reflection (phase 4),
 * and one transient hard-mode reflection each run their own sequencer.  An
 * entity's bo_tl field says which slot it ticks. */
#define TL_BOSS      0
#define TL_REFLECT   1
#define TL_TRANSIENT 2
typedef struct {
    Step steps[160];
    int len, idx, wait, active;
} Timeline;
static Timeline g_tls[3];
static Timeline *g_build = &g_tls[0];   /* timeline the em_* emitters write to */

static void tl_begin(int slot)
{
    g_build = &g_tls[slot];
    g_build->len = 0; g_build->idx = 0; g_build->wait = 0; g_build->active = 1;
}

/* hard-mode ternary, mirrors the Lua ternary_hard_mode */
#define THM(hard, easy) (g_hard_mode ? (hard) : (easy))

static Step *tl_add(int op, Entity *actor)
{
    Step *s;
    if (g_build->len >= 160) g_build->len = 159;   /* clamp; never overflow */
    s = &g_build->steps[g_build->len++];
    s->op = op; s->actor = actor;
    s->x = s->y = s->a0 = s->a1 = s->a2 = s->a3 = 0;
    s->has_anchor = s->dur = s->ease = s->rel = s->i0 = 0;
    return s;
}
static void em_wait(int f)                 { tl_add(OP_WAIT, 0)->i0 = f; }
static void em_expr(Entity *a, int e)      { tl_add(OP_SET_EXPR, a)->i0 = e; }
static void em_pose(Entity *a, int p)      { tl_add(OP_SET_POSE, a)->i0 = p; }
static void em_idle(Entity *a, int v)      { tl_add(OP_SET_IDLE, a)->i0 = v; }
static void em_op(int op, Entity *a)       { tl_add(op, a); }
static void em_move(Entity *a, num x, num y, int dur, int ease, int rel)
{
    Step *s = tl_add(OP_MOVE, a);
    s->x = x; s->y = y; s->dur = dur; s->ease = ease; s->rel = rel;
}
/* like em_move but the timeline does NOT hold for it -- lets two actors
 * (e.g. both hands pounding) move simultaneously */
static void em_move_nowait(Entity *a, num x, num y, int dur, int ease, int rel)
{
    Step *s = tl_add(OP_MOVE, a);
    s->x = x; s->y = y; s->dur = dur; s->ease = ease; s->rel = rel;
    s->i0 = 1;
}
static void em_move_a(Entity *a, num x, num y, int dur, int ease, int rel,
                      num a0, num a1, num a2, num a3)
{
    Step *s = tl_add(OP_MOVE, a);
    s->x = x; s->y = y; s->dur = dur; s->ease = ease; s->rel = rel;
    s->has_anchor = 1; s->a0 = a0; s->a1 = a1; s->a2 = a2; s->a3 = a3;
}

static void decide_next_action(Entity *boss);   /* fwd */

static void boss_apply_colors(Entity *e)
{
    p8_pal(2, e->bo_is_cracked ? 6 : 7);   /* show/hide the crack */
    if (e->bo_is_reflection <= 0) {
        /* lines-and-solids: white mirror/hat/frame, black feature lines */
        p8_pal(9, 7);       /* frame        */
        p8_pal(15, 7);      /* frame accent */
        p8_pal(4, 7);       /* handle       */
        p8_pal(13, 0);      /* face shading -> black line detail */
        p8_pal(5, 7);       /* hat body     */
        p8_pal(8, 0);       /* hat band     */
        p8_pal(1, 0);
    }
    if (e->bo_is_reflection > 0) {
        /* mirror copy: wash the body dark, keep highlights light -- reads as
         * a "ghost" boss against the solid-white real one in 1-bit */
        color_wash(e->bo_dark_color);
        p8_pal(8, e->bo_light_color);
        p8_pal(7, e->bo_light_color);
        p8_pal(6, e->bo_light_color);
        p8_pal(2, e->bo_light_color);
    }
}

static void boss_draw(Entity *e)
{
    int expression = e->bo_expression;
    num x = e->x, y = e->y;
    if (!e->bo_really_visible) return;

    boss_apply_colors(e);
    if (e->bo_visible) ent_sprite(e, 6, 12, 115, 0, 13, 30, 0);   /* mirror */

    if (e->bo_visible || (g_boss_health && g_boss_health->bh_rainbow > 0)) {
        if (g_boss_health && g_boss_health->bh_rainbow > 0) {
            color_wash(g_rainbow_color);
            if (expression > 0 && expression != 5 && g_boss_phase > 0) p8_pal(13, 5);
            expression = 8;
        }
        if (expression > 0)
            ent_sprite(e, 5, 7, 11 * expression - 11, 57, 11, 14, 0);   /* face */
    }
    p8_pal_reset();
    boss_apply_colors(e);
    if (e->bo_visible) {
        if (e->bo_wearing_top_hat) ent_sprite(e, 6, 15, 102, 0, 13, 9, 0);
        if (e->default_counter % 2 > 0)          /* laser charge preview */
            p8_line((int)x, (int)(y + 7), (int)x, 60, 7);
    }
}

/* ---- boss "methods" appended to the timeline ---- */

/* return_to_ready_position: reset poses/expression + move boss & hands home */
static void em_return_to_ready(Entity *boss, int expr)
{
    em_pose(boss->ref[0], 3);
    em_pose(boss->ref[1], 3);
    em_idle(boss, 1);
    em_expr(boss, expr);
    em_op(OP_RETURN_MOVES, boss);
    em_wait(THM(15, 25));
}

/* a hand flies to the edge at three rows, hurling a card at each */
static void em_throw_cards(Entity *hand)
{
    int is_first = hand->hand_is_right;   /* is_right != is_reflection(=0) */
    int dir = hand->hand_dir;
    int r;
    em_wait(is_first ? 0 : THM(13, 19));
    em_idle(hand, 0);
    for (r = is_first ? 0 : 1; r <= 4; r += 2) {
        em_pose(hand, 3);
        em_move_a(hand, 40 + 52 * dir, 8 * (r % 5) + 4, 18, EASE_OUT_IN, 0,
                  10 * dir, -10, 10 * dir, 10);
        em_pose(hand, 2);
        em_wait(THM(0, 12));
        em_pose(hand, 1);
        em_op(OP_SPAWN_CARD, hand);
        em_wait(10);
    }
}

/* three laser shots from random columns */
static void em_shoot_lasers(Entity *boss)
{
    int k, col = rnd_int(0, 7);
    em_idle(boss, 0);
    for (k = 0; k < 3; k++) {
        col = (col + rnd_int(2, 6)) % 8;
        em_move(boss, 10 * col + 5, -20, 15, EASE_IN, 0);
        em_wait(1);
        em_op(OP_CHARGE_LASER, boss);
        em_op(OP_FIRE_LASER, boss);
        em_expr(boss, 1);
        em_wait(5);
    }
}

/* conjure a field of flowers, then bloom them (dodge onto a safe cell) */
static void em_conjure_flowers(Entity *boss)
{
    Entity *lh = boss->ref[0], *rh = boss->ref[1];
    num hx = boss->bo_home_x, hy = boss->bo_home_y;
    em_idle(boss, 1);
    em_pose(lh, 1); em_move(lh, hx - 13, hy, 20, EASE_LINEAR, 0);   /* temples */
    em_pose(rh, 1); em_move(rh, hx + 13, hy, 20, EASE_LINEAR, 0);
    em_expr(boss, 2);
    em_op(OP_CONJURE_FLOWERS, boss);
    em_wait(THM(50, 65));
    em_op(OP_BLOOM_FLOWERS, boss);
    em_pose(lh, 5); em_pose(rh, 5); em_expr(boss, 3);
    em_wait(31);
}

/* rain coins onto the player's current cell (keep moving!) */
static void em_throw_coins(Entity *boss, int num_coins)
{
    Entity *rh = boss->ref[1];
    num hx = boss->bo_home_x, hy = boss->bo_home_y;
    int k;
    em_idle(boss, 1);
    em_pose(rh, 1); em_move(rh, hx + 13, hy, 20, EASE_LINEAR, 0);
    for (k = 0; k < num_coins; k++) {
        em_expr(boss, 7);
        em_pose(rh, 1);
        em_wait(15);
        em_op(OP_SPAWN_COIN, boss);
        em_pose(rh, 4);
        em_expr(boss, 3);
        em_wait(20);
    }
}

/* both fists slam down beside the mirror (simultaneous via no-wait moves) */
static void em_pound(Entity *boss)
{
    Entity *lh = boss->ref[0], *rh = boss->ref[1];
    num hx = boss->bo_home_x, hy = boss->bo_home_y;
    if (!lh || !rh) return;
    em_pose(lh, 2); em_pose(rh, 2);
    em_move_nowait(lh, hx - 4, hy + 20, 15, EASE_OUT, 0);
    em_move(rh, hx + 4, hy + 20, 15, EASE_OUT, 0);
    { Step *s = tl_add(OP_SHAKE, boss); s->i0 = 2; }
    em_wait(1);
}

/* the boss reels from the phase hit: hurt face, drops a healing heart,
 * then gets knocked about `pulses` times */
static void em_reel(Entity *boss, int pulses)
{
    int k;
    em_expr(boss, 8);
    em_idle(boss, 0);
    em_op(OP_SPAWN_HEART, boss);
    for (k = 0; k < pulses; k++) {
        em_op(OP_REEL_PULSE, boss);
        em_wait(5);
    }
}

/* the wand-conjuring spell: wave, poof-in a wand, flourish, cast.
 * what==0 casts the player reflection (phase 3); what==1 the upgraded
 * two-wand version that summons the boss's own mirror (phase 4). */
static void em_cast_reflection(Entity *boss, int what)
{
    Entity *lh = boss->ref[0], *rh = boss->ref[1];
    if (!lh || !rh) return;
    em_idle(boss, 0);
    em_move(lh, 23, 14, 20, EASE_IN, 1);          /* lh drifts low */
    em_pose(rh, 1);
    /* two circular waves of the right hand */
    em_move_a(rh, 0, 0, 40, EASE_LINEAR, 1, 18, 6, -18, 6);
    em_move_a(rh, 0, 0, 40, EASE_LINEAR, 1, 18, 6, -18, 6);
    em_expr(boss, 1);
    { Step *s = tl_add(OP_WAND, lh); s->i0 = 1; }
    if (what == 1) { Step *s = tl_add(OP_WAND, rh); s->i0 = 1; }
    em_wait(30);
    /* flourish: raise the wand and let the spell rip */
    em_move_a(lh, 40 + 20 * -1, -30, 12, EASE_OUT, 0, -20, 20, 0, 20);
    em_pose(lh, 6);
    em_op(OP_SPELL_BURST, lh);
    if (what == 1) {
        em_move_a(rh, 40 + 20 * 1, -30, 12, EASE_OUT, 0, -20, 20, 0, 20);
        em_pose(rh, 6);
        em_op(OP_SPELL_BURST, rh);
    }
    em_expr(boss, 3);
    em_wait(5);
    em_op(what == 1 ? OP_SPAWN_BREFLECT : OP_SPAWN_PREFLECT, boss);
    em_wait(30);
    { Step *s = tl_add(OP_WAND, lh); s->i0 = 0; }
    if (what == 1) { Step *s = tl_add(OP_WAND, rh); s->i0 = 0; }
}

/* a barrage of falling top hats to weave through */
static void em_throw_hats(Entity *boss)
{
    int k;
    em_idle(boss, 1);
    em_expr(boss, 1);
    for (k = 0; k < 3; k++) { em_op(OP_THROW_HAT, boss); em_wait(24); }
    em_wait(20);
}

/* attack rotation, per phase; loops via the runner when it ends.
 * Builds into the timeline slot named by boss->bo_tl (concurrency: the real
 * boss and the phase-4 reflection each run their own). */
static void decide_next_action(Entity *boss)
{
    Entity *lh = boss->ref[0], *rh = boss->ref[1];
    tl_begin(boss->bo_tl);
    /* the phase-4 reflection runs the same rotation, staggered so the pair
     * don't attack in perfect unison */
    if (boss->bo_is_reflection > 0) em_wait(45);

    if (g_boss_phase >= 2) {
        /* phases 2-3: flowers + cards + lasers + coins (+ hats at 3) */
        em_return_to_ready(boss, 1);
        em_wait(15);
        em_conjure_flowers(boss);
        em_return_to_ready(boss, 1);
        em_throw_cards(lh);
        em_return_to_ready(boss, 1);
        em_wait(20);
        if (g_hard_mode)                     /* hard: a reflection joins the lasers */
            { Step *s = tl_add(OP_SPAWN_REFLECTION, boss); s->i0 = 0; }
        em_shoot_lasers(boss);
        em_return_to_ready(boss, 1);
        em_op(OP_DESPAWN_COINS, boss);
        if (g_hard_mode)                     /* hard: and another throws a hat */
            { Step *s = tl_add(OP_SPAWN_REFLECTION, boss); s->i0 = 1; }
        em_throw_coins(boss, THM(6, 4));
        if (g_boss_phase >= 3) {
            em_return_to_ready(boss, 1);
            em_wait(10);
            em_throw_hats(boss);
        }
        em_return_to_ready(boss, 1);
    } else {
        em_return_to_ready(boss, 1);
        em_wait(15);
        em_throw_cards(lh);
        em_return_to_ready(boss, 1);
        em_wait(10);
        em_throw_cards(rh);
        em_return_to_ready(boss, 1);
        em_wait(THM(10, 25));
        if (g_hard_mode)
            { Step *s = tl_add(OP_SPAWN_REFLECTION, boss); s->i0 = 0; }
        em_shoot_lasers(boss);
    }
}

static Entity *spawn_boss_reflection(int transient, num x_off);   /* fwd */

/* execute the entity's timeline: batch instant steps, hold on timed ones */
static void tl_tick(Entity *boss)
{
    Timeline *tl = &g_tls[boss->bo_tl];
    if (!tl->active) return;
    if (tl->wait > 0) { tl->wait--; return; }
    while (tl->idx < tl->len) {
        Step *s = &tl->steps[tl->idx++];
        Entity *a = s->actor ? s->actor : boss;
        int wait = 0;
        switch (s->op) {
        case OP_WAIT: wait = s->i0; break;
        case OP_MOVE: {
            num an[4]; const num *ap = 0;
            if (s->has_anchor) { an[0]=s->a0; an[1]=s->a1; an[2]=s->a2; an[3]=s->a3; ap=an; }
            entity_move(a, s->x, s->y, s->dur, s->ease, ap, s->rel);
            if (!s->i0) wait = s->dur - 1;   /* i0 = no-wait (parallel move) */
        } break;
        case OP_SET_EXPR: a->bo_expression = s->i0; break;
        case OP_SET_POSE: a->hand_pose = s->i0; break;
        case OP_SET_IDLE:
            if (a == boss) {
                boss->is_idle = s->i0;
                if (boss->ref[0]) boss->ref[0]->is_idle = s->i0;
                if (boss->ref[1]) boss->ref[1]->is_idle = s->i0;
            } else a->is_idle = s->i0;
            break;
        case OP_APPEAR:    a->hand_visible = 1; spawn_entity(27, a->x, a->y); break;
        case OP_DISAPPEAR: a->hand_visible = 0; spawn_entity(27, a->x, a->y); break;
        case OP_SPAWN_CARD: {
            int dir = a->hand_dir;
            Entity *c = spawn_entity(17, a->x - 7 * dir, a->y);
            if (c) { c->vx = -1.5f * dir; c->iv[0] = (rnd_f(1) < 0.5f); }  /* iv[0]=card_is_red */
        } break;
        case OP_CHARGE_LASER: boss->default_counter = 31; wait = 31; break;
        case OP_FIRE_LASER: {
            Entity *l = spawn_entity(25, boss->x, boss->y);
            if (l) l->ref[0] = boss;
            freeze_and_shake_screen(0, 4);
            boss->bo_expression = 0;
            wait = 16;
        } break;
        case OP_RETURN_MOVES: {
            num hx = boss->bo_home_x, hy = boss->bo_home_y;
            Entity *lh = boss->ref[0], *rh = boss->ref[1];
            entity_move(boss, hx, hy, 15, EASE_IN, 0, 0);
            if (lh) { entity_move(lh, hx - 18, hy + 5, 15, EASE_IN, 0, 0); lh->hand_visible = 1; }
            if (rh) { entity_move(rh, hx + 18, hy + 5, 15, EASE_IN, 0, 0); rh->hand_visible = 1; }
        } break;
        case OP_CONJURE_FLOWERS: {
            /* pick flower cells via the Lua pseudo-random progression, leaving
             * gaps (safe cells) for the player */
            static const int seq[8] = { 1, 2, 3, 5, 7, 9, 10, 11 };
            int i, n = 0, base;
            g_conjure_counter = 1 + (g_conjure_counter + rnd_int(0, 2)) % 8;
            base = seq[g_conjure_counter - 1];
            for (i = 0; i < 40; i++) {
                if (i == n) {
                    int m2 = ((n + base) % 2) > 0 ? 1 : 0;
                    int m3 = ((n + base) % 3) > 0 ? 1 : 0;
                    int m5 = ((n + base) % 5) > 0 ? 1 : 0;
                    int step = m2 + m3 + m5; if (step < 1) step = 1; if (step > 3) step = 3;
                    n += step;
                    { Entity *f = spawn_entity(18, (i % 8) * 10 + 5, 8 * (i / 8) + 4);
                      if (f) f->ref[0] = boss; }
                }
            }
        } break;
        case OP_BLOOM_FLOWERS: {
            int i;
            for (i = 0; i < g_entity_count; i++)
                if (g_entities[i] && g_entities[i]->class_id == 18)
                    flower_bloom(g_entities[i]);
        } break;
        case OP_SPAWN_COIN:
            if (g_player) {
                int tcol = entity_col(g_player), trow = entity_row(g_player);
                num tx = 10 * tcol - 5, ty = 8 * trow - 4;
                Entity *coin = spawn_entity(19, boss->x + 13, boss->y - 6);
                if (coin) {
                    num an[4]; an[0]=20; an[1]=-30; an[2]=10; an[3]=-60;
                    coin->coin_tx = tx; coin->coin_ty = ty; coin->coin_health = 3;
                    coin->frames_to_death = 220;         /* safety despawn */
                    entity_move(coin, tx + 2, ty, 25, EASE_OUT, an, 0);
                }
            }
            break;
        case OP_DESPAWN_COINS: {
            int i;
            for (i = 0; i < g_entity_count; i++)
                if (g_entities[i] && g_entities[i]->class_id == 19)
                    entity_die(g_entities[i]);
            wait = 10;
        } break;
        case OP_THROW_HAT: {
            /* a spinning top hat falls from above, sweeping side to side */
            Entity *h = spawn_entity(2, boss->x, -32);
            if (h) { h->ref[0] = boss; h->vy = 1; }
            sfx(26, 2);
        } break;
        case OP_SPAWN_REFLECTION: {
            /* hard mode: a short-lived mirror copy performs one attack.
             * Builds the transient slot's timeline, then restores ours. */
            Timeline *save = g_build;
            Entity *r;
            if (g_tls[TL_TRANSIENT].active) break;    /* one at a time */
            r = spawn_boss_reflection(1, (boss->x < 40) ? 25 : -25);
            if (!r) break;
            tl_begin(TL_TRANSIENT);
            em_wait(10);
            if (s->i0 == 1) em_throw_hats(r);
            else {
                em_op(OP_CHARGE_LASER, r);
                em_op(OP_FIRE_LASER, r);
                em_wait(10);
            }
            em_op(OP_DIE_SELF, r);
            g_build = save;
        } break;
        case OP_DIE_SELF:
            spawn_entity(27, a->x, a->y);            /* poof out */
            entity_die(a);
            tl->active = 0;
            return;
        case OP_SHAKE:
            freeze_and_shake_screen(0, s->i0);
            break;
        case OP_SPAWN_TILE:
            spawn_magic_tile(s->i0 > 0 ? s->i0 : 100);
            break;
        case OP_SPAWN_HEART:
            spawn_entity(26, 10 * rnd_int(3, 6) - 5, 4);
            break;
        case OP_REEL_PULSE: {
            /* boss + both hands get knocked about with poofs */
            Entity *trio[3];
            int k;
            trio[0] = boss; trio[1] = boss->ref[0]; trio[2] = boss->ref[1];
            freeze_and_shake_screen(0, 2);
            for (k = 0; k < 3; k++) {
                Entity *t = trio[k];
                if (!t) continue;
                t->x = fmid(10, 70, t->x);
                t->y = fmid(-40, -20, t->y);
                spawn_entity(27, t->x + rnd_int(-10, 10), t->y + rnd_int(-10, 10));
                entity_move(t, rnd_int(-7, 7), rnd_int(-7, 7), 6, EASE_OUT, 0, 1);
            }
            sfx(12, 2);
        } break;
        case OP_BOUQUET: a->hand_bouquet = s->i0; sfx(s->i0 ? 16 : 28, 3); break;
        case OP_WAND:
            a->hand_wand = s->i0;
            if (s->i0) { spawn_entity(27, a->x + 10 * a->hand_dir, a->y); sfx(11, 2); }
            break;
        case OP_SPELL_BURST:
            sfx(23, 1);
            spawn_particle_burst(a, 20, 20, 3, 10);
            freeze_and_shake_screen(0, 20);
            break;
        case OP_SPAWN_PREFLECT:
            if (!g_player_reflection)
                g_player_reflection = spawn_entity(16, 0, 0);
            break;
        case OP_SPAWN_BREFLECT:
            if (!g_hard_mode && !g_boss_reflection)
                g_boss_reflection = spawn_boss_reflection(0, 0);
            break;
        case OP_DON_HAT:
            boss->bo_wearing_top_hat = 1;
            spawn_entity(27, boss->x, boss->y - 10);
            sfx(11, 2);
            break;
        }
        if (wait > 0) { tl->wait = wait; return; }
    }
    decide_next_action(boss);   /* loop */
}

static void boss_update(Entity *e)
{
    calc_idle_mult(e, e->frames_alive, 2);
    if (g_boss_health && g_boss_health->bh_rainbow > 12)
        e->draw_offset_x += (g_scene_frame % 2) * 2 - 1;
    if (g_boss_phase >= 1) tl_tick(e);
}

static void boss_init(Entity *e)
{
    Entity *lh, *rh;
    num hy;
    e->x = 40; e->y = -28;
    e->bo_home_x = 40; e->bo_home_y = -28;
    e->bo_expression = 4;
    e->bo_dark_color = 14; e->bo_light_color = 15;
    e->bo_really_visible = 1;
    e->is_idle = 1;
    hy = e->y + 5;
    lh = spawn_entity(24, e->x - 18, hy);
    rh = spawn_entity(24, e->x + 18, hy);
    if (lh) { lh->ref[0] = e; lh->hand_dir = -1; lh->hand_is_right = 0;
              lh->hand_dark = e->bo_dark_color; lh->hand_light = e->bo_light_color; }
    if (rh) { rh->ref[0] = e; rh->hand_dir = 1;  rh->hand_is_right = 1;
              rh->hand_dark = e->bo_dark_color; rh->hand_light = e->bo_light_color; }
    e->ref[0] = lh; e->ref[1] = rh;
}

static void boss_on_death(Entity *e)
{
    if (e->ref[0]) entity_die(e->ref[0]);
    if (e->ref[1]) entity_die(e->ref[1]);
}

/* despawn everything the boss has in flight (cards/lasers/flowers/coins/hats)
 * so a cinematic starts on a clean stage */
static void boss_cancel_attacks(void)
{
    int i;
    for (i = 0; i < g_entity_count; i++) {
        Entity *e = g_entities[i];
        if (e && e->is_boss_generated && e != g_boss && e != g_boss_reflection)
            entity_die(e);
    }
}

/* the full intro cinematic: hands appear one by one, finger-wag, grab the
 * handle, the face wakes up, taps of the mirror, poof -- a top hat!
 * When the timeline runs out the runner falls through to decide_next_action,
 * so the fight begins the moment the theater ends. */
static void boss_intro(Entity *e)
{
    Entity *lh = e->ref[0], *rh = e->ref[1];
    int k;
    e->bo_visible = 1;
    g_boss_phase = 1;
    if (g_player_health) g_player_health->ph_visible = 1;
    platform_music(MUS_BOSS);       /* Lua: music(8/25) boss intro/fight */

    tl_begin(e->bo_tl);
    em_expr(e, 0);                  /* mirror wakes blank */
    em_wait(20);
    if (lh) {
        em_op(OP_APPEAR, lh);
        em_wait(20);
        for (k = 0; k < 2; k++) {   /* wag a scolding finger */
            em_pose(lh, 5); em_wait(3);
            em_pose(lh, 4); em_wait(3);
        }
        em_wait(20);
    }
    if (rh) {
        em_op(OP_APPEAR, rh);
        em_wait(10);
        /* grab the mirror's handle */
        em_move_a(rh, -16, 8, 10, EASE_OUT, 1, 10, 0, 10, 5);
        em_pose(rh, 2);
    }
    /* the face cycles awake */
    em_expr(e, 5); em_wait(33);
    em_expr(e, 6); em_wait(28);
    em_expr(e, 5); em_wait(34);
    em_expr(e, 1); em_wait(5);
    if (lh) {                       /* tap tap */
        em_pose(lh, 5);
        em_move_a(lh, e->x - 5, e->y - 3, 10, EASE_OUT, 0, 0, -10, -10, -2);
        em_pose(lh, 4);
        em_move_a(lh, e->bo_home_x - 18, e->bo_home_y + 5, 10, EASE_IN, 0, -10, -2, 0, -10);
    }
    em_wait(10);
    em_op(OP_DON_HAT, e);           /* poof! a top hat appears */
    em_wait(35);
    { Step *s = tl_add(OP_SPAWN_TILE, e); s->i0 = 100; }
    /* timeline ends -> decide_next_action -> the fight is on */
}

/* the between-phases cinematic: reel + per-phase theater, ending with a fresh
 * tile; the bar drains via spawn_magic_tile's >=60 drain.  Falls through to
 * the new phase's attack rotation when the timeline empties. */
static void boss_phase_cinematic(Entity *boss)
{
    Entity *lh = boss->ref[0], *rh = boss->ref[1];
    int k;
    boss_cancel_attacks();
    tl_begin(boss->bo_tl);
    em_op(OP_RETURN_MOVES, boss);   /* hands home first */
    em_wait(16);
    em_reel(boss, 10);
    em_wait(10);
    em_expr(boss, 5);
    em_wait(20);

    if (g_boss_phase == 2 && lh && rh) {
        /* pound fists, then stop to smell the flowers */
        em_return_to_ready(boss, 1);
        for (k = 0; k < 3; k++) em_pound(boss);
        em_expr(boss, 1);
        { Step *s = tl_add(OP_BOUQUET, lh); s->i0 = 1; }
        em_pose(rh, 3);
        em_move(boss, 20, -10, 15, EASE_IN, 1);
        em_wait(35);
        em_move(lh, 2, -9, 20, EASE_IN, 1);       /* raise the bouquet */
        em_expr(boss, 3);                          /* sniff */
        em_wait(30);
        em_expr(boss, 1);
        em_wait(15);
        { Step *s = tl_add(OP_BOUQUET, lh); s->i0 = 0; }
        em_move(rh, 0, 7, 20, EASE_OUT_IN, 1);
        em_wait(15);
    } else if (g_boss_phase == 3) {
        em_return_to_ready(boss, 2);
        em_cast_reflection(boss, 0);               /* conjure YOUR mirror */
        em_return_to_ready(boss, 1);
        em_wait(30);
    } else if (g_boss_phase == 4 && !g_hard_mode) {
        em_return_to_ready(boss, 2);
        em_cast_reflection(boss, 1);               /* conjure ITS mirror */
        em_return_to_ready(boss, 1);
        em_wait(30);
    }
    { Step *s = tl_add(OP_SPAWN_TILE, boss); s->i0 = 100; }
}

/* class 23: mirror copy of the boss.  transient=1 -> hard-mode one-attack
 * flicker beside the real boss; transient=0 -> the persistent phase-4 partner
 * (real boss shifts right, reflection takes the left home position). */
static void reflection_init(Entity *e)
{
    boss_init(e);                     /* spawns its own hands */
    e->bo_is_reflection = 1;
    e->bo_visible = 1;
    e->bo_wearing_top_hat = 1;
    e->bo_expression = 1;
    e->bo_dark_color = 3;
    e->bo_light_color = 11;
    if (e->ref[0]) { e->ref[0]->hand_is_reflect = 1; e->ref[0]->hand_visible = 1;
                     e->ref[0]->hand_dark = 3; e->ref[0]->hand_light = 11; }
    if (e->ref[1]) { e->ref[1]->hand_is_reflect = 1; e->ref[1]->hand_visible = 1;
                     e->ref[1]->hand_dark = 3; e->ref[1]->hand_light = 11; }
}

static Entity *spawn_boss_reflection(int transient, num x_off)
{
    Entity *r = spawn_entity(23, 0, 0);
    if (!r) return 0;
    if (transient) {
        num bx = g_boss ? g_boss->x : 40;
        num by = g_boss ? g_boss->y : -28;
        r->bo_tl = TL_TRANSIENT;
        r->x = bx + x_off; r->y = by;
        r->bo_home_x = r->x; r->bo_home_y = -28;
    } else {
        r->bo_tl = TL_REFLECT;
        r->bo_home_x = 20; r->x = 20; r->y = -28;
        if (g_boss) g_boss->bo_home_x = 60;   /* the pair split the stage */
        decide_next_action(r);
    }
    if (r->ref[0]) { r->ref[0]->x = r->x - 18; r->ref[0]->y = r->y + 5; }
    if (r->ref[1]) { r->ref[1]->x = r->x + 18; r->ref[1]->y = r->y + 5; }
    spawn_entity(27, r->x, r->y);     /* poof in */
    sfx(30, 2);
    return r;
}

static void boss_health_milestone(int health)
{
    if (g_boss_phase == 0) {
        if (health == 25 && !g_boss)      g_boss = spawn_entity(22, 0, 0);
        else if (health == 37 && g_boss)  g_boss->bo_visible = 1;
        else if (health == 60 && g_boss)  boss_intro(g_boss);
    } else if (health >= 60 && g_boss_health) {
        /* bar filled during the fight -> advance via the phase cinematic
         * (reel + theater + fresh tile; the bar DRAINS when that tile spawns,
         * and the new rotation starts when the timeline falls through).
         * Reflection arrivals happen inside the cinematic's cast steps. */
        if (g_boss_phase < 4 && g_boss) {
            g_boss_phase++;
            freeze_and_shake_screen(4, 8);
            boss_phase_cinematic(g_boss);
            if (g_boss_reflection) decide_next_action(g_boss_reflection);
        } else {
            trigger_victory();                 /* phase 4 cleared -> you win! */
        }
    }
}

/* ---- class 24: magic_mirror_hand ---- */
static void hand_init(Entity *e) { e->hand_pose = 3; e->hand_dir = -1; }
static void hand_draw(Entity *e)
{
    int ir;
    if (!e->hand_visible) return;
    ir = e->hand_is_right;
    if (e->hand_bouquet > 0)           /* bouquet sits behind the hand */
        ent_sprite(e, 1, 12, 110, 71, 9, 16, 0);
    if (e->hand_is_reflect) {          /* ghost tint to match the mirror boss */
        color_wash(e->hand_dark);
        p8_pal(7, e->hand_light);
        p8_pal(6, e->hand_light);
    } else {                           /* solid white glove, black line detail */
        p8_pal(6, 7);
        p8_pal(13, 0);
    }
    ent_sprite(e, ir ? 7 : 4, 8, 12 * e->hand_pose - 12, 46, 12, 11, ir);
    if (e->hand_wand > 0) {            /* wand held per pose */
        if (e->hand_pose == 1)
            ent_sprite(e, ir ? 10 : -4, 8, 91, 57, 7, 13, ir);
        else
            ent_sprite(e, ir ? 3 : 2, 16, 98, 57, 7, 13, ir);
    }
}
static void hand_update(Entity *e)
{
    /* hands are positioned by the boss timeline's move steps (4b), no longer
     * glued to the mirror -- so they can fly out to throw cards */
    e->render_layer = e->hand_is_reflect ? 6 : (e->hand_is_right ? 9 : 8);
    calc_idle_mult(e, (g_boss ? g_boss->frames_alive : 0) + (e->hand_is_right ? 9 : 4), 4);
    entity_apply_velocity(e);
    e->skip_default_move = 1;
}

/* ---- class 25: mirror_laser (vertical beam, hits the boss's column) ----
 * Drawn as line art (edge lines + solid white core) instead of the original's
 * stretched gradient sprite -- crisp on 1-bit, per owner feedback. */
static void laser_draw(Entity *e)
{
    int x = (int)e->x, y0 = (int)e->y + 5, y1 = (int)e->y + 104;
    p8_line(x - 4, y0, x - 4, y1, 7);              /* edges */
    p8_line(x + 6, y0, x + 6, y1, 7);
    p8_rectfill(x - 1, y0, x + 3, y1, 7);          /* solid core */
}
static void laser_update(Entity *e) { if (e->ref[0]) e->x = e->ref[0]->x; }
static int  laser_is_hitting(Entity *self, Entity *o) { return entity_col(self) == entity_col(o); }

/* ---- class 17: playing_card (spinning; hits the player) ---- */
#define card_is_red iv[0]
static void card_draw(Entity *e)
{
    int sprite = (e->frames_alive / 4) % 4;
    if (e->vx < 0) sprite = (6 - sprite) % 4;
    if (e->card_is_red) {
        /* red suit in 1-bit = inverted card: black face, white border */
        p8_pal(5, 7); p8_pal(6, 0); p8_pal(7, 0);
    } else {
        p8_pal(5, 0); p8_pal(6, 7);       /* white card, black border */
    }
    ent_sprite(e, 5, 7, 10 * sprite + 77, 21, 10, 10, 0);
}

/* ---- class 26: heart pickup (dropped when the boss reels; heals one) ---- */
static void heart_init(Entity *e) { e->hurtbox_channel = 2; }   /* pickup */
static void heart_draw(Entity *e)
{
    int f2 = e->frames_to_death;
    if (f2 > 30 || f2 % 4 > 1)          /* blink as it expires */
        draw_heart((int)e->x - 3, (int)e->y - 3, kHeartFull, 7);
}
static void heart_on_hurt(Entity *e)
{
    Entity *ph = g_player_health;
    sfx(18, 0);                          /* player heal */
    if (ph && ph->ph_hearts < 4) {
        ph->ph_hearts++;
        ph->ph_anim = 1;
        ph->default_counter = 10;
    }
    spawn_particle_burst(e, 0, 6, 8, 4);
    entity_die(e);
}

/* ---- class 18: flower_patch (blooms dangerously for a few frames) ---- */
static void flower_draw(Entity *e)
{
    int sx = (e->default_counter > 0) ? 119 : (e->frames_to_death > 0 ? 110 : 101);
    p8_pal(8, 7);       /* blooms solid white (were dark stipple) */
    p8_pal(3, 0);       /* leaves -> black lines */
    ent_sprite(e, 4, 4, sx, 71, 9, 8, 0);
}
static void flower_update(Entity *e)
{
    if (e->counter_reached_zero) e->hitbox_channel = 0;   /* danger window ends */
}
static void flower_bloom(Entity *e)
{
    int i;
    e->frames_to_death = 15;
    e->default_counter = 4;      /* hurts for these 4 frames */
    e->hitbox_channel = 1;
    for (i = 0; i < 2; i++) {
        Entity *p = spawn_entity(21, e->x, e->y - 2);
        if (p) {
            p->vx = i - 1.5f;
            p->vy = -1 - rnd_f(1);
            p->pa_friction = 0.9f;
            p->pa_gravity = 0.06f;
            p->pa_color = 8;
            p->frames_to_death = 10 + (int)rnd_f(7);
            particle_update(p);
            entity_apply_velocity(p);
        }
    }
}

/* ---- class 19: coin (arcs to the player's cell, lands as a hazard/obstacle) ---- */
static void coin_draw(Entity *e)
{
    int f = e->frames_alive, sprite, r = f / 7;
    if (r > 4) r = 4;
    /* landing telegraph: a growing white RING (line art; the old filled
     * dark-color shadow dithered to mud on the black stage) */
    if (!e->coin_landed)
        p8_circ((int)e->coin_tx, (int)e->coin_ty - 1, r, 7);
    if (f >= 26) sprite = (e->coin_health < 3) ? 5 : 4;
    else sprite = (f > 10 ? 2 : 0) + (f / 3) % 2;
    p8_pal(6, 7);       /* coin body solid white  */
    p8_pal(13, 0);      /* etching -> black lines */
    p8_pal(5, 0);
    ent_sprite(e, 4, 5, 9 * sprite, 37, 9, 9, 0);
}
static void coin_update(Entity *e)
{
    if (!e->coin_landed && e->frames_alive >= 27) {
        e->coin_landed = 1;
        e->occupies_tile = 1;
        e->hitbox_channel = 1;                 /* hurts if player is still here */
        freeze_and_shake_screen(2, 2);
    }
    if (e->frames_alive == 33) e->hitbox_channel = 0;   /* now just an obstacle */
}
static void coin_get_bumped(Entity *e)          /* player smashed it */
{
    if (decrement_counter_prop(&e->coin_health)) entity_die(e);
}
static void coin_on_death(Entity *e)
{
    spawn_particle_burst(e, 0, 6, 6, 4);
    sfx(21, 1);                                 /* coin death */
}

/* ---- class 2: spinning_top_hat (falls, sweeping side to side; hazard) ---- */
static void spinning_hat_draw(Entity *e)
{
    /* solid white hat with black band/lines (was a parent-tinted wash that
     * dithered to mud) */
    p8_pal(5, 7);
    p8_pal(6, 7);
    p8_pal(1, 0);
    p8_pal(8, 0);
    ent_sprite(e, 7, 10, 100, 9, 15, 12, 0);
}
static void spinning_hat_update(Entity *e)
{
    e->x += 2 * p8_cos((num)e->frames_alive / 50);   /* engine adds vy each frame */
}

/* ===================================================================
 * PHASE 4b: winnable loop -- victory / game-over / credit screens
 * =================================================================== */

/* print "<score>00" (Lua appends the two zeros literally) */
static void print_score_str(int score, int x, int y, int c)
{
    char b[12], r[12]; int n = 0, i, v = score;
    do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v > 0 && n < 8);
    for (i = 0; i < n; i++) r[i] = b[n - 1 - i];
    r[n++] = '0'; r[n++] = '0'; r[n] = 0;
    p8_print(r, x, y, c);
}
static void print_timer_str(int seconds, int x, int y, int c)
{
    char r[8]; int m = seconds / 60, s = seconds % 60, n = 0;
    if (m >= 10) r[n++] = (char)('0' + (m / 10) % 10);
    r[n++] = (char)('0' + m % 10);
    r[n++] = ':';
    r[n++] = (char)('0' + (s / 10) % 10);
    r[n++] = (char)('0' + s % 10);
    r[n] = 0;
    p8_print(r, x, y, c);
}

/* remove all combat entities; keep the framing (curtains + any screens) */
void clear_combat_entities(void)
{
    int i;
    for (i = 0; i < g_entity_count; i++) {
        Entity *e = g_entities[i];
        int c;
        if (!e) continue;
        c = e->class_id;
        if (c == 4 || c == 6 || c == 7 || c == 8 || c == 9) continue;
        entity_die(e);
    }
    g_boss = 0; g_boss_health = 0; g_player = 0;
    g_player_health = 0; g_player_figment = 0;
    g_player_reflection = 0; g_boss_reflection = 0;
    /* stop all sequencers so a reused slot can't tick stale steps */
    for (i = 0; i < 3; i++) g_tls[i].active = 0;
}

/* spawn the boss already fighting at the given phase (retry/continue) */
void boss_spawn_at_phase(int phase)
{
    g_boss = spawn_entity(22, 0, 0);
    if (!g_boss) return;
    g_boss->bo_visible = 1;
    g_boss->bo_wearing_top_hat = (phase > 1);
    g_boss_phase = phase;
    if (g_player_health) g_player_health->ph_visible = 1;
    if (g_boss_health)   g_boss_health->bh_health = 0;
    platform_music(MUS_BOSS);
    /* retries into later phases restore that phase's reflections too */
    if (phase >= 3 && !g_player_reflection)
        g_player_reflection = spawn_entity(16, 0, 0);
    if (phase >= 4 && !g_hard_mode && !g_boss_reflection)
        g_boss_reflection = spawn_boss_reflection(0, 0);
    decide_next_action(g_boss);
    if (g_boss_reflection) decide_next_action(g_boss_reflection);
    spawn_magic_tile(90);
}

/* ---- triggers ---- */
static void trigger_game_over(Entity *e)
{
    platform_music(MUS_GAMEOVER);  /* Lua: music(-1) then music(35) */
    g_is_paused = 1;
    if (g_player_health) g_player_health->render_layer = 16;
    g_player_figment = spawn_entity(10, e->x + 23, e->y + 65);   /* ghost, screen-space */
    spawn_entity(9, 0, 0);                              /* game_over_screen */
    if (g_curtains) curtains_set_anim(g_curtains, 0);   /* close */
    entity_die(e);
    g_player = 0;
}
static void trigger_victory(void)
{
    int best, bt;
    platform_music(MUS_VICTORY);   /* Lua: music(47) victory riff */
    g_score += (380 - g_timer_seconds > 0) ? 380 - g_timer_seconds : 0;   /* time bonus */
    best = g_dget(g_score_data_index);
    if (g_score > best) g_dset(g_score_data_index, g_score);
    bt = g_dget(g_time_data_index);
    if (g_timer_seconds < bt || bt == 0) g_dset(g_time_data_index, g_timer_seconds);

    g_is_paused = 1;
    if (g_boss) entity_die(g_boss);
    g_boss = 0;
    if (g_curtains) curtains_set_anim(g_curtains, 0);
    spawn_entity(8, 0, 0);                              /* victory_screen */
}

/* ---- class 8: victory_screen ---- */
static void victory_init(Entity *e) { e->x = 63; e->scr_frames_until_active = 215; }
static void victory_draw(Entity *e)
{
    int f = e->frames_alive, x = (int)e->x;
    ent_sprite(e, 39, -15, 48, 95, 79, 25, 0);          /* "congratulations" */
    if (f >= 25) print_centered(g_hard_mode ? "you really did it!!"
                                            : "you did it!", x, 51, 15);
    if (f >= 135) {
        p8_print("score", x - 42, 71, 7);
        print_score_str(g_score, x + 12, 71, 6);
        print_timer_str(g_timer_seconds, x + 44, 71, 6);
    }
    if (f >= 170) {
        p8_print("best", x - 42, 79, 7);
        print_score_str(g_dget(g_score_data_index), x + 12, 79, 6);
        print_timer_str(g_dget(g_time_data_index), x + 44, 79, 6);
    }
    screen_draw_prompt(e, "continue");
}
static void victory_update(Entity *e) { screen_check_activation(e); }
static void victory_on_activated(Entity *self)
{
    (void)self;
    spawn_entity(7, 0, 0);                              /* credit_screen */
}

/* ---- class 7: credit_screen ---- */
static void credit_init(Entity *e) { e->x = 63; e->scr_frames_until_active = 130; }
static void credit_draw(Entity *e)
{
    int x = (int)e->x;
    print_centered("thank you for playing!", x, 26, g_rainbow_color);
    print_centered("created with love by bridgs", x, 66, 6);
    print_centered("aylanonsen.se", x, 75, 12);
    screen_draw_prompt(e, "continue");
}
static void credit_update(Entity *e) { screen_check_activation(e); }

/* ---- class 9: game_over_screen ---- */
static void gameover_init(Entity *e) { e->x = 63; e->scr_frames_until_active = 220; }
static void gameover_draw(Entity *e)
{
    if (screen_draw_prompt(e, "retry")) {
        p8_pal(13, 5);
        print_centered("or    to return to menu", 63, 108, 13);
        p8_spr(190, 28, 107, 1, 1, 1, 0);              /* flipped glyph (left) */
    }
}
static void gameover_update(Entity *e)
{
    screen_check_activation(e);                         /* RIGHT = retry */
    if (e->scr_is_active && p8_btnp(0)) {              /* LEFT = menu */
        e->scr_is_active = 0;
        slide(e, -1);
        e->frames_to_death = 130;
        clear_combat_entities();
        show_title_screen(-1);
    }
}
static void gameover_continue(Entity *self)
{
    (void)self;
    clear_combat_entities();
    g_score = (g_boss_phase <= 1) ? 40 : 0;
    if (g_boss_phase <= 1) g_timer_seconds = 0;
    start_game(g_boss_phase);                           /* retry current phase */
}

void entities_debug_spawn_boss_fight(void)
{
    if (!g_boss) {
        g_boss = spawn_entity(22, 0, 0);
        if (g_boss) { g_boss->bo_visible = 1; boss_intro(g_boss); }
    }
}

/* host-test/debug: jump the fight to a given boss phase */
void entities_debug_set_phase(int phase)
{
    entities_debug_spawn_boss_fight();
    g_boss_phase = phase;
    if (phase >= 3 && !g_player_reflection)
        g_player_reflection = spawn_entity(16, 0, 0);
    if (phase >= 4 && !g_hard_mode && !g_boss_reflection)
        g_boss_reflection = spawn_boss_reflection(0, 0);
    if (g_boss) decide_next_action(g_boss);
    if (g_boss_reflection) decide_next_action(g_boss_reflection);
}
void entities_debug_hard(void) { g_hard_mode = 1; }
void entities_debug_win(void)  { entities_debug_spawn_boss_fight(); trigger_victory(); }
void entities_debug_lose(void)
{
    entities_debug_spawn_boss_fight();
    if (g_player) trigger_game_over(g_player);
}

/* ===================================================================
 * registry
 * =================================================================== */
void register_entity_classes(void)
{
    int i;
    for (i = 0; i < 30; i++) {
        g_classes[i].draw = 0; g_classes[i].update = 0; g_classes[i].init = 0;
        g_classes[i].on_hurt = 0; g_classes[i].on_death = 0; g_classes[i].is_hitting = 0;
    }
    g_classes[4].draw = curtains_draw;   g_classes[4].update = curtains_update;

    g_classes[6].draw = title_draw;      g_classes[6].update = title_update;
    g_classes[6].init = title_init;

    g_classes[7].draw = credit_draw;   g_classes[7].update = credit_update;
    g_classes[7].init = credit_init;
    g_classes[8].draw = victory_draw;  g_classes[8].update = victory_update;
    g_classes[8].init = victory_init;
    g_classes[9].draw = gameover_draw; g_classes[9].update = gameover_update;
    g_classes[9].init = gameover_init;

    g_classes[10].draw = figment_draw;

    g_classes[11].draw = player_draw;    g_classes[11].update = player_update;
    g_classes[11].init = player_init;    g_classes[11].on_hurt = player_on_hurt;

    g_classes[16].draw = preflect_draw;  g_classes[16].update = preflect_update;
    g_classes[16].init = preflect_init;  g_classes[16].on_hurt = preflect_on_hurt;

    g_classes[17].draw = card_draw;

    g_classes[2].draw = spinning_hat_draw; g_classes[2].update = spinning_hat_update;

    g_classes[18].draw = flower_draw;    g_classes[18].update = flower_update;
    g_classes[19].draw = coin_draw;      g_classes[19].update = coin_update;
    g_classes[19].on_bumped = coin_get_bumped; g_classes[19].on_death = coin_on_death;

    g_classes[22].draw = boss_draw;      g_classes[22].update = boss_update;
    g_classes[22].init = boss_init;      g_classes[22].on_death = boss_on_death;

    g_classes[23].draw = boss_draw;      g_classes[23].update = boss_update;
    g_classes[23].init = reflection_init; g_classes[23].on_death = boss_on_death;

    g_classes[24].draw = hand_draw;      g_classes[24].update = hand_update;
    g_classes[24].init = hand_init;

    g_classes[25].draw = laser_draw;     g_classes[25].update = laser_update;
    g_classes[25].is_hitting = laser_is_hitting;

    g_classes[26].draw = heart_draw;     g_classes[26].init = heart_init;
    g_classes[26].on_hurt = heart_on_hurt;

    g_classes[12].draw = health_draw;    g_classes[12].update = health_update;
    g_classes[12].init = health_init;

    g_classes[13].draw = boss_health_draw; g_classes[13].update = boss_health_update;

    g_classes[14].draw = tile_spawn_draw;  g_classes[14].on_death = tile_spawn_on_death;

    g_classes[15].draw = tile_draw;      g_classes[15].update = tile_update;
    g_classes[15].init = tile_init;      g_classes[15].on_hurt = tile_on_hurt;

    g_classes[21].draw = particle_draw;  g_classes[21].update = particle_update;

    g_classes[27].draw = poof_draw;
    g_classes[28].draw = pain_draw;
    g_classes[29].draw = points_draw;    g_classes[29].init = points_init;
}
