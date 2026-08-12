/*
 * game.h -- thin lifecycle wrapper so mac_main.c / host_test.c drive Celeste
 * through the same game_init/update/draw interface the JOB port used.
 */
#ifndef GAME_H
#define GAME_H

void game_init(unsigned rnd_seed);
void game_restart(unsigned rnd_seed);   /* back to title, fresh seed */
void game_update(void);
void game_draw(void);

#endif /* GAME_H */
