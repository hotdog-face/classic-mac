/*
 * engine.c -- entity pool, spawn/registry, the per-frame update/hit/sort/reap
 * passes, bezier movement, promise scheduler, and counter helpers.  This is the
 * C translation of the Lua runtime (spawn_entity, _update body, make_promise,
 * apply_velocity/move).  See engine.h and PORTING-PLAN.md §4.
 */
#include "engine.h"
#include "p8.h"
#include <math.h>
#include <string.h>

/* full Promise definition (opaque in engine.h) -- needed before the pool */
struct Promise {
    int in_use;
    Entity *ctx;
    PromiseFn fn;
    int frames_to_finish;
    int started, finished, canceled;
    Promise *and_thens[8];
    int and_then_count;
    Promise *parent;
};

/* ---------- pools (no malloc churn on a 68000) ---------- */
static Entity  entity_pool[MAX_ENTITIES];
static Promise promise_pool[MAX_PROMISES];

Entity *g_entities[MAX_ENTITIES];
int     g_entity_count;
Promise *g_promises[MAX_PROMISES];
int      g_promise_count;

EntityClass g_classes[30];

/* ---------- counters ---------- */
int decrement_counter(int n) { return n > 0 ? n - 1 : 0; }
int increment_counter(int n) { return n + (n > 32000 ? -12000 : 1); }
int decrement_counter_prop(int *p)
{
    if (*p > 0) { *p = decrement_counter(*p); return *p <= 0; }
    return 0;
}

/* ---------- col/row + tile occupancy ----------
 * all-integer (these run in the O(n^2) hit loop every tick; the old
 * double-precision floor(x/10.0) was a hidden hot spot on the 68000) */
int entity_col(Entity *e)
{
    int xi = (int)e->x;
    if ((num)xi > e->x) xi--;                       /* floor toward -inf */
    return 1 + (xi >= 0 ? xi / 10 : (xi - 9) / 10); /* floor division */
}
int entity_row(Entity *e)
{
    int yi = (int)e->y;
    if ((num)yi > e->y) yi--;
    return 1 + (yi >= 0 ? yi / 8 : (yi - 7) / 8);
}

Entity *get_tile_occupant(Entity *e)
{
    int i;
    for (i = 0; i < g_entity_count; i++) {
        Entity *o = g_entities[i];
        if (o && o->occupies_tile &&
            entity_col(o) == entity_col(e) && entity_row(o) == entity_row(e))
            return o;
    }
    return NULL;
}

/* default virtual behaviours */
static void default_on_death(Entity *self) { (void)self; }
static void default_on_hurt(Entity *self)  { entity_die(self); }
static int  default_is_hitting(Entity *self, Entity *other)
{
    return entity_row(self) == entity_row(other) &&
           entity_col(self) == entity_col(other);
}
static void noop_fn(Entity *self) { (void)self; }

/* ---------- spawn ---------- */
static Entity *alloc_entity(void)
{
    int i;
    for (i = 0; i < MAX_ENTITIES; i++)
        if (!entity_pool[i].in_use) {
            Entity *e = &entity_pool[i];
            memset(e, 0, sizeof(*e));
            e->in_use = 1;
            return e;
        }
    return NULL;   /* pool exhausted -- MAX_ENTITIES too small; bump it */
}

Entity *spawn_entity(int class_id, num x, num y)
{
    int sid = 6 * class_id - 6;
    int extends = p8_fget(sid + 3);
    EntityClass *cls = &g_classes[class_id];
    Entity *e = alloc_entity();
    if (!e) return NULL;

    e->class_id = class_id;
    e->x = x; e->y = y;

    /* defaults matching the Lua base entity */
    e->init      = noop_fn;
    e->draw      = noop_fn;
    e->update    = noop_fn;
    e->on_hurt   = default_on_hurt;
    e->on_death  = default_on_death;
    e->on_bumped = noop_fn;
    e->is_hitting = default_is_hitting;

    /* metadata from sprite flags (identical to the Lua fget() loads) */
    e->render_layer      = p8_fget(sid);
    e->frames_to_death   = p8_fget(sid + 1);
    e->is_boss_generated = p8_fget_bit(sid + 2, 0);
    e->is_pause_immune   = p8_fget_bit(sid + 2, 1);
    e->hitbox_channel    = p8_fget(sid + 4);
    e->hurtbox_channel   = 0;    /* classes that need one set it in init */
    (void)extends;               /* inheritance handled by class fn assignment */

    /* class function pointers (fall back to defaults) */
    if (cls->draw)       e->draw       = cls->draw;
    if (cls->update)     e->update     = cls->update;
    if (cls->init)       e->init       = cls->init;
    if (cls->on_hurt)    e->on_hurt    = cls->on_hurt;
    if (cls->on_death)   e->on_death   = cls->on_death;
    if (cls->on_bumped)  e->on_bumped  = cls->on_bumped;
    if (cls->is_hitting) e->is_hitting = cls->is_hitting;

    e->init(e);

    if (g_entity_count < MAX_ENTITIES)
        g_entities[g_entity_count++] = e;
    return e;
}

