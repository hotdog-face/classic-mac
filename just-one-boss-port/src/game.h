/*
 * game.h -- the platform-agnostic game entry points the Mac shell drives at
 * 30fps (mirrors _init/_update/_draw), plus the shared scene globals and scene
 * helpers the entities reach for (the Lua cart kept these as file-level globals
 * and free functions).
 */
#ifndef GAME_H
#define GAME_H

#include "engine.h"

void game_init(void);     /* == _init  */
void game_update(void);   /* == _update (advance one 30fps tick) */
void game_draw(void);     /* == _draw  (render into p8_screen)   */

/* ---- scene globals (defined in game.c) ---- */
extern int g_scene_frame, g_rainbow_color;
extern int g_score, g_score_mult, g_boss_phase, g_timer_seconds, g_is_paused;
extern int g_freeze_frames, g_screen_shake_frames;
extern int g_starting_phase;
extern int g_score_data_index, g_time_data_index;
extern int g_hard_mode;

/* persistent high-score slots (Lua cartdata): 0/1 = normal score/time,
 * 2/3 = hard-mode score/time.  Backed by a scores file via the platform
 * hooks below; g_dset saves immediately (it only fires on a victory). */
int  g_dget(int i);
void g_dset(int i, int v);
void platform_load_scores(long s[4]);       /* leaves s untouched if no file */
void platform_save_scores(const long s[4]);

/* platform audio hooks (no-ops on the host build, Sound Manager in mac_audio.c) */
void platform_sfx(int n);
#define MUS_NONE     (-1)
#define MUS_MENU     0      /* loops */
#define MUS_BOSS     1      /* loops */
#define MUS_GAMEOVER 2      /* one-shot stinger */
#define MUS_VICTORY  3      /* one-shot stinger */
void platform_music(int id);

extern Entity *g_curtains, *g_title_screen;
extern Entity *g_player, *g_player_health, *g_boss_health, *g_player_figment;
extern Entity *g_boss, *g_player_reflection, *g_boss_reflection;

/* ---- scene helpers (Lua free functions) ---- */
void     freeze_and_shake_screen(int f, int s);
void     spawn_magic_tile(int frames);
void     spawn_particle_burst(Entity *src, num dy, int count, int color, num speed);
void     start_game(int phase);
void     show_title_screen(int dir);
Entity  *slide(Entity *e, int dir);   /* dir defaults handled by caller */

#endif /* GAME_H */
