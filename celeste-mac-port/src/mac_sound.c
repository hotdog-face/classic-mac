/*
 * mac_sound.c -- Celeste audio on ONE sampled Sound Manager channel.
 *
 * Two simultaneous sampled channels are beyond the old Sound Manager on
 * Plus/Classic-class hardware (garbled mix + interrupt load that starves
 * input), so: a single channel.  MUSIC streams as ~0.5s chunks; SFX
 * interrupt the channel and music resumes behind them (one-voice ducking).
 *
 * PACING IS SELF-CLOCKING: earlier revisions estimated the queue by
 * TickCount arithmetic, and any skew between the tick clock and the Sound
 * Manager's true consumption rate accumulated until the stream starved or
 * overran (crackle + dead controls after ~a minute of play).  Now the
 * channel itself is asked: pending = SndChannel.qTail - qHead, playing =
 * SndChannelStatus.scChannelBusy, and we simply top the queue back up to
 * a fixed depth.  No clocks, nothing to drift.
 *
 * No interrupt callbacks -- everything runs from the main loop, so there
 * are no A5/interrupt-time hazards.
 */
#include "mac_sound.h"
#include "sfx_data.h"
#include "music_data.h"

#include <MacTypes.h>
#include <Sound.h>
#include <Events.h>   /* TickCount (sfx restart throttle only, not pacing) */
#include <string.h>

static SndChannelPtr gChan;
static int           gReady;

/* runtime mute (File > Sound).  When set, we neither play sfx nor stream
 * music -- stopping the stream is the real win, it reclaims the per-frame
 * CPU the old "Celeste NoSnd" build saved by compiling audio out. */
int mac_sound_muted = 0;

/* live telemetry for the on-screen audio diagnostic (mac_main AUDIO_DIAG) */
int mac_snd_diag_chunks;   /* music chunks queued since launch */
int mac_snd_diag_err;      /* last nonzero SndDoCommand result */
int mac_snd_diag_sfx;      /* sfx plays since launch */

/* headers must stay valid until their queued command executes: use a ring */
#define N_HDR 8
static SoundHeader gHdr[N_HDR];
static int gHdrNext;

/* music streaming state */
#define MUS_CHUNK   2782             /* ~0.5s at 5563 Hz */
#define QUEUE_DEPTH 3                /* chunks kept buffered (~1.5s) */
static int  gMusSlot = -1;           /* music_table slot playing, -1 none */
static long gMusPos;                 /* next byte to queue */
static int  gMusQueued;              /* music chunks queued since last flush
                                        (upper bound on unplayed ones) */

#define RATE_11KHZ 0x2B110000UL      /* 11025 Hz as 16.16 Fixed */
#define RATE_5KHZ  0x15BB0000UL      /*  5563 Hz as 16.16 Fixed */

#ifndef initMono                     /* Retro68's Multiverse.h omits it */
#define initMono 0L
#endif

int mac_sound_init(void)
{
    OSErr err;
#ifdef CELESTE_MAC_NO_AUDIO   /* perf-diagnosis build: audio fully off */
    gChan = 0; gReady = 0;
    return -1;
#endif
    gChan = 0;
    err = SndNewChannel(&gChan, sampledSynth, initMono, 0);
    gReady = (err == noErr && gChan != 0);
    return gReady ? 0 : -1;
}

/* diag: expose raw queue indices + pending for the overlay */
void mac_sound_diag(int *pending, int *qhead, int *qtail)
{
    int d = 0;
    if (gChan) {
        d = gChan->qTail - gChan->qHead;
        if (d < 0) d += gChan->qLength;
        *qhead = gChan->qHead; *qtail = gChan->qTail;
    } else {
        *qhead = *qtail = -1;
    }
    *pending = d;
}

/* commands sitting in the channel queue, NOT counting the one in progress.
 * Plain struct reads -- NO trap.  (SndChannelStatus bombs with system error
 * ID 12 "unimplemented trap" on the old Sound Manager: its _SoundDispatch
 * selector doesn't exist there.  qHead/qTail are just memory.) */
static int chan_pending(void)
{
    int d = gChan->qTail - gChan->qHead;
    if (d < 0) d += gChan->qLength;
    return d;
}

static void chan_flush(void)
{
    SndCommand cmd;
    cmd.cmd = quietCmd; cmd.param1 = 0; cmd.param2 = 0;
    SndDoImmediate(gChan, &cmd);
    cmd.cmd = flushCmd;
    SndDoImmediate(gChan, &cmd);
}

