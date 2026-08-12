/*
 * cinema.c -- "Cinema": an After Dark module that streams the MOV1 film
 * library (Claude Originals:Movies) as a screensaver TV channel.
 *
 * The Donut/Lum demos could never be modules (megabytes baked into a
 * code resource), but films STREAM: all this module holds is one 8KB
 * frame buffer.  DrawFrame reads the next frame off disk and CopyBits
 * it centered -- the exact pipeline the Movie app proved on hardware.
 * Each film loops until FILM_TICKS elapses (15 min), then the next
 * film takes the screen.  Missing films are skipped; if none open,
 * the screen just stays black.
 *
 * Files are found WITHOUT a volume-name dependency: SysEnvirons gives
 * the System Folder's working directory, PBGetWDInfo turns that into
 * the real boot-volume refnum, and PBHOpen resolves each film by a
 * partial pathname relative to the root (dirID 2).  Same ABI rules as
 * ants.c/show.c (verified against Berkeley's own GraphicsModule_Types.h,
 * which ships on the AD 2.0 disk): pascal entry pinned to _start, no
 * writable globals, state in the storage Handle, params untouched.
 */
#include <MacTypes.h>
#include <Quickdraw.h>
#include <MacMemory.h>
#include <Events.h>
#include <Files.h>
#include <OSUtils.h>
#include "Retro68Runtime.h"

enum { msgInitialize = 0, msgClose = 1, msgBlank = 2, msgDrawFrame = 3 };
#define kModuleError (-1)

#ifndef FILM_TICKS
#define FILM_TICKS 54000L        /* 15 minutes per film */
#endif

#define MAXFRAME 8192L           /* 256x256 1-bit */
#define MOV1MAGIC 0x4D4F5631L    /* 'MOV1' */

static const char kFolder[] = ":Claude Originals:Movies:";
static const char * const kFilmNames[] = {
    "Dragons Heaven", "Gunbuster", "Golden Age of Anime", "Gowcaizer", "Lum",
};
#define NFILMS 5

static const Pattern kBlackPat =
    {{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}};
static const Pattern kWhitePat =
    {{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}};

typedef struct {
    Rect          b;
    short         vRefNum;       /* real boot-volume refnum */
    short         wdErr;         /* PBGetWDInfo result at Initialize */
    short         ref;           /* open film, 0 = none */
    short         filmIdx;
    short         w, h, fps;
    long          frameBytes, frameCount, frame;
    long          played;
    unsigned long start;         /* pacing epoch */
    unsigned long filmStart;     /* rotation epoch */
    short         e1, e2, e3;    /* last open errors, per strategy */
    Ptr           buf;
    BitMap        bm;
    Rect          dst;
    Str255        scratch;       /* pathname assembly */
} State;

static void film_close(State *s)
{
    if (s->ref) { FSClose(s->ref); s->ref = 0; }
}

static void pstr_build(Str255 out, const unsigned char *prefix, short plen,
                       const char *a, const char *b)
{
    short n = 0, k;
    for (k = 0; k < plen; k++) out[1 + n++] = prefix[k];
    for (k = 0; a[k]; k++) out[1 + n++] = (unsigned char)a[k];
    if (b) for (k = 0; b[k]; k++) out[1 + n++] = (unsigned char)b[k];
    out[0] = (unsigned char)n;
}

static OSErr try_open(unsigned char *pname, short vref, long dirid, short *ref)
{
    HParamBlockRec pb;
    OSErr err;
    short i;
    for (i = 0; i < (short)sizeof(pb); i++) ((char *)&pb)[i] = 0;
    pb.ioParam.ioNamePtr = pname;
    pb.ioParam.ioVRefNum = vref;
    pb.fileParam.ioDirID = dirid;
    pb.ioParam.ioPermssn = 1;             /* fsRdPerm */
    err = PBHOpenSync(&pb);
    if (err == noErr) *ref = pb.ioParam.ioRefNum;
    return err;
}

