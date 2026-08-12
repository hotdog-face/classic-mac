#include "game.h"
#include "celeste.h"
#include "p8.h"

extern int celeste_glue_p8call(CELESTE_P8_CALLBACK_TYPE call, ...);

void game_init(unsigned rnd_seed)
{
    p8_init();
    Celeste_P8_set_call_func(celeste_glue_p8call);
    Celeste_P8_set_rndseed(rnd_seed);
    Celeste_P8_init();
}

/* back to the title screen with a fresh seed (pause menu "restart") */
void game_restart(unsigned rnd_seed)
{
    Celeste_P8_set_rndseed(rnd_seed);
    Celeste_P8_init();
}

void game_update(void)
{
    Celeste_P8_update();
}

void game_draw(void)
{
    Celeste_P8_draw();
}