void entity_die(Entity *e)
{
    if (!e->finished) {
        e->on_death(e);
        e->finished = 1;
    }
}

/* ---------- easing ---------- */
static num ease_linear(num t)  { return t; }
static num ease_out(num t)     { return t * t; }
static num ease_in(num t)      { num u = 1 - t; return 1 - u * u; }
static num ease_out_in(num t)
{
    return (t < 0.5f ? ease_out(2 * t) : 1 + ease_in(2 * t - 1)) / 2;
}
num easing_apply(int easing, num t)
{
    switch (easing) {
        case EASE_IN:     return ease_in(t);
        case EASE_OUT:    return ease_out(t);
        case EASE_OUT_IN: return ease_out_in(t);
        default:          return ease_linear(t);
    }
}

/* ---------- bezier movement (ports apply_velocity/move verbatim) ---------- */
void entity_apply_velocity(Entity *e)
{
    Movement *m = &e->movement;
    if (m->active) {
        num t, u, b0, b1, b2, b3;
        m->frames++;
        t = easing_apply(m->easing, (num)m->frames / (num)m->duration);
        u = 1 - t;
        /* cubic Bernstein weights, computed directly (the old power loops
         * cost ~20 software-float multiplies per moving entity per tick) */
        b0 = u * u * u;
        b1 = 3 * u * u * t;
        b2 = 3 * u * t * t;
        b3 = t * t * t;
        e->vx = b0 * m->bezier[0] + b1 * m->bezier[2]
              + b2 * m->bezier[4] + b3 * m->bezier[6] - e->x;
        e->vy = b0 * m->bezier[1] + b1 * m->bezier[3]
              + b2 * m->bezier[5] + b3 * m->bezier[7] - e->y;
        if (m->frames >= m->duration) {
            e->x = m->final_x; e->y = m->final_y;
            e->vx = 0; e->vy = 0; m->active = 0;
        }
    }
    e->x += e->vx;
    e->y += e->vy;
}

int entity_move(Entity *e, num x, num y, int dur, int easing,
                const num anchors[4], int relative)
{
    num sx = e->x, sy = e->y, ex = x, ey = y, dx, dy;
    num a[4];
    Movement *m = &e->movement;
    if (relative) { ex += sx; ey += sy; }
    dx = ex - sx; dy = ey - sy;
    if (anchors) { a[0]=anchors[0]; a[1]=anchors[1]; a[2]=anchors[2]; a[3]=anchors[3]; }
    else { a[0]=dx/4; a[1]=dy/4; a[2]=-dx/4; a[3]=-dy/4; }

    m->active = 1;
    m->frames = 0;
    m->duration = dur;
    m->easing = easing;
    m->final_x = ex; m->final_y = ey;
    m->bezier[0]=sx;       m->bezier[1]=sy;
    m->bezier[2]=sx+a[0];  m->bezier[3]=sy+a[1];
    m->bezier[4]=ex+a[2];  m->bezier[5]=ey+a[3];
    m->bezier[6]=ex;       m->bezier[7]=ey;
    return dur - 1;
}

void entity_cancel_move(Entity *e)
{
    e->vx = 0; e->vy = 0; e->movement.active = 0;
}

/* ================= promises ================= */
static Promise *alloc_promise(void)
{
    int i;
    for (i = 0; i < MAX_PROMISES; i++)
        if (!promise_pool[i].in_use) {
            Promise *p = &promise_pool[i];
            memset(p, 0, sizeof(*p));
            p->in_use = 1;
            return p;
        }
    return NULL;
}

static void promise_finish(Promise *p);

static void promise_start(Promise *p)
{
    if (p->started || p->canceled) return;
    p->started = 1;
    {
        int wait = p->fn ? p->fn(p->ctx) : 0;
        if (wait > 0) {
            p->frames_to_finish = wait;
            if (g_promise_count < MAX_PROMISES)
                g_promises[g_promise_count++] = p;
        } else {
            promise_finish(p);
        }
    }
}

static void promise_finish(Promise *p)
{
    int i;
    if (p->finished) return;
    p->finished = 1;
    for (i = 0; i < p->and_then_count; i++)
        promise_start(p->and_thens[i]);
}

static void promise_cancel(Promise *p)
{
    int i;
    if (p->canceled) return;
    p->canceled = 1; p->finished = 1;
    if (p->parent) promise_cancel(p->parent);
    for (i = 0; i < p->and_then_count; i++)
        promise_cancel(p->and_thens[i]);
}

static Promise *make_and_attach(Promise *parent, Entity *ctx, PromiseFn fn, int delay)
{
    Promise *p = alloc_promise();
    if (!p) return NULL;
    p->ctx = ctx;
    p->fn = fn;
    p->frames_to_finish = delay;      /* used only for pure-delay promises */
    if (parent) {
        p->parent = parent;
        if (parent->canceled)      promise_cancel(p);
        else if (parent->finished) promise_start(p);
        else if (parent->and_then_count < 8)
            parent->and_thens[parent->and_then_count++] = p;
    }
    return p;
}

