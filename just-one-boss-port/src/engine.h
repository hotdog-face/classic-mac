/*
 * engine.h -- the C shape of the PICO-8 entity/promise/movement runtime.
 *
 * Lua entities are open tables with arbitrary fields + closures.  We collapse
 * that into one fixed Entity struct: named fields for everything the *engine*
 * touches, plus a small scratch bag (n[]/iv[]/ref[]) each entity type aliases
 * with #defines for readable per-type state.  Class behaviour lives in
 * function pointers set from a class registry built off the extracted sprite
 * flags.  See PORTING-PLAN.md §4.
 */
#ifndef ENGINE_H
#define ENGINE_H

/* Fractional world coordinates.  Kept behind a typedef so the whole game can be
 * flipped to 16.16 fixed-point later if 68000 software-float is too slow. */
typedef float num;

typedef struct Entity  Entity;
typedef struct Promise Promise;
typedef void (*EntityFn)(Entity *self);
typedef num  (*EasingFn)(num t);
/* a promise step: run some work, return how many frames to wait before the
 * chained steps run (<=0 == continue immediately).  Mirrors the Lua idiom
 * where a callback returns move()'s dur-1, poof()'s 12, etc. */
typedef int  (*PromiseFn)(Entity *ctx);

#define MAX_ENTITIES 192
#define MAX_PROMISES 96

/* easing ids (functions live in engine.c) */
enum { EASE_LINEAR, EASE_IN, EASE_OUT, EASE_OUT_IN, EASE_COUNT };

typedef struct {
    int  active;
    int  frames, duration;
    int  easing;               /* EASE_* */
    num  final_x, final_y;
    num  bezier[8];            /* 4 control points (x,y) */
} Movement;

struct Entity {
    int class_id;              /* 1..29; 0 == free slot */
    int in_use;
    int finished;              /* flagged for removal at end of frame */

    /* spatial */
    num x, y, vx, vy;
    num draw_offset_x, draw_offset_y;
    Movement movement;

    /* counters (all auto-decremented by the engine where PICO-8 did) */
    int default_counter, frames_alive, frames_to_death, invincibility_frames;

    /* metadata loaded from sprite flags at spawn */
    int render_layer, hitbox_channel, hurtbox_channel;
    int is_boss_generated, is_pause_immune, occupies_tile;

    /* per-type scratch -- alias with #defines in entities.c
     * (e.g. #define e_facing iv[0]) */
    num  n[8];
    int  iv[8];
    Entity *ref[4];

    /* idle-bob animation (used by the boss + hands via calc_idle_mult) */
    num idle_mult;
    int is_idle;

    /* virtual behaviour (engine calls these generically) */
    EntityFn init;
    EntityFn draw;             /* receives self; reads x,y,frames_alive,... */
    EntityFn update;           /* returns void; set self->skip_default_move to
                                  suppress the post-update apply_velocity */
    EntityFn on_hurt;          /* default: die */
    EntityFn on_death;         /* default: noop */
    EntityFn on_bumped;        /* default: noop -- player walked into this tile */
    int (*is_hitting)(Entity *self, Entity *other); /* default: same tile */

    int skip_default_move;     /* update() sets this like the Lua `return true` */
    int counter_reached_zero;  /* set by the engine each frame: did default_counter
                                  hit 0 this tick?  (Lua passed this to update) */
};

/* one row of the class registry */
typedef struct {
    EntityFn draw, update, init, on_hurt, on_death, on_bumped;
    int (*is_hitting)(Entity *, Entity *);
} EntityClass;

extern EntityClass g_classes[30];   /* index by class_id */

/* ---- globals mirroring the Lua scene vars (defined in game.c) ---- */
extern Entity *g_entities[MAX_ENTITIES];
extern int     g_entity_count;

/* ---- entity lifecycle ---- */
void    engine_init(void);
Entity *spawn_entity(int class_id, num x, num y);   /* NULL x/y default to 0 */
void    entity_die(Entity *e);
int     entity_col(Entity *e);   /* 1 + floor(x/10) */
int     entity_row(Entity *e);   /* 1 + floor(y/8)  */
Entity *get_tile_occupant(Entity *e);

/* ---- the per-frame passes (called by game._update) ---- */
void    engine_update_entities(int is_paused);
void    engine_run_hits(void);
void    engine_sort_for_render(void);
void    engine_reap(void);       /* remove finished entities */

/* ---- movement / bezier ---- */
num     easing_apply(int easing, num t);
void    entity_apply_velocity(Entity *e);
/* move to (x,y) over dur frames; anchors NULL -> auto quarter-vectors;
 * relative adds current pos.  Returns dur-1 (frames to wait), Lua-style. */
int     entity_move(Entity *e, num x, num y, int dur, int easing,
                    const num anchors[4], int relative);
void    entity_cancel_move(Entity *e);

/* ---- promises (frame scheduler) ---- */
extern Promise *g_promises[MAX_PROMISES];
extern int      g_promise_count;

Promise *promise_delay(Entity *ctx, int frames);     /* fires after N frames */
Promise *promise_run(Entity *ctx, PromiseFn fn);     /* runs fn, waits fn's result */
Promise *promise_then(Promise *p, PromiseFn fn);     /* chain after p finishes */
Promise *promise_then_delay(Promise *p, int frames);
void     promise_cancel_ctx(Entity *ctx);            /* cancel all for an entity */
void     engine_update_promises(void);

/* counters */
int  decrement_counter(int n);
int  decrement_counter_prop(int *p);   /* returns 1 when it hits 0 this frame */
int  increment_counter(int n);

#endif /* ENGINE_H */
