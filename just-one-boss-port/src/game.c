/*
 * game.c -- global scene state + the _init/_update/_draw translation + the
 * Lua "free function" helpers (start_game, slide, spawn_magic_tile, ...).
 *
 * PHASE 3: title screen -> press RIGHT -> curtains open -> player on the stage
 * collecting tutorial magic tiles (boss_phase 0).  The boss and its attacks are
 * Phase 4; magic_tile's boss milestones are stubbed (see entities.c).
 */
#include "game.h"

void platform_set_room(const unsigned char *room);  /* mac_display / host stub */
#include "engine.h"
#include "entities.h"
#include "p8.h"
#include "util.h"
#include <string.h>

/* cached static background (frame + a clean checkerboard floor), rendered once so
 * _draw doesn't re-blit 256 map tiles every frame -- a big win on the 68000. */
static unsigned char g_bg[P8_W * P8_H];

/* The play grid is 8 cols x 5 rows; a cell is 10x8 world px.  Entities draw at
 * camera (-23,-65), so world (0,0) is screen (23,65) -- the floor's top-left. */
#define FLOOR_X 23
#define FLOOR_Y 65
#define FLOOR_COLS 8
#define FLOOR_ROWS 5
#define FLOOR_CW 10
#define FLOOR_CH 8

/* draw a simple line-drawn checkerboard floor (quiet, so sprites read clearly):
 * solid black base + crisp grid lines aligned to the play cells */
static void draw_floor(void)
{
    int c, r, x, y;
    int fw = FLOOR_COLS * FLOOR_CW, fh = FLOOR_ROWS * FLOOR_CH;
    /* clear a generous black area to wipe the old busy tiled floor (it extends
     * higher/wider than the logical grid) */
    p8_rectfill(FLOOR_X - 8, FLOOR_Y - 12, FLOOR_X + fw + 8, FLOOR_Y + fh + 6, 0);
    /* grid lines (color 6 = light grey; solid because whole cols/rows share a
     * dither phase) */
    for (c = 0; c <= FLOOR_COLS; c++) {
        x = FLOOR_X + c * FLOOR_CW;
        p8_line(x, FLOOR_Y, x, FLOOR_Y + fh, 6);
    }
    for (r = 0; r <= FLOOR_ROWS; r++) {
        y = FLOOR_Y + r * FLOOR_CH;
        p8_line(FLOOR_X, y, FLOOR_X + fw, y, 6);
    }
}

/* ---- scene globals ---- */
int g_scene_frame, g_rainbow_color;
int g_score, g_score_mult, g_boss_phase, g_timer_seconds, g_is_paused;
int g_freeze_frames, g_screen_shake_frames;
int g_starting_phase;
int g_score_data_index = 0, g_time_data_index = 1;
int g_hard_mode;

static int g_cartdata[4];
int  g_dget(int i) { return (i >= 0 && i < 4) ? g_cartdata[i] : 0; }
void g_dset(int i, int v)
{
    long s[4];
    int k;
    if (i < 0 || i >= 4) return;
    g_cartdata[i] = v;
    for (k = 0; k < 4; k++) s[k] = g_cartdata[k];
    platform_save_scores(s);       /* only fires on victories -- cheap */
}

Entity *g_curtains, *g_title_screen;
Entity *g_player, *g_player_health, *g_boss_health, *g_player_figment;
Entity *g_boss, *g_player_reflection, *g_boss_reflection;

/* ---- helpers ---- */
void freeze_and_shake_screen(int f, int s)
{
    if (f > g_freeze_frames)       g_freeze_frames = f;
    if (s > g_screen_shake_frames) g_screen_shake_frames = s;
}

void spawn_magic_tile(int frames)
{
    Entity *t;
    if (g_boss_health && g_boss_health->n[0] >= 60)   /* bh_health */
        g_boss_health->default_counter = 61;
    t = spawn_entity(14, 10 * rnd_int(1, 8) - 5, 8 * rnd_int(1, 5) - 4);
    if (t) t->frames_to_death = frames;
}

Entity *slide(Entity *e, int dir)
{
    num anchors[4];
    anchors[0] = dir * 70; anchors[1] = 0; anchors[2] = 0; anchors[3] = 0;
    entity_move(e, -dir * 129, 0, 100, EASE_LINEAR, anchors, 1);
    return e;
}

/* start_game via a curtain promise chain (mirrors curtains:promise_sequence) */
static int g_pending_phase;
static int pf_curtains_open(Entity *e) { curtains_set_anim(e, 1); return 0; }
static int pf_start_body(Entity *e)
{
    int n = 30, phase = g_pending_phase;
    (void)e;
    g_score_mult = 0;
    g_boss_phase = imax(0, phase - 1);
    g_is_paused = 0;

    g_player        = spawn_entity(11, 0, 0);
    g_player_health = spawn_entity(12, 0, 0);
    g_boss_health   = spawn_entity(13, 0, 0);

    if (phase > 0) {
        boss_spawn_at_phase(phase);   /* retry/continue drops straight into the fight */
    } else {
        /* original waits 150+n frames here, masked by intro music we don't have
         * yet -- shorten so the tutorial's first tile appears promptly */
        spawn_magic_tile(50 + n);
    }
    return 0;
}
void start_game(int phase)
{
    Promise *p;
    g_pending_phase = phase;
    p = promise_delay(g_curtains, 35);
    p = promise_then(p, pf_curtains_open);
    promise_then(p, pf_start_body);
}

