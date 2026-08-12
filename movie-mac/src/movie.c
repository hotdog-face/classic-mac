/*
 * Movie -- disk-STREAMING 1-bit video player for the Mac Classic, MOV2.
 *
 * MOV2 (see tools/gen_stream2.py) is the MacFlim-inspired upgrade over MOV1:
 *   - INTER-FRAME DELTAS: each frame ships only the byte-runs that changed
 *     since the last, so the disk carries a fraction of a full frame and the
 *     decoder only touches changed pixels.  Keyframes every 24 frames resync.
 *   - SOUND: 8-bit audio interleaved one chunk per frame, streamed through a
 *     single Sound Manager channel.  Video is paced to audio consumption, so
 *     picture and sound stay together structurally (no drifting clock).
 *   - DIRECT SCREEN WRITES: changed rows are BlockMove'd straight into screen
 *     memory (QuickDraw CopyBits was the old bottleneck) at a byte-aligned,
 *     centered 256x256 spot.
 *
 * Streams off disk, so length is bounded by disk space, not RAM.  Any key or
 * click quits.  Falls back to a plain full-frame path for older MOV1 files.
 */
#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include <Files.h>
#include <Sound.h>
#include <Memory.h>
#include <StandardFile.h>

static WindowPtr gWin;

static void die(const char *msg)
{
    Str255 p; short len = 0;
    while (msg[len]) { p[len+1] = msg[len]; len++; }
    p[0] = (unsigned char)len;
    SetPort(gWin); ForeColor(whiteColor);
    MoveTo(20, 40); DrawString(p);
    while (!Button()) {}
}

/* ---- audio: one channel, a ring of chunks kept queued ------------------ */
#ifndef initMono
#define initMono 0
#endif
#define RING 8                          /* audio chunks buffered */
#define QDEPTH 4                        /* keep ~this many queued (paces video) */
static SndChannelPtr gChan;
static SoundHeader   gHdr[RING];
static Ptr           gAudio;            /* RING * aperf bytes */
static int           gRing;
static long          gAperf;
static unsigned long gARate;

static int chan_pending(void)          /* queued buffers, plain struct reads */
{
    int d;
    if (!gChan) return 0;
    d = gChan->qTail - gChan->qHead;
    if (d < 0) d += gChan->qLength;
    return d;
}
static void queue_audio(const unsigned char *src)
{
    SoundHeader *h; SndCommand c; unsigned char *dst;
    if (!gChan || gAperf <= 0) return;
    dst = (unsigned char *)(gAudio + (long)gRing * gAperf);
    BlockMove((Ptr)src, dst, gAperf);
    h = &gHdr[gRing];
    memset(h, 0, sizeof *h);
    h->samplePtr = (Ptr)dst; h->length = (unsigned long)gAperf;
    h->sampleRate = gARate << 16;   /* Hz -> 16.16 Fixed (the lockup bug: raw Hz
                                       read as 0.12Hz => buffer never finishes) */
    h->encode = stdSH; h->baseFrequency = 60;
    c.cmd = bufferCmd; c.param1 = 0; c.param2 = (long)h;
    SndDoCommand(gChan, &c, true);
    gRing = (gRing + 1) % RING;
}