/* Three ways to find a film; real hardware taught us not to trust one.
 * a) partial path from the resolved boot volume's root (dirID 2)
 * b) partial path against the DEFAULT volume/directory
 * c) full pathname "VolName:..." via GetVol's volume name          */
static int film_open(State *s, short idx)
{
    long hdr[4], n;

    s->e1 = s->e2 = s->e3 = 0;
    pstr_build(s->scratch, (const unsigned char *)kFolder,
               (short)(sizeof(kFolder) - 1), kFilmNames[idx], 0);
    s->e1 = try_open(s->scratch, s->vRefNum, 2, &s->ref);
    if (s->e1 != noErr)
        s->e2 = try_open(s->scratch, 0, 0, &s->ref);
    if (s->e1 != noErr && s->e2 != noErr) {
        Str255 vol;
        short  vref;
        s->e3 = -1;
        if (GetVol(vol, &vref) == noErr && vol[0] > 0) {
            short k, m = 0;
            Str255 full;
            for (k = 0; k < vol[0]; k++) full[1 + m++] = vol[1 + k];
            for (k = 0; k < s->scratch[0]; k++) full[1 + m++] = s->scratch[1 + k];
            full[0] = (unsigned char)m;
            s->e3 = try_open(full, 0, 0, &s->ref);
        }
        if (s->e3 != noErr) { s->ref = 0; return 0; }
    }

    n = 16;
    if (FSRead(s->ref, &n, (Ptr)hdr) != noErr || n < 16 ||
        hdr[0] != MOV1MAGIC) { film_close(s); return 0; }
    s->frameCount = hdr[1];
    s->w   = (short)(((unsigned long)hdr[2] >> 16) & 0xFFFF);
    s->h   = (short)(hdr[2] & 0xFFFF);
    s->fps = (short)(((unsigned long)hdr[3] >> 16) & 0xFFFF);
    s->frameBytes = (long)(s->w / 8) * s->h;
    if (s->frameBytes <= 0 || s->frameBytes > MAXFRAME || s->fps <= 0) {
        film_close(s); return 0;
    }
    s->bm.baseAddr = s->buf;
    s->bm.rowBytes = s->w / 8;
    SetRect(&s->bm.bounds, 0, 0, s->w, s->h);
    SetRect(&s->dst,
            (short)((s->b.left + s->b.right - s->w) / 2),
            (short)((s->b.top + s->b.bottom - s->h) / 2),
            (short)((s->b.left + s->b.right + s->w) / 2),
            (short)((s->b.top + s->b.bottom + s->h) / 2));
    s->frame = 0; s->played = 0;
    s->start = s->filmStart = TickCount();
    return 1;
}

/* try each film once, starting at filmIdx; 1 if one opened */
static int film_next(State *s)
{
    short tries;
    film_close(s);
    for (tries = 0; tries < NFILMS; tries++) {
        if (film_open(s, s->filmIdx)) return 1;
        s->filmIdx = (short)((s->filmIdx + 1) % NFILMS);
    }
    return 0;
}

static void str_append(Str255 st, const char *a)
{
    short k;
    for (k = 0; a[k]; k++) st[++st[0]] = (unsigned char)a[k];
}

static void num_append(Str255 st, long v)
{
    char  tmp[12];
    short n = 0, k;
    if (v < 0) { st[++st[0]] = '-'; v = -v; }
    if (v == 0) tmp[n++] = '0';
    while (v > 0) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    for (k = (short)(n - 1); k >= 0; k--) st[++st[0]] = (unsigned char)tmp[k];
}