static void queue_buffer(const unsigned char *data, long len,
                         unsigned long rate)
{
    SndCommand   cmd;
    SoundHeader *h = &gHdr[gHdrNext];
    gHdrNext = (gHdrNext + 1) % N_HDR;

    memset(h, 0, sizeof *h);
    h->samplePtr     = (Ptr)data;
    h->length        = (unsigned long)len;
    h->sampleRate    = rate;
    h->encode        = stdSH;
    h->baseFrequency = 60;           /* middle C: recorded pitch */

    cmd.cmd    = bufferCmd;
    cmd.param1 = 0;
    cmd.param2 = (long)h;
    {
        OSErr e = SndDoCommand(gChan, &cmd, true);
        if (e != noErr) mac_snd_diag_err = e;
    }
}

static void queue_music_chunk(void)
{
    const MusicTrack *t = &music_table[gMusSlot];
    long n = t->len - gMusPos;
    if (n > MUS_CHUNK) n = MUS_CHUNK;
    queue_buffer(t->data + gMusPos, n, RATE_5KHZ);
    gMusPos += n;
    if (gMusPos >= t->len) gMusPos = 0;          /* seamless loop wrap */
    gMusQueued++;
    mac_snd_diag_chunks++;
}

void mac_sound_play(int sfx_id)
{
    const SfxSample *s;
    /* rapid same-sound restarts (jump spam, wall-jump chains) hard-cut the
     * sample mid-wave every time = an audible CLICK per press = "crackle".
     * One voice can't overlap sounds, but it can decline to restart the
     * same effect within ~1/8s -- retail plays these on separate channels
     * anyway, so dropping the re-trigger is the least-wrong rendering. */
    static unsigned long last_tick;
    static int last_id = -1;

    if (mac_sound_muted) return;
    if (!gReady || sfx_id < 0 || sfx_id >= SFX_MAX) return;
    s = &sfx_table[sfx_id];
    if (s->data == 0) return;
    if (sfx_id == last_id && (long)(TickCount() - last_tick) < 8) return;
    last_id = sfx_id;
    last_tick = TickCount();

    /* rewind the music by the queued-but-unstarted chunks we flush (the
     * in-progress one counts as played -- skipping a hair beats repeating) */
    if (gMusSlot >= 0) {
        int pend = chan_pending();
        int mus = (gMusQueued < pend) ? gMusQueued : pend;
        const MusicTrack *t = &music_table[gMusSlot];
        gMusPos -= (long)mus * MUS_CHUNK;
        while (gMusPos < 0) gMusPos += t->len;
    }
    chan_flush();
    gMusQueued = 0;
    queue_buffer(s->data, s->len, RATE_11KHZ);
    mac_snd_diag_sfx++;
}

void mac_music_play(int index)
{
    int slot = index / 10;           /* celeste convention: 0,10,20,30,40 */
#ifdef CELESTE_MAC_NO_MUSIC          /* sfx-only: skip the streaming CPU drain */
    (void)slot; return;
#endif
    if (mac_sound_muted) return;
    if (!gReady) return;
    if (index < 0) { mac_music_stop(); return; }
    if (slot < 0 || slot >= MUSIC_SLOTS || music_table[slot].data == 0) return;
    if (slot == gMusSlot) return;    /* already looping */

    chan_flush();
    gMusSlot = slot;
    gMusPos = 0;
    gMusQueued = 0;
    mac_music_idle();                /* prime the queue now */
}

void mac_music_stop(void)
{
    if (!gReady || gMusSlot < 0) return;
    gMusSlot = -1;
    chan_flush();
    gMusQueued = 0;
}

/* once per frame: keep QUEUE_DEPTH buffers PENDING beyond whatever is
 * playing (music fills in behind any playing sfx).  Pending-only pacing
 * needs no busy flag: the moment SM dequeues a buffer to play it, pending
 * drops and we top it back up -- steady state is 1 playing + QUEUE_DEPTH
 * queued (~1s+).  Self-heals from a total drain the same way. */
void mac_music_idle(void)
{
    int guard = 4;
#ifdef CELESTE_MAC_NO_MUSIC
    return;
#endif
    if (mac_sound_muted) return;
    if (!gReady || gMusSlot < 0) return;
    while (guard-- && chan_pending() < QUEUE_DEPTH)
        queue_music_chunk();
}

void mac_sound_dispose(void)
{
    if (gChan) { SndDisposeChannel(gChan, true); gChan = 0; }
    gReady = 0;
}