int main(void)
{
    Rect full, dst;
    EventRecord ev;
    short ref;
    OSErr err;
    unsigned char hdr[32];
    long frameCount, fbytes, dataStart, i = 0, played = 0;
    short w, h, rowb, fps, flags, keyint;
    int isMOV2;
    Ptr buf, vidTmp;
    unsigned char dirty[256];
    GrafPtr port; long srb; Ptr scr0;
    unsigned long fpsMark, frames = 0, curfps = 0;

    InitGraf(&qd.thePort); InitFonts(); InitWindows(); InitMenus();
    TEInit(); InitDialogs(NULL); InitCursor(); FlushEvents(everyEvent, 0);

    {   /* pick a film: MOV2 (and legacy MOV1) streams show in the list */
        Point where; SFReply reply; SFTypeList types;
        where.h = 100; where.v = 80;
        types[0] = 'MOV2'; types[1] = 'MOV1';
        SFGetFile(where, "\p", NULL, 2, types, NULL, &reply);
        if (!reply.good) return 0;
        err = FSOpen(reply.fName, reply.vRefNum, &ref);
    }
    HideCursor();
    full = qd.screenBits.bounds;
    gWin = NewWindow(NULL, &full, "\p", true, plainDBox, (WindowPtr)-1, false, 0);
    SetPort(gWin);
    FillRect(&gWin->portRect, &qd.black);
    if (err != noErr) { die("could not open the movie file"); goto done; }

    { long n = 32; FSRead(ref, &n, hdr); }
    isMOV2 = (hdr[0]=='M'&&hdr[1]=='O'&&hdr[2]=='V'&&hdr[3]=='2');
    frameCount = ((long)hdr[4]<<24)|((long)hdr[5]<<16)|((long)hdr[6]<<8)|hdr[7];
    w   = (hdr[8]<<8)|hdr[9];   h = (hdr[10]<<8)|hdr[11];
    fps = (hdr[12]<<8)|hdr[13];
    rowb = w / 8; fbytes = (long)rowb * h;
    if (isMOV2) {
        flags = (hdr[14]<<8)|hdr[15];
        gARate = ((unsigned long)hdr[16]<<24)|((unsigned long)hdr[17]<<16)|
                 ((unsigned long)hdr[18]<<8)|hdr[19];
        gAperf = ((long)hdr[20]<<24)|((long)hdr[21]<<16)|((long)hdr[22]<<8)|hdr[23];
        keyint = (hdr[24]<<8)|hdr[25];
        dataStart = 32;
    } else {
        flags = 0; gAperf = 0; keyint = 1; dataStart = 16;
        SetFPos(ref, fsFromStart, 16);         /* MOV1 header is 16 bytes */
    }

    buf    = NewPtrClear(fbytes);
    vidTmp = NewPtr(fbytes);
    if (!buf || !vidTmp) { die("out of memory"); goto closed; }

    /* audio channel */
    if (isMOV2 && (flags & 1)) {
        gAudio = NewPtr((long)RING * gAperf);
        if (gAudio && SndNewChannel(&gChan, sampledSynth, initMono, NULL) != noErr)
            gChan = 0;
    }

    /* centered 256x256, byte-aligned; compute the screen address once */
    SetRect(&dst, (short)((full.right-full.left-w)/2),
                  (short)((full.bottom-full.top-h)/2),
                  (short)((full.right-full.left+w)/2),
                  (short)((full.bottom-full.top+h)/2));
    dst.left &= ~7;                              /* force byte alignment */
    port = (GrafPtr)gWin;
    srb  = (long)(port->portBits.rowBytes & 0x3FFF);
    scr0 = port->portBits.baseAddr
         + (long)(dst.top - port->portBits.bounds.top) * srb
         + (long)(dst.left - port->portBits.bounds.left) / 8;

    fpsMark = TickCount();
    for (;;) {
        long n; int type; long vlen, vp; short r;

        if (GetNextEvent(everyEvent, &ev) &&
            (ev.what == keyDown || ev.what == mouseDown)) break;

        /* pace: only advance a frame when the audio queue has room (audio is
         * the clock).  With no audio, pace to header fps by TickCount. */
        if (gChan) { if (chan_pending() >= QDEPTH) continue; }

        /* --- read one frame record --- */
        if (isMOV2) {
            unsigned char th[5];
            n = 5; err = FSRead(ref, &n, th);
            if (err != noErr || n < 5) goto loop_rewind;
            type = th[0];
            vlen = ((long)th[1]<<24)|((long)th[2]<<16)|((long)th[3]<<8)|th[4];
            n = vlen; err = FSRead(ref, &n, vidTmp);
            if (err != noErr || n < vlen) goto loop_rewind;
            if (type == 1) {                    /* keyframe: whole image */
                BlockMove(vidTmp, buf, fbytes);
                for (r = 0; r < h; r++) dirty[r] = 1;
            } else {                            /* delta: apply changed runs */
                for (r = 0; r < h; r++) dirty[r] = 0;
                vp = 0;
                while (vp + 4 <= vlen) {
                    long off = ((long)(unsigned char)vidTmp[vp]<<8)|(unsigned char)vidTmp[vp+1];
                    long ln  = ((long)(unsigned char)vidTmp[vp+2]<<8)|(unsigned char)vidTmp[vp+3];
                    vp += 4;
                    if (off + ln > fbytes) break;
                    BlockMove(vidTmp + vp, buf + off, ln);
                    { long r0 = off / rowb, r1 = (off + ln - 1) / rowb, rr;
                      for (rr = r0; rr <= r1 && rr < h; rr++) dirty[rr] = 1; }
                    vp += ln;
                }
            }
            if (flags & 1) {                    /* this frame's audio chunk */
                unsigned char achunk[2048];
                n = gAperf; FSRead(ref, &n, achunk);
                if (gChan) queue_audio(achunk);
            }
        } else {                                /* MOV1: full frame, no delta */
            n = fbytes; err = FSRead(ref, &n, buf);
            if (err != noErr || n < fbytes) goto loop_rewind;
            for (r = 0; r < h; r++) dirty[r] = 1;
        }

        /* --- blit changed rows straight to screen memory --- */
        for (r = 0; r < h; r++)
            if (dirty[r])
                BlockMove(buf + (long)r * rowb, scr0 + (long)r * srb, rowb);

        /* pace MOV1 (no audio clock) to header fps */
        if (!gChan && fps > 0) {
            static unsigned long t0v = 0;
            unsigned long due;
            if (t0v == 0) t0v = TickCount();
            due = t0v + ((unsigned long)played * 60 + fps - 1) / fps;
            while (TickCount() < due) {}
        }

        i++; played++; frames++;
        if (TickCount() - fpsMark >= 60) {
            char b[7]; short k; Str255 pp;
            curfps = frames; frames = 0; fpsMark += 60;
            b[0]='f';b[1]='p';b[2]='s';b[3]=' ';
            b[4]=(char)('0'+(curfps/10)%10);b[5]=(char)('0'+curfps%10);
            for(k=0;k<6;k++)pp[k+1]=b[k]; pp[0]=6;
            SetPort(gWin); ForeColor(whiteColor);
            MoveTo(dst.left, dst.top-4); DrawString(pp);
        }
        if (i >= frameCount) {
loop_rewind:
            SetFPos(ref, fsFromStart, dataStart);
            i = 0; played = 0;
            for (r = 0; r < h; r++) buf[r*rowb] = buf[r*rowb]; /* keep buf; frame0 is keyframe */
            continue;
        }
    }

    if (gChan) SndDisposeChannel(gChan, true);
    if (gAudio) DisposePtr(gAudio);
    DisposePtr(vidTmp); DisposePtr(buf);
closed:
    FSClose(ref);
done:
    ShowCursor(); DisposeWindow(gWin);
    return 0;
}