/* the black box speaks: which open strategy failed with what OSErr */
static void cinema_diag(State *s)
{
    Rect   r;
    Str255 msg;
    msg[0] = 0;
    str_append(msg, "Cinema: films not found.  wd=");
    num_append(msg, s->wdErr);
    str_append(msg, " a="); num_append(msg, s->e1);
    str_append(msg, " b="); num_append(msg, s->e2);
    str_append(msg, " c="); num_append(msg, s->e3);
    SetRect(&r, (short)(s->b.left + 16), (short)(s->b.bottom - 44),
            (short)(s->b.left + 420), (short)(s->b.bottom - 16));
    FillRect(&r, &kWhitePat);
    MoveTo((short)(r.left + 8), (short)(r.bottom - 10));
    DrawString(msg);
}

static void cinema_frame(State *s)
{
    GrafPtr port;
    long n;

    if (s->buf == NULL) return;
    if (s->ref == 0 && !film_next(s)) { cinema_diag(s); return; }

    if (TickCount() - s->filmStart >= (unsigned long)FILM_TICKS) {
        s->filmIdx = (short)((s->filmIdx + 1) % NFILMS);
        if (!film_next(s)) return;
        { Rect r = s->b; FillRect(&r, &kBlackPat); } /* clear letterbox */
    }

    /* pace to header fps: NO busy-wait in a module -- just come back */
    if (TickCount() < s->start + ((unsigned long)s->played * 60 + s->fps - 1)
                                  / s->fps)
        return;

    n = s->frameBytes;
    if (FSRead(s->ref, &n, s->buf) != noErr || n < s->frameBytes) {
        /* EOF (or read error): rewind and restart pacing */
        if (SetFPos(s->ref, fsFromStart, 16) != noErr) { film_close(s); return; }
        s->frame = 0; s->played = 0; s->start = TickCount();
        return;
    }
    GetPort(&port);
    CopyBits(&s->bm, &port->portBits, &s->bm.bounds, &s->dst, srcCopy, NULL);
    s->frame++; s->played++;
    if (s->frame >= s->frameCount) {
        SetFPos(s->ref, fsFromStart, 16);
        s->frame = 0; s->played = 0; s->start = TickCount();
    }
}

pascal OSErr module_main(Handle *storage, RgnHandle blankRgn,
                         short message, void *params) __asm__("_start");

pascal OSErr module_main(Handle *storage, RgnHandle blankRgn,
                         short message, void *params)
{
    RETRO68_RELOCATE();
    (void)params;

    switch (message) {
    case msgInitialize: {
        Handle    h = NewHandle(sizeof(State));
        State    *s;
        SysEnvRec env;
        WDPBRec   wd;
        short     i;
        if (h == NULL) return kModuleError;
        *storage = h;
        s = (State *)*h;
        for (i = 0; i < (short)sizeof(State); i++) ((char *)s)[i] = 0;
        s->b = (**blankRgn).rgnBBox;

        SysEnvirons(1, &env);                 /* System Folder's WD... */
        for (i = 0; i < (short)sizeof(wd); i++) ((char *)&wd)[i] = 0;
        wd.ioVRefNum = env.sysVRefNum;
        wd.ioWDIndex = 0;
        s->wdErr = PBGetWDInfoSync(&wd);
        if (s->wdErr == noErr)
            s->vRefNum = wd.ioWDVRefNum;      /* ...to real volume refnum */
        else
            s->vRefNum = env.sysVRefNum;      /* pre-HFS fallback */

        s->buf = NewPtr(MAXFRAME);
        if (s->buf == NULL) { DisposeHandle(h); *storage = NULL; return kModuleError; }
        break;
    }
    case msgBlank:
        if (*storage) {
            Rect r = (*(State *)**storage).b;
            FillRect(&r, &kBlackPat);         /* theater lights down */
        }
        break;
    case msgDrawFrame:
        if (*storage) {
            HLock(*storage);
            cinema_frame((State *)**storage);
            HUnlock(*storage);
        }
        break;
    case msgClose:
        if (*storage) {
            State *s = (State *)**storage;
            film_close(s);
            if (s->buf) DisposePtr(s->buf);
            DisposeHandle(*storage); *storage = NULL;
        }
        Retro68FreeGlobals();
        break;
    default:
        break;
    }
    return noErr;
}