/* fn that just consumes its stored delay */
static int delay_holder(Entity *ctx) { (void)ctx; return 0; }

Promise *promise_delay(Entity *ctx, int frames)
{
    Promise *p = make_and_attach(NULL, ctx, NULL, 0);
    if (!p) return NULL;
    if (frames > 0) {
        p->started = 1;
        p->frames_to_finish = frames;
        if (g_promise_count < MAX_PROMISES) g_promises[g_promise_count++] = p;
    } else {
        promise_start(p);
    }
    return p;
}

Promise *promise_run(Entity *ctx, PromiseFn fn)
{
    Promise *p = make_and_attach(NULL, ctx, fn, 0);
    if (p) promise_start(p);
    return p;
}

Promise *promise_then(Promise *prev, PromiseFn fn)
{
    return make_and_attach(prev, prev ? prev->ctx : NULL, fn, 0);
}

Promise *promise_then_delay(Promise *prev, int frames)
{
    /* a chained pure-delay: schedule when the parent finishes */
    Promise *p = alloc_promise();
    (void)delay_holder;
    if (!p) return NULL;
    p->ctx = prev ? prev->ctx : NULL;
    p->fn = NULL;
    p->parent = prev;
    /* store the delay in frames_to_finish; promise_start won't use fn, so we
     * special-case: wrap by starting a delay when parent finishes. */
    p->frames_to_finish = frames;
    if (prev) {
        if (prev->canceled)      promise_cancel(p);
        else if (prev->finished) { p->started = 1;
            if (g_promise_count < MAX_PROMISES) g_promises[g_promise_count++] = p; }
        else if (prev->and_then_count < 8)
            prev->and_thens[prev->and_then_count++] = p;
    }
    return p;
}

void promise_cancel_ctx(Entity *ctx)
{
    int i;
    for (i = 0; i < g_promise_count; i++)
        if (g_promises[i] && g_promises[i]->ctx == ctx)
            promise_cancel(g_promises[i]);
}

void engine_update_promises(void)
{
    int i, n = g_promise_count;
    for (i = 0; i < n; i++) {
        Promise *p = g_promises[i];
        if (p && !p->finished && decrement_counter_prop(&p->frames_to_finish))
            promise_finish(p);
    }
    /* compact: drop finished promises and free their slots */
    {
        int w = 0;
        for (i = 0; i < g_promise_count; i++) {
            Promise *p = g_promises[i];
            if (p && p->finished) { p->in_use = 0; continue; }
            g_promises[w++] = p;
        }
        g_promise_count = w;
    }
}

/* ================= per-frame passes ================= */
void engine_update_entities(int is_paused)
{
    int i, n = g_entity_count;
    for (i = 0; i < n && i < g_entity_count; i++) {
        Entity *e = g_entities[i];
        if (!e) continue;
        if (is_paused && !e->is_pause_immune) continue;

        e->counter_reached_zero = decrement_counter_prop(&e->default_counter);
        e->skip_default_move = 0;
        e->update(e);
        if (!e->skip_default_move) entity_apply_velocity(e);

        decrement_counter_prop(&e->invincibility_frames);
        e->frames_alive = increment_counter(e->frames_alive);
        if (decrement_counter_prop(&e->frames_to_death)) entity_die(e);
    }
}

void engine_run_hits(void)
{
    int i, j, n = g_entity_count;
    for (i = 0; i < n && i < g_entity_count; i++) {
        for (j = 0; j < n && j < g_entity_count; j++) {
            Entity *a = g_entities[i], *b = g_entities[j];
            if (!a || !b || i == j) continue;
            if ((a->hitbox_channel & b->hurtbox_channel) &&
                a->is_hitting(a, b) && b->invincibility_frames <= 0)
                b->on_hurt(b);
        }
    }
}

/* render order: layer asc, then y asc (is_rendered_on_top_of) -- insertion sort
 * exactly like the Lua stable pass. */
static int rendered_on_top(Entity *a, Entity *b)
{
    if (a->render_layer == b->render_layer) return a->y > b->y;
    return a->render_layer > b->render_layer;
}
void engine_sort_for_render(void)
{
    int i;
    for (i = 1; i < g_entity_count; i++) {
        int j = i;
        while (j > 0 && rendered_on_top(g_entities[j - 1], g_entities[j])) {
            Entity *t = g_entities[j]; g_entities[j] = g_entities[j-1]; g_entities[j-1] = t;
            j--;
        }
    }
}

void engine_reap(void)
{
    int i, w = 0;
    for (i = 0; i < g_entity_count; i++) {
        Entity *e = g_entities[i];
        if (e && e->finished) { e->in_use = 0; continue; }
        g_entities[w++] = e;
    }
    g_entity_count = w;
}

void engine_init(void)
{
    memset(entity_pool, 0, sizeof(entity_pool));
    memset(promise_pool, 0, sizeof(promise_pool));
    g_entity_count = 0;
    g_promise_count = 0;
}