void show_title_screen(int dir)
{
    platform_music(MUS_MENU);
    clear_combat_entities();       /* wipe any lingering fight before the menu */
    g_title_screen->x = (dir == -1) ? -66 : 192;
    slide(g_title_screen, dir);
    g_starting_phase = 1;
    g_title_screen->iv[1] = 115;   /* scr_frames_until_active */
}

/* ---- _init ---- */
void game_init(void)
{
    engine_init();
    register_entity_classes();
    p8_init();
    p8_srand(0x1BADF00Du);

    /* load saved high scores (slots stay 0 if no file yet) */
    {
        long s[4]; int k;
        s[0] = s[1] = s[2] = s[3] = 0;
        platform_load_scores(s);
        for (k = 0; k < 4; k++) g_cartdata[k] = (int)s[k];
    }

    g_scene_frame = 0; g_rainbow_color = 8;
    g_score = 0; g_score_mult = 0; g_boss_phase = 8;
    g_timer_seconds = 0; g_is_paused = 0;
    g_freeze_frames = 0; g_screen_shake_frames = 0;
    g_starting_phase = 0;
    g_boss = 0; g_player = 0; g_player_health = 0; g_boss_health = 0;
    g_player_reflection = 0; g_player_figment = 0; g_boss_reflection = 0;
    g_hard_mode = 0;

    /* render the static background once and cache it.  Owner direction: lines
     * and solids over dithered fills -- so no map tiles at all: solid black
     * with a clean white proscenium frame around the stage, and the grid floor. */
    p8_cls();
    p8_rect(20, 8, 106, 118, 6);     /* stage frame (boss space + floor) */
    p8_rect(18, 6, 108, 120, 6);     /* double line for a theatrical feel */
    draw_floor();
    memcpy(g_bg, p8_screen, sizeof g_bg);
    platform_set_room(g_bg);          /* donut principle: pre-dither once */

    /* entity 6 title_screen, entity 4 curtains (closed = the title backdrop) */
    g_title_screen = spawn_entity(6, 0, 0);
    g_curtains     = spawn_entity(4, 0, 0);

    platform_music(MUS_MENU);      /* Lua: music(37) menu music */
}

/* ---- _update ---- */
void game_update(void)
{
    if (g_freeze_frames > 0) {
        g_freeze_frames = decrement_counter(g_freeze_frames);
        if (g_player) { /* player:check_inputs() during freeze -- minor, omitted */ }
        return;
    }
    if (g_scene_frame % 30 == 0 && !g_is_paused && g_boss_phase > 0)
        g_timer_seconds = (g_timer_seconds < 5999) ? g_timer_seconds + 1 : 5999;

    g_screen_shake_frames = decrement_counter(g_screen_shake_frames);
    g_scene_frame = increment_counter(g_scene_frame);

    g_rainbow_color = (g_scene_frame / 4) % 6 + 8;
    if (g_rainbow_color == 13) g_rainbow_color = 14;

    engine_update_promises();
    engine_update_entities(g_is_paused);
    if (!g_is_paused) engine_run_hits();
    engine_reap();
    engine_sort_for_render();
}

/* ---- _draw ---- */
static void draw_int(int v, int x, int y, int c)   /* small unsigned itoa+print */
{
    char buf[8]; int n = 0, i;
    if (v <= 0) { p8_print("0", x, y, c); return; }
    while (v > 0 && n < 7) { buf[n++] = (char)('0' + v % 10); v /= 10; }
    { char rev[8]; for (i = 0; i < n; i++) rev[i] = buf[n - 1 - i]; rev[n] = 0;
      p8_print(rev, x, y, c); }
}

void game_draw(void)
{
    int shake_x = 0, i;
    if (g_freeze_frames <= 0 && g_screen_shake_frames > 0)
        shake_x = (g_scene_frame % 2) * 2 - 1;

    /* background restore, dirty rows only.  Rows drawn last frame (flagged in
     * p8_touched) get their background copied back and are marked p8_restored
     * (they changed, so the display must re-dither them); the touch flags then
     * reset so this frame's entity draws re-flag only their new rows.  Rows
     * nobody drew cost nothing: no copy, no dither, no blit. */
    {
        int y;
        short tx0[P8_H], tx1[P8_H];
        memcpy(tx0, p8_touched_x0, sizeof tx0);   /* last frame's touched spans */
        memcpy(tx1, p8_touched_x1, sizeof tx1);
        p8_spans_reset();                          /* clear spans for this frame */
        for (y = 0; y < P8_H; y++) {
            if (p8_touched[y]) {
                p8_touched[y] = 0;
                /* dirty-rectangle erase: restore only the span drawn last frame */
                p8_restore_span(g_bg, y, tx0[y], tx1[y]);
            } else {
                p8_restored[y] = 0;
            }
        }
    }

    /* entities: world layers offset to play-area origin, UI/high layers in
     * screen space (matches the Lua camera dance) */
    p8_camera(shake_x - 23, -65);
    for (i = 0; i < g_entity_count; i++) {
        Entity *e = g_entities[i];
        if (!e) continue;
        if (e->render_layer >= 13) p8_camera(shake_x, 0);

        /* the curtains layer also hosts the score/timer HUD */
        if (e->class_id == 4) {
            if (g_score_mult > 0) {
                draw_sprite(6, 2, 69, 71, 11, 7, 0);
                draw_int(g_score_mult, 8, 3, 0);
            }
            if (g_score > 0) {
                int digits = 1, s = g_score * 100; while (s >= 10) { s /= 10; digits++; }
                draw_int(g_score * 100, 121 - 4 * digits, 3, 6);
            }
        }
        e->draw(e);
        p8_pal_reset();
    }
}
