/*
 * mac_audio.c -- SFX + music playback through the classic Sound Manager.
 *
 * One sampled-sound channel (the Classic's hardware is single-channel anyway):
 *   - sfx are latest-wins: flush + play immediately (11127 Hz samples)
 *   - music is a long PCM loop at half rate (5564 Hz).  A virtual playhead
 *     advances in wall-clock ticks regardless of what's audible; whenever the
 *     channel goes idle (sfx finished, or loop wrapped), mac_audio_tick()
 *     re-enters the loop at the playhead by pointing samplePtr mid-buffer.
 *     So sfx "duck" the music instead of restarting it.
 */
#include "mac_audio.h"
#include "game.h"                 /* platform_sfx / platform_music decls */
#include "generated/sfx_data.h"
#include "generated/music_data.h"

#include <Sound.h>
#include <Events.h>               /* TickCount */
#include <Memory.h>

#define RATE_11K 0x2B7745D1L      /* Fixed 11127.27 Hz */
#define RATE_5K5 0x15BBA2E9L      /* Fixed  5563.64 Hz */
#define MUSIC_HZ 5564L
#define SFX_HZ   11127L

static SndChannelPtr gChan;
static SoundHeader   gHdr;
static int           gReady;
static int           gMusicId = -1;      /* index into music_table, -1 = off */
static unsigned long gMusicEpoch;        /* TickCount when the loop "started" */
static unsigned long gBusyUntil;         /* channel busy until this tick */

int mac_audio_init(void)
{
    OSErr err;
    gChan = NULL;
    err = SndNewChannel(&gChan, sampledSynth, 0L, NULL);
    gReady = (err == noErr && gChan != NULL);
    gBusyUntil = 0;
    return gReady;
}

void mac_audio_dispose(void)
{
    /* explicitly silence + flush BEFORE disposal: with one huge music
     * bufferCmd in flight, SndDisposeChannel alone can leave the Sound
     * Manager streaming the (freed) buffer after the app exits -- the
     * minutes-long DRONE heard on real hardware. */
    if (gChan) {
        SndCommand cmd;
        cmd.cmd = quietCmd; cmd.param1 = 0; cmd.param2 = 0;
        SndDoImmediate(gChan, &cmd);
        cmd.cmd = flushCmd;
        SndDoImmediate(gChan, &cmd);
        SndDisposeChannel(gChan, true);
        gChan = NULL;
    }
    gReady = 0;
}

static void play_buffer(const unsigned char *data, unsigned long len, long rate)
{
    SndCommand cmd;
    cmd.cmd = flushCmd; cmd.param1 = 0; cmd.param2 = 0;
    SndDoImmediate(gChan, &cmd);
    cmd.cmd = quietCmd;
    SndDoImmediate(gChan, &cmd);

    gHdr.samplePtr     = (Ptr)data;
    gHdr.length        = (long)len;
    gHdr.sampleRate    = rate;
    gHdr.loopStart     = 0;
    gHdr.loopEnd       = 0;
    gHdr.encode        = stdSH;
    gHdr.baseFrequency = 60;
    cmd.cmd = bufferCmd; cmd.param1 = 0; cmd.param2 = (long)&gHdr;
    SndDoImmediate(gChan, &cmd);
}

void platform_sfx(int n)
{
    const SfxSample *s;
    if (!gReady || n < 0 || n >= 64) return;
    s = &sfx_table[n];
    if (s->data == 0 || s->len == 0) return;
    play_buffer(s->data, s->len, RATE_11K);
    gBusyUntil = TickCount() + (s->len * 60L) / SFX_HZ + 1;
}

void platform_music(int id)
{
    if (!gReady) { gMusicId = id; return; }
    gMusicId = id;
    gMusicEpoch = TickCount();
    if (id < 0) {
        SndCommand cmd;
        cmd.cmd = flushCmd; cmd.param1 = 0; cmd.param2 = 0;
        SndDoImmediate(gChan, &cmd);
        cmd.cmd = quietCmd;
        SndDoImmediate(gChan, &cmd);
        gBusyUntil = 0;
    } else {
        gBusyUntil = 0;
        mac_audio_tick();
    }
}

void mac_audio_tick(void)
{
    unsigned long now, lenTicks, posTicks, offset;
    const MusicLoop *m;
    if (!gReady || gMusicId < 0 || gMusicId >= MUSIC_COUNT) return;
    now = TickCount();
    if (now < gBusyUntil) return;            /* sfx or music segment playing */

    m = &music_table[gMusicId];
    lenTicks = (m->len * 60L) / MUSIC_HZ;    /* loop length in ticks */
    if (lenTicks == 0) return;

    /* stingers (game-over / victory) play through once, then stop */
    if (gMusicId >= 2 && (now - gMusicEpoch) >= lenTicks) {
        gMusicId = -1;
        return;
    }

    posTicks = (now - gMusicEpoch) % lenTicks;
    offset = (posTicks * MUSIC_HZ) / 60L;    /* playhead in samples */
    if (offset >= m->len) offset = 0;

    /* snap the entry point forward to a near-zero-crossing: starting the DAC
     * mid-wave pops audibly (part of the reported crackle) */
    {
        unsigned long k, limit = offset + 96;
        if (limit > m->len) limit = m->len;
        for (k = offset; k < limit; k++) {
            unsigned char v = m->data[k];
            if (v >= 125 && v <= 131) { offset = k; break; }
        }
    }

    play_buffer(m->data + offset, m->len - offset, RATE_5K5);
    gBusyUntil = now + ((m->len - offset) * 60L) / MUSIC_HZ;
}
