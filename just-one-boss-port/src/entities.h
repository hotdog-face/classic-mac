/*
 * entities.h -- entity class registration + a few shared entity helpers.
 */
#ifndef ENTITIES_H
#define ENTITIES_H

#include "engine.h"

/* fills g_classes[] with the ported entity behaviours; call once at boot */
void register_entity_classes(void);

/* class 4 curtains: 0 = close, 1 = open (kicks off the 100-frame animation) */
void curtains_set_anim(Entity *self, int open);

/* spawn the boss already in a given phase and start attacking (used by
 * retry/continue via start_game) */
void boss_spawn_at_phase(int phase);
/* remove all combat entities (boss, attacks, tiles, effects), keeping the
 * persistent title/curtains/screens/figment; used when restarting or exiting */
void clear_combat_entities(void);

/* host-test/debug: skip the tutorial and drop straight into the boss fight */
void entities_debug_spawn_boss_fight(void);
void entities_debug_set_phase(int phase);
void entities_debug_hard(void);
void entities_debug_win(void);
void entities_debug_lose(void);

#endif /* ENTITIES_H */
