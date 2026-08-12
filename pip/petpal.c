/*
 * Pip - a desktop companion for the Classic Macintosh
 * ---------------------------------------------------
 * A tiny original creature that lives in a corner of your screen and shares
 * the workspace. It breathes, blinks, looks around, follows your cursor,
 * naps when you're away, gets livelier during the day, and sleeps at night.
 * Click it to pet it; press F to feed it.
 *
 * Architecture: a background-capable application (see pet.r 'SIZE' resource)
 * with a small draggable window parked in a screen corner. It yields the CPU
 * generously through WaitNextEvent (long sleep) so it stays near-idle while you
 * work in other apps, and double-buffers to an offscreen bitmap.
 *
 * This is v1: alive first. Internal needs (energy/playfulness/boredom) drift and
 * bias behavior, but saving state to a prefs file between launches is the next
 * pass (see DEVLOG.md). Built with Retro68.
 */

#include <Quickdraw.h>
#include <MacTypes.h>
#include <Windows.h>
#include <Fonts.h>
#include <Events.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <ToolUtils.h>
#include <OSUtils.h>
#include <Memory.h>
#include <Files.h>
#include <Sound.h>
#include <string.h>
#include "pip_snd.h"
#include "pip_speech.h"

/* For the WaitNextEvent availability check (Traps.h isn't present here). */
#ifndef ToolTrap
#define ToolTrap 1
#endif

#define WIN_W 76
#define WIN_H 84

enum { ST_IDLE = 0, ST_ALERT, ST_DROWSY, ST_SLEEP, ST_HAPPY, ST_EAT };

static GrafPort gOff; static BitMap gOffBits;
static WindowPtr gWin;
static Pattern patBody, patBelly;

static short gState = ST_IDLE, gPrev = -1;
static long  gAnim = 0;                 /* animation tick counter        */
static short gBob, gBlink, gLookX, gLookY, gProp, gMouth, gForceBlink = 0;
static long  gStateTimer = 0;           /* ticks remaining in temp state */
static long  gLastActive;               /* TickCount of last user action */
static Point gLastMouse;
static Boolean gInBack = false, gQuit = false, gDirty = true, gHasWNE = true;

/* needs 0..100 */
static short gEnergy = 80, gPlay = 50, gBored = 20;
static long  gNeedMark;

static unsigned long rs = 12345UL;
static short rnd(short n){ rs = rs*1103515245UL + 12345UL; return (short)(((rs>>16)&0x7fff)%(n>0?n:1)); }
static void setPat(Pattern*p,const unsigned char*b){ memcpy(p,b,8); }

/* ---- Pip's voice: one sampled Sound Manager channel, short cute clips ---- */
#ifndef initMono
#define initMono 0
#endif
static SndChannelPtr gChan;
static SoundHeader   gHdr[4];             /* ring; headers outlive the command */
static int           gHdrN;

static void snd_init(void)
{
    if (SndNewChannel(&gChan, sampledSynth, initMono, NULL) != noErr) gChan = 0;
}
/* play any 8-bit sampled buffer at a given rate (frog clips 11k, speech 8k) */
static void play_buf(const unsigned char *data, long len, unsigned long rate)
{
    SndCommand   c;
    SoundHeader *h;
    if (!gChan) return;
    c.cmd = flushCmd; c.param1 = 0; c.param2 = 0; SndDoImmediate(gChan, &c);
    c.cmd = quietCmd;                              SndDoImmediate(gChan, &c);
    h = &gHdr[gHdrN]; gHdrN = (gHdrN + 1) & 3;
    memset(h, 0, sizeof *h);
    h->samplePtr     = (Ptr)data;
    h->length        = (unsigned long)len;
    h->sampleRate    = rate;
    h->encode        = stdSH;
    h->baseFrequency = 60;
    c.cmd = bufferCmd; c.param1 = 0; c.param2 = (long)h;
    SndDoCommand(gChan, &c, true);
}
static void snd_play(int id)
{
    if (id < 0 || id >= SND_COUNT) return;
    play_buf(pip_clips[id].data, pip_clips[id].len, PIP_SND_RATE);
}

/* liveliness state */
static short gHop = 0;                     /* little jump counter */
static long  gNextSpont;                   /* TickCount of next spontaneous act */
static Boolean gCursorNear = false;        /* cursor was near Pip last tick */
static short gTreat = 0;                    /* which food (0=fly 1=cookie 2=cake) */
static short gTrick = 0;                    /* trick spin counter */
static short gTongue = 0;                   /* tongue-flick counter (fly catch) */

/* a fly buzzes in now and then; Pip's tongue flicks out to catch it */
static Boolean gFly = false;
static short gFlyX, gFlyY, gFlyVX, gFlyT;
static long  gNextFly;

/* ---- extra charm: idle variety, moods, wandering, hourly chime -------- */
static short gYawn = 0;                 /* yawn animation counter (wide mouth) */
static short gStretch = 0;              /* stretch-up counter */
static short gScratch = 0;              /* hind-leg scratch counter */
static short gGrump = 0;                /* grumpy sulk counter (frown, look away) */
static short gStartle = 0;              /* startled counter ("!" + big jump) */
static short gPetHits = 0;              /* pets inside the current window */
static long  gPetWinEnd = 0;            /* when the rapid-pet window closes */
static short gLastHour = -1;            /* hour of the last on-the-hour chime */
/* wandering: Pip hops across the desktop to a new spot now and then */
static Boolean gWander = false;
static short   gWanderTX, gWanderTY;    /* target window top-left (global) */
static long    gNextWander;

/* ---- speech bubble + pending "ponder then answer" -------------------- */
static WindowPtr gBubble = NULL;
static long      gBubbleUntil = 0;
static char      gBubbleText[256];
static const Speech *gPend = NULL;         /* fortune to speak after a beat */
static long          gPendAt = 0;

static void draw_bubble(void)
{
    Rect r; short y; const char *p;
    if (!gBubble) return;
    SetPort(gBubble);
    r = gBubble->portRect;
    EraseRect(&r);
    FrameRoundRect(&r, 14, 14);
    TextFont(3); TextSize(9); TextFace(0);   /* Geneva 9 */
    y = 15; p = gBubbleText;
    while (*p) {
        char line[80]; short n = 0;
        while (*p && *p != '\r' && n < 79) line[n++] = *p++;
        MoveTo(9, y); DrawText(line, 0, n);
        if (*p == '\r') p++;
        y += 12;
    }
}

static void show_bubble(const char *text)
{
    short nlines = 1, w = 0, maxw = 0, i, bw, bh, x, ytop;
    Point tl;
    strncpy(gBubbleText, text, 255); gBubbleText[255] = 0;
    for (i = 0; text[i]; i++) {
        if (text[i] == '\r') { if (w > maxw) maxw = w; w = 0; nlines++; }
        else w++;
    }
    if (w > maxw) maxw = w;
    bw = maxw * 6 + 22; if (bw < 90) bw = 90; if (bw > 250) bw = 250;
    bh = nlines * 12 + 12;

    /* place above Pip; if no room, place to the left */
    SetPort(gWin); tl.h = 0; tl.v = 0; LocalToGlobal(&tl);
    x = tl.h + WIN_W/2 - bw/2;
    ytop = tl.v - bh - 6;
    if (ytop < 22) { ytop = tl.v; x = tl.h - bw - 6; }     /* to the left */
    if (x < 4) x = 4;
    if (x + bw > qd.screenBits.bounds.right) x = qd.screenBits.bounds.right - bw - 2;

    if (gBubble) { DisposeWindow(gBubble); gBubble = NULL; }
    { Rect r; SetRect(&r, x, ytop, x + bw, ytop + bh);
      gBubble = NewWindow(NULL, &r, "\p", true, plainDBox, (WindowPtr)-1L, false, 0); }
    draw_bubble();
    gBubbleUntil = TickCount() + 240 + (long)nlines * 80;   /* ~5-8s */
}

static void speak(const Speech *s)
{
    if (!s) return;
    play_buf(s->data, s->len, SPEECH_RATE);
    show_bubble(s->text);
}
static void sayHello(void);                /* defined below; used in animate() */

/* ---- offscreen --------------------------------------------------------- */

static Boolean makeOff(void)
{
    Rect b; short rb; long sz;
    SetRect(&b,0,0,WIN_W,WIN_H);
    rb = ((WIN_W+15)/16)*2; sz=(long)rb*WIN_H;
    gOffBits.baseAddr = NewPtr(sz); if(!gOffBits.baseAddr) return false;
    gOffBits.rowBytes = rb; gOffBits.bounds = b;
    OpenPort(&gOff); SetPortBits(&gOffBits);
    gOff.portRect=b; ClipRect(&b); RectRgn(gOff.visRgn,&b); EraseRect(&b);
    return true;
}
static void blit(void){ Rect s=gOffBits.bounds; SetPort(gWin); CopyBits(&gOffBits,&gWin->portBits,&s,&gWin->portRect,srcCopy,NULL); }

/* ---- the creature ------------------------------------------------------ */

static void drawPet(void)
{
    short cx=WIN_W/2, cy=52+gBob, bw=28, bh=17;
    short elx=cx-13, erx=cx+13, ey=cy-bh, eR=8;
    short lpx,lpy,rpx,rpy;
    Rect r; PolyHandle p;

    SetPort(&gOff); PenNormal();
    r=gOffBits.bounds; EraseRect(&r);

    /* haunches (big folded back legs) */
    SetRect(&r,cx-bw-1,cy-1,cx-bw+18,cy+bh+5); FillOval(&r,&patBody); FrameOval(&r);
    SetRect(&r,cx+bw-18,cy-1,cx+bw+1,cy+bh+5); FillOval(&r,&patBody); FrameOval(&r);

    /* front feet with little webbed toes */
    SetRect(&r,cx-17,cy+bh-2,cx-3,cy+bh+4); PaintOval(&r);
    MoveTo(cx-14,cy+bh+4);LineTo(cx-14,cy+bh+7); MoveTo(cx-10,cy+bh+4);LineTo(cx-10,cy+bh+8); MoveTo(cx-6,cy+bh+4);LineTo(cx-6,cy+bh+7);
    SetRect(&r,cx+3,cy+bh-2,cx+17,cy+bh+4); PaintOval(&r);
    MoveTo(cx+14,cy+bh+4);LineTo(cx+14,cy+bh+7); MoveTo(cx+10,cy+bh+4);LineTo(cx+10,cy+bh+8); MoveTo(cx+6,cy+bh+4);LineTo(cx+6,cy+bh+7);

    /* body: wide, dithered, pale belly, outline, a few frog spots */
    SetRect(&r,cx-bw,cy-bh,cx+bw,cy+bh); FillOval(&r,&patBody);
    SetRect(&r,cx-bw+5,cy+1,cx+bw-5,cy+bh); FillOval(&r,&patBelly);
    { Rect b; SetRect(&b,cx-15,cy+3,cx+15,cy+bh-1); FillOval(&b,&qd.white); }
    SetRect(&r,cx-bw,cy-bh,cx+bw,cy+bh); FrameOval(&r);
    SetRect(&r,cx-18,cy-9,cx-12,cy-4); PaintOval(&r);
    SetRect(&r,cx+11,cy-6,cx+17,cy-1); PaintOval(&r);
    SetRect(&r,cx-3,cy-11,cx+3,cy-7); PaintOval(&r);

    /* bulging eyes on top of the head */
    SetRect(&r,elx-eR,ey-eR,elx+eR,ey+eR); FillOval(&r,&patBody); FrameOval(&r);
    SetRect(&r,erx-eR,ey-eR,erx+eR,ey+eR); FillOval(&r,&patBody); FrameOval(&r);

    if (gBlink) {
        MoveTo(elx-eR+2,ey); LineTo(elx+eR-2,ey);
        MoveTo(erx-eR+2,ey); LineTo(erx+eR-2,ey);
    } else if (gState==ST_SLEEP) {
        MoveTo(elx-eR+2,ey+1); LineTo(elx,ey-2); LineTo(elx+eR-2,ey+1);
        MoveTo(erx-eR+2,ey+1); LineTo(erx,ey-2); LineTo(erx+eR-2,ey+1);
    } else {
        short ew = gStartle ? 8 : 6;                     /* startled -> big wide eyes */
        short pr = gStartle ? 2 : 3;                      /* ...with pinprick pupils */
        SetRect(&r,elx-ew,ey-ew,elx+ew,ey+ew); FillOval(&r,&qd.white); FrameOval(&r);
        SetRect(&r,erx-ew,ey-ew,erx+ew,ey+ew); FillOval(&r,&qd.white); FrameOval(&r);
        /* derpy wall-eyed pupils that drift independently */
        lpx=elx+gLookX-1; rpx=erx+gLookX+1; lpy=ey+gLookY; rpy=ey+gLookY;
        if(lpx<elx-3)lpx=elx-3; if(lpx>elx+3)lpx=elx+3;
        if(rpx<erx-3)rpx=erx-3; if(rpx>erx+3)rpx=erx+3;
        if(lpy<ey-3)lpy=ey-3; if(lpy>ey+3)lpy=ey+3;
        if(rpy<ey-3)rpy=ey-3; if(rpy>ey+3)rpy=ey+3;
        SetRect(&r,lpx-pr,lpy-pr,lpx+pr,lpy+pr); PaintOval(&r);
        SetRect(&r,rpx-pr,rpy-pr,rpx+pr,rpy+pr); PaintOval(&r);
        if (gGrump) {                                     /* heavy half-lids = sulking */
            MoveTo(elx-6,ey-2); LineTo(elx+6,ey-1);
            MoveTo(erx-6,ey-1); LineTo(erx+6,ey-2);
        }
    }

    /* nostrils */
    SetRect(&r,cx-3,cy-bh+2,cx-1,cy-bh+4); PaintOval(&r);
    SetRect(&r,cx+2,cy-bh+2,cx+4,cy-bh+4); PaintOval(&r);

    /* big derpy wide mouth */
    if (gMouth==2) {                                     /* open + tongue (eating) */
        SetRect(&r,cx-20,cy-1,cx+20,cy+13); FillOval(&r,&qd.black); FrameOval(&r);
        SetRect(&r,cx-4,cy+5,cx+6,cy+15); FillOval(&r,&patBelly); FrameOval(&r);
    } else if (gMouth==3) {                              /* wide YAWN (tall O) */
        SetRect(&r,cx-14,cy-2,cx+14,cy+18); FillOval(&r,&qd.black); FrameOval(&r);
    } else if (gMouth==4) {                              /* frown (grumpy, curls down) */
        MoveTo(cx-16,cy+7);
        LineTo(cx-8,cy+2);
        LineTo(cx,cy+1);
        LineTo(cx+8,cy+2);
        LineTo(cx+16,cy+7);
    } else {
        short up = (gMouth==1)?6:2;                      /* goofy grin, curls up when happy */
        MoveTo(cx-20,cy+1);
        LineTo(cx-11,cy+1+up/2);
        LineTo(cx,cy+2+up);
        LineTo(cx+11,cy+1+up/2);
        LineTo(cx+20,cy+1);
    }

    /* props */
    if (gProp==1) {                                     /* Zzz */
        TextFont(0); TextSize(9);
        MoveTo(erx+8,ey-6);  DrawChar('z');
        MoveTo(erx+13,ey-12); DrawChar('Z');
    } else if (gProp==2) {                               /* heart */
        SetRect(&r,cx+14,cy-24,cx+20,cy-19); PaintOval(&r);
        SetRect(&r,cx+18,cy-24,cx+24,cy-19); PaintOval(&r);
        p=OpenPoly(); MoveTo(cx+14,cy-21); LineTo(cx+19,cy-13); LineTo(cx+24,cy-21); ClosePoly(); FillPoly(p,&qd.black); KillPoly(p);
    } else if (gProp==3) {                               /* a treat */
        if (gTreat==1) {                                 /* cookie: round + chips */
            SetRect(&r,cx-8,cy+bh+2,cx+8,cy+bh+10); FillOval(&r,&patBelly); FrameOval(&r);
            SetRect(&r,cx-4,cy+bh+4,cx-2,cy+bh+6); PaintOval(&r);
            SetRect(&r,cx+2,cy+bh+5,cx+4,cy+bh+7); PaintOval(&r);
        } else if (gTreat==2) {                          /* cake: a slice */
            SetRect(&r,cx-7,cy+bh+4,cx+7,cy+bh+11); FrameRect(&r); FillRect(&r,&patBelly);
            MoveTo(cx-7,cy+bh+6); LineTo(cx+7,cy+bh+6);   /* frosting line */
            SetRect(&r,cx-1,cy+bh,cx+1,cy+bh+4); PaintRect(&r); /* candle */
        } else {                                         /* a fly */
            SetRect(&r,cx-2,cy+bh+4,cx+2,cy+bh+8); PaintOval(&r);
            MoveTo(cx-4,cy+bh+4); LineTo(cx-2,cy+bh+6);
            MoveTo(cx+4,cy+bh+4); LineTo(cx+2,cy+bh+6);
        }
    } else if (gProp==4) {                               /* startled "!" */
        TextFont(0); TextSize(12); TextFace(bold);
        MoveTo(erx+7, ey-4); DrawChar('!');
        TextFace(0);
    } else if (gProp==5) {                               /* grumpy anger puff */
        MoveTo(erx+7,ey-11); LineTo(erx+11,ey-7);
        MoveTo(erx+11,ey-11); LineTo(erx+7,ey-7);
        MoveTo(erx+13,ey-5); LineTo(erx+16,ey-2);
    }

    /* a hind foot flicks up to scratch behind an ear */
    if (gScratch > 0) {
        short sxx = cx + 21, syy = cy - 4 + ((gScratch & 1) ? -3 : 3);
        PenSize(2,2);
        MoveTo(cx+17, cy+bh-3); LineTo(sxx, syy);        /* raised hind leg */
        PenSize(1,1);
        SetRect(&r, sxx-2, syy-2, sxx+2, syy+2); PaintOval(&r);   /* webbed foot */
    }

    /* a buzzing fly, and Pip's tongue flicking out to snag it */
    if (gTongue > 0) {                                   /* tongue: mouth -> fly */
        PenSize(2,2);
        MoveTo(cx, cy+6); LineTo(gFlyX, gFlyY);
        SetRect(&r, gFlyX-2, gFlyY-2, gFlyX+2, gFlyY+2); PaintOval(&r);  /* tongue tip */
        PenSize(1,1);
    } else if (gFly) {
        SetRect(&r, gFlyX-1, gFlyY-1, gFlyX+2, gFlyY+2); PaintOval(&r);  /* body */
        if (gFlyT & 1) {                                 /* flapping wings */
            MoveTo(gFlyX-3, gFlyY-2); LineTo(gFlyX-1, gFlyY);
            MoveTo(gFlyX+3, gFlyY-2); LineTo(gFlyX+1, gFlyY);
        }
    }
}

/* ---- behavior ---------------------------------------------------------- */

static short hourNow(void)
{
    /* Read the low-memory global Time (seconds since 1904, local) directly.
       Avoids GetTime glue that Retro68 doesn't link. Hour of day = (s mod day)/hr. */
    unsigned long now = *(volatile unsigned long *)0x020CL;
    return (short)((now % 86400UL) / 3600UL);
}

static void updateNeeds(void)
{
    long now = TickCount();
    if (now - gNeedMark < 3600) return;               /* ~once/minute */
    gNeedMark = now;
    { short h = hourNow(); Boolean night = (h >= 22 || h < 6);
      if (night) { if (gEnergy < 100) gEnergy++; }
      else       { if (gEnergy > 0)   gEnergy--; }
    }
    if (gBored < 100) gBored++;
    if (gPlay  > 0)   gPlay--;
}

static void chooseState(void)
{
    long idle = TickCount() - gLastActive;
    short h = hourNow();
    Boolean night = (h >= 22 || h < 6);

    short before = gState;

    if (gStateTimer > 0) return;                        /* HAPPY/EAT run out first */

    if (night)                 gState = ST_SLEEP;
    else if (idle > 3600)      gState = ST_DROWSY;      /* away ~1 min -> drowsy */
    else if (idle < 90)        gState = ST_ALERT;       /* active -> perky       */
    else                       gState = ST_IDLE;

    /* woke up because you came back: a happy little greeting */
    if ((before == ST_SLEEP || before == ST_DROWSY) &&
        (gState == ST_ALERT || gState == ST_IDLE)) {
        snd_play(SND_GREET); gForceBlink = 3; gHop = 5;
    }
}

static void lookAtCursor(void)
{
    Point m; SetPort(gWin); GetMouse(&m);
    gLookX = (m.h < WIN_W/2 - 3) ? -3 : (m.h > WIN_W/2 + 3) ? 3 : 0;
    gLookY = (m.v < 30) ? -3 : (m.v > 42) ? 3 : 0;
}

static void animate(void)
{
    gAnim++;
    gProp = 0; gMouth = 0; gBlink = 0;

    if (gStateTimer > 0) gStateTimer--;

    switch (gState) {
    case ST_SLEEP:
        gBob = (gAnim/8) & 1;                           /* slow breathing */
        gMouth = 0; gProp = (gAnim/6 & 3) ? 1 : 0; gLookX=gLookY=0;
        break;
    case ST_DROWSY:
        gBob = (gAnim/6) & 1;
        gBlink = (gAnim % 10) < 5;                      /* half-closed */
        gLookX=0; gLookY=1;
        break;
    case ST_IDLE:
        gBob = (gAnim/5) & 1;                           /* breathing */
        gBlink = (gAnim % 24) == 0;                     /* occasional blink */
        lookAtCursor();                                 /* eyes follow the cursor */
        break;
    case ST_ALERT:
        gBob = (gAnim/4) & 1;
        gBlink = (gAnim % 40) == 0;
        lookAtCursor();                                 /* eyes follow the cursor */
        gMouth = (gPlay > 60) ? 1 : 0;
        break;
    case ST_HAPPY:
        gBob = -((gAnim/2) & 3);                        /* bouncing */
        gMouth = 1; gProp = 2; gLookX=gLookY=0;
        if (gStateTimer == 0) gState = ST_IDLE;
        break;
    case ST_EAT:
        gBob = (gAnim/3) & 1;
        gMouth = (gAnim & 2) ? 2 : 0; gProp = 3;
        if (gStateTimer == 0) { if ((gEnergy += 30) > 100) gEnergy = 100; gPlay += 10; gState = ST_HAPPY; gStateTimer = 40; }
        break;
    }
    if (gForceBlink > 0) { gBlink = 1; gForceBlink--; }   /* blink when clicked */

    /* ---- liveliness: makes Pip feel awake, not just breathing ---- */
    if (gState == ST_IDLE || gState == ST_ALERT) {
        Point m; short dx, dy; Boolean near;
        SetPort(gWin); GetMouse(&m);
        dx = m.h - WIN_W/2; dy = m.v - WIN_H/2;
        near = (long)dx*dx + (long)dy*dy < 68L*68L;
        if (near && !gCursorNear) {                       /* you just came over */
            gState = ST_ALERT; gForceBlink = 2; gMouth = 1;
            if (rnd(3) == 0) sayHello();                   /* sometimes in English */
            else snd_play(SND_GREET);
        }
        gCursorNear = near;

        if (gStateTimer == 0 && TickCount() >= gNextSpont && !gBubble &&
            !gYawn && !gStretch && !gScratch && !gGrump && !gStartle) {
            gNextSpont = TickCount() + 480 + rnd(600);    /* every ~8-18s */
            /* mood-biased quirks first: sleepy -> yawn, bored -> scratch */
            if (gEnergy < 35 && rnd(2))      { gYawn = 12; snd_play(SND_RIBBIT); }
            else if (gBored > 70 && rnd(2))  { gScratch = 10; }
            else switch (rnd(8)) {
            case 0:  gHop = 8; gMouth = 1; snd_play(SND_CHIRP); break;
            case 1:  gMouth = 2; snd_play(SND_RIBBIT); break;      /* throat puff */
            case 2:  gLookX = rnd(2) ? 4 : -4; gForceBlink = 3; break;
            case 3:  gMouth = 1; gHop = 4; sayHello(); break;      /* speaks English */
            case 4:  gStretch = 12; break;                        /* a big stretch */
            case 5:  gScratch = 10; break;                        /* scratch an ear */
            case 6:  gYawn = 12; break;                           /* a little yawn */
            default: gMouth = 1; gBlink = 1; break;               /* silent grin */
            }
        }
    } else {
        gCursorNear = false;
    }

    /* --- the fly: buzzes in, Pip's tongue snaps out to eat it --- */
    if (gTongue > 0) { gTongue--; if (gTongue == 0) gFly = false; }
    else if (gFly) {
        gFlyT++;
        gFlyX += gFlyVX;
        gFlyY += (gFlyT & 2) ? 1 : -1;                    /* buzzy wobble */
        if (gFlyY < 8) gFlyY = 8; if (gFlyY > WIN_H-8) gFlyY = WIN_H-8;
        if (gFlyX < 4 || gFlyX > WIN_W-4) gFly = false;   /* escaped */
        else if (gState != ST_SLEEP && gState != ST_DROWSY) {
            short mx = WIN_W/2, my = 52+6, dfx = gFlyX-mx, dfy = gFlyY-my;
            if ((long)dfx*dfx + (long)dfy*dfy < 24L*24L && (gFlyT & 1)) {
                gTongue = 4; gState = ST_ALERT; gMouth = 2; /* SNAP! */
                snd_play(SND_NOM);
                if (rnd(2)) speak(&sp_gotcha_c);
                if ((gPlay += 8) > 100) gPlay = 100; gBored = 0;
            }
        }
    } else if ((gState == ST_IDLE || gState == ST_ALERT) &&
               gStateTimer == 0 && TickCount() >= gNextFly && !gBubble) {
        gFly = true; gFlyT = 0;                            /* a new fly enters */
        gFlyVX = rnd(2) ? 2 : -2;
        gFlyX = (gFlyVX > 0) ? 4 : WIN_W-4;
        gFlyY = 14 + rnd(20);
        gNextFly = TickCount() + 900 + rnd(1200);         /* every ~15-35s */
    }

    if (gTrick > 0) {                                     /* a flip-and-spin trick */
        gTrick--;
        gBob = -(short)((gTrick < 8 ? gTrick : 16 - gTrick) * 2);  /* up then down */
        gLookX = (gTrick & 2) ? 4 : -4;                   /* eyes whirl */
        gLookY = (gTrick & 4) ? 3 : -3;
        gMouth = 1; gProp = 2;
    }
    if (gHop > 0) { gBob -= gHop; gHop--; }               /* the little jump arc */

    /* ---- charm overlays: applied last so they win the frame ---- */
    if (gYawn > 0) {                                      /* a big slow yawn */
        gYawn--;
        gMouth = 3; gBlink = 1;                           /* wide open, eyes squeezed */
        gBob = (gYawn > 6) ? -2 : 0;                      /* head tips back, settles */
    }
    if (gStretch > 0) {                                   /* a luxurious stretch up */
        gStretch--;
        gBob = -(short)(gStretch < 6 ? gStretch : 12 - gStretch);
        gMouth = 1;
    }
    if (gScratch > 0) { gScratch--; gMouth = 1; }         /* hind foot drawn in drawPet */
    if (gGrump > 0) {                                     /* sulking: frown, look away */
        gGrump--;
        gMouth = 4; gProp = 5; gBlink = 0;
        gLookX = 4; gLookY = -2;                          /* pointedly looks aside */
        gBob = (gAnim/6) & 1;
        if (gGrump == 0) { gForceBlink = 2; snd_play(SND_CHIRP); }  /* forgiven */
    }
    if (gStartle > 0) {                                   /* wide-eyed shock + big hop */
        gStartle--;
        gProp = 4; gMouth = 1;
        gBob -= (gStartle > 4) ? 10 : gStartle;
    }

    /* ---- wandering: Pip hops across the desktop to a new spot ---- */
    if (gWander) {
        Point tl; short dx, dy, step = 7;
        SetPort(gWin); tl.h = 0; tl.v = 0; LocalToGlobal(&tl);
        dx = gWanderTX - tl.h; dy = gWanderTY - tl.v;
        if (dx > -6 && dx < 6 && dy > -6 && dy < 6) {
            gWander = false; gMouth = 1; gHop = 3;        /* arrived, happy hop */
        } else {
            short nh = tl.h + (dx > step ? step : dx < -step ? -step : dx);
            short nv = tl.v + (dy > step ? step : dy < -step ? -step : dy);
            MoveWindow(gWin, nh, nv, false);
            if (gHop == 0) gHop = 5;                       /* hop along as it travels */
            gMouth = 1;
        }
    } else if (gState == ST_IDLE && gStateTimer == 0 && !gBubble && !gFly &&
               !gGrump && !gYawn && TickCount() >= gNextWander) {
        short sr = qd.screenBits.bounds.right, sb = qd.screenBits.bounds.bottom;
        gWanderTX = 8 + rnd(sr - WIN_W - 16);
        gWanderTY = 26 + rnd(sb - WIN_H - 46);
        gWander = true; gHop = 5; snd_play(SND_CHIRP);
        gNextWander = TickCount() + 2400 + rnd(2400);     /* every ~40-80s */
    }

    gDirty = true;
}

static void petIt(void)
{
    long now = TickCount();
    gLastActive = now;
    /* poke a sleeping/drowsy Pip and he startles awake, wide-eyed */
    if (gState == ST_SLEEP || gState == ST_DROWSY) {
        gStartle = 9; gState = ST_ALERT; gStateTimer = 22; gHop = 11;
        snd_play(SND_GREET);
        return;
    }
    /* too many pokes too fast and he sulks for a moment */
    if (now < gPetWinEnd) gPetHits++; else { gPetHits = 1; gPetWinEnd = now + 180; }
    if (gPetHits >= 5) {
        gPetHits = 0; gGrump = 45; gState = ST_ALERT; gStateTimer = 45;
        snd_play(SND_RIBBIT); show_bubble("hey! gentle...");
        return;
    }
    gState = ST_HAPPY; gStateTimer = 60; gBored = 0;
    if ((gPlay += 25) > 100) gPlay = 100;
    gForceBlink = 4; gHop = 6; snd_play(SND_CHIRP);
}
static void feedIt(void)                     /* a different treat each time */
{
    gTreat = rnd(N_TREATS);
    gState = ST_EAT; gStateTimer = 60; gLastActive = TickCount();
    speak(&pip_treats[gTreat]);
}
static void doTrick(void)                    /* flip, spin, ta-da! */
{
    gTrick = 16; gState = ST_ALERT; gStateTimer = 24;
    speak(&sp_trick_c); gLastActive = TickCount();
}
static void tellTime(void)                   /* Pip reads the clock and says it */
{
    short h = hourNow();                     /* 0..23 */
    short h12 = h % 12; if (h12 == 0) h12 = 12;
    char msg[40]; short k = 0; const char *d;
    static const char *names[13] = {"","one","two","three","four","five","six",
        "seven","eight","nine","ten","eleven","twelve"};
    d = "It is about "; while (*d) msg[k++]=*d++;
    d = names[h12];     while (*d) msg[k++]=*d++;
    d = " o'clock.";    while (*d) msg[k++]=*d++;
    msg[k] = 0;
    play_buf(pip_time[h12-1].data, pip_time[h12-1].len, SPEECH_RATE);
    show_bubble(msg);
    gState = ST_ALERT; gForceBlink = 2; gLastActive = TickCount();
}

/* Pip the oracle-comedian: speaks real English (see pip_speech.h) */
static void tellJoke(void)
{
    speak(&pip_jokes[rnd(N_JOKES)]);
    gState = ST_HAPPY; gStateTimer = 90; gMouth = 1; gHop = 5;
    gLastActive = TickCount();
}
static void askPip(void)                    /* magic 8-ball: ponder, then answer */
{
    snd_play(SND_RIBBIT);
    show_bubble("Hmm... let me see...");
    gState = ST_ALERT; gMouth = 2; gForceBlink = 2;
    gPend = &pip_fortunes[rnd(N_FORTUNES)];
    gPendAt = TickCount() + 110;            /* ~1.8s thinking beat */
    gLastActive = TickCount();
}
static void sayHello(void) { speak(&pip_greets[rnd(N_GREETS)]); }

/* ---- menu bar ---------------------------------------------------------- */
#define M_APPLE 128
#define M_FILE  129
static void setupMenus(void)
{
    MenuHandle a, f;
    Str255 apple; apple[0] = 1; apple[1] = 0x14;    /* apple-logo char */
    a = NewMenu(M_APPLE, apple);
    AppendMenu(a, "\pAbout Pip…;(-");
    AppendResMenu(a, 'DRVR');                            /* desk accessories */
    InsertMenu(a, 0);
    f = NewMenu(M_FILE, "\pFile");
    AppendMenu(f, "\pTell a Joke/J;Ask Pip/K;Tell the Time/T;Do a Trick/R;(-;"
                  "Pet Pip/P;Feed Pip/F;(-;Quit/Q");
    InsertMenu(f, 0);
    DrawMenuBar();
}
static void doMenu(long sel)
{
    short m = HiWord(sel), it = LoWord(sel);
    if (m == M_APPLE) {
        if (it == 1) show_bubble("I'm Pip, your oracle frog!\rJokes, fortunes, the time,\rtricks -- just ask. Feed me\rand watch me catch flies!");
        else {
            Str255 nm; GrafPtr sp; GetPort(&sp);
            GetMenuItemText(GetMenuHandle(M_APPLE), it, nm);
            OpenDeskAcc(nm); SetPort(sp);
        }
    } else if (m == M_FILE) {
        switch (it) {
        case 1: tellJoke(); break;
        case 2: askPip();   break;
        case 3: tellTime(); break;
        case 4: doTrick();  break;
        case 6: petIt();    break;
        case 7: feedIt();   break;
        case 9: gQuit = true; break;
        }
    }
    HiliteMenu(0);
}

/* ---- persistence (remembers mood between launches) --------------------- */

#define PREF_MAGIC 0x5069
typedef struct { short magic, energy, play, bored; unsigned long saved; } PetPrefs;

static void loadPrefs(void)
{
    short ref; long count; OSErr err; PetPrefs pf;
    if (FSOpen("\pPip Prefs", 0, &ref) != noErr) return;   /* default directory */
    count = sizeof(pf);
    err = FSRead(ref, &count, &pf);
    FSClose(ref);
    if (err == noErr && pf.magic == PREF_MAGIC) {
        unsigned long now = *(volatile unsigned long *)0x020CL;
        long away = (long)(now - pf.saved);
        gEnergy = pf.energy; gPlay = pf.play; gBored = pf.bored;
        if (away > 0) {                                   /* catch up for time gone */
            short mins = (short)(away / 60); if (mins > 240) mins = 240;
            gEnergy -= mins / 8; if (gEnergy < 0) gEnergy = 0;
            gBored  += mins / 6; if (gBored > 100) gBored = 100;
        }
    }
}

static void savePrefs(void)
{
    short ref; long count; PetPrefs pf;
    if (FSOpen("\pPip Prefs", 0, &ref) != noErr) {         /* create it if missing */
        if (Create("\pPip Prefs", 0, 'Pip ', 'pref') != noErr) return;
        if (FSOpen("\pPip Prefs", 0, &ref) != noErr) return;
    }
    pf.magic = PREF_MAGIC; pf.energy = gEnergy; pf.play = gPlay; pf.bored = gBored;
    pf.saved = *(volatile unsigned long *)0x020CL;
    count = sizeof(pf);
    SetFPos(ref, fsFromStart, 0);
    FSWrite(ref, &count, &pf);
    SetEOF(ref, count);
    FSClose(ref);
}

/* ---- main -------------------------------------------------------------- */

int main(void)
{
    EventRecord ev; Rect r; long nextAnim; short sr, sb;
    unsigned char body[8]  = {0xDD,0x77,0xDD,0x77,0xDD,0x77,0xDD,0x77};
    unsigned char belly[8] = {0x88,0x22,0x88,0x22,0x88,0x22,0x88,0x22};

    InitGraf(&qd.thePort); InitFonts(); InitWindows(); InitMenus(); TEInit(); InitDialogs(NULL); InitCursor();

    /* Is WaitNextEvent available? (needs System 7 or System 6 + MultiFinder) */
    gHasWNE = (NGetTrapAddress(0xA860, ToolTrap) != NGetTrapAddress(0xA89F, ToolTrap));

    sr = qd.screenBits.bounds.right; sb = qd.screenBits.bounds.bottom;
    SetRect(&r, sr-WIN_W-14, sb-WIN_H-18, sr-14, sb-18);
    gWin = NewWindow(NULL, &r, "\pPip", true, documentProc, (WindowPtr)-1L, true, 0);
    SetPort(gWin);

    setPat(&patBody, body); setPat(&patBelly, belly);
    if (!makeOff()) { SysBeep(20); return 1; }
    snd_init();                         /* give Pip his voice */
    setupMenus();                       /* Apple + File menu bar */
    loadPrefs();

    gLastActive = TickCount(); GetMouse(&gLastMouse); gNeedMark = TickCount();
    nextAnim = TickCount(); gNextSpont = TickCount() + 240;
    gNextFly = TickCount() + 600;
    gNextWander = TickCount() + 3000;      /* let him settle ~50s before roaming */
    gLastHour = hourNow();                 /* don't chime the moment he wakes up */

    while (!gQuit) {
        Boolean got;
        if (gHasWNE) got = WaitNextEvent(everyEvent, &ev, 15, NULL);
        else { SystemTask(); got = GetNextEvent(everyEvent, &ev); }
        if (got) {
            switch (ev.what) {
            case mouseDown: {
                WindowPtr wp; short part = FindWindow(ev.where, &wp);
                gLastActive = TickCount();
                if (part == inMenuBar) doMenu(MenuSelect(ev.where));
                else if (part == inGoAway && TrackGoAway(wp, ev.where)) gQuit = true;
                else if (part == inDrag) {
                    Point b4; b4.h = ((WindowPtr)wp)->portRect.left;
                    DragWindow(wp, ev.where, &qd.screenBits.bounds);
                    speak(&sp_wheee_c);                  /* wheee, you moved me! */
                    (void)b4;
                }
                else if (part == inContent) petIt();
                break;
            }
            case keyDown: {
                char c = (char)(ev.message & charCodeMask);
                gLastActive = TickCount();
                if (ev.modifiers & cmdKey) doMenu(MenuKey(c));   /* menu cmd-keys */
                else if (c=='f'||c=='F') feedIt();               /* bare keys, kid-friendly */
                else if (c=='j'||c=='J') tellJoke();
                else if (c=='k'||c=='K') askPip();
                else if (c=='t'||c=='T') tellTime();
                else if (c=='r'||c=='R') doTrick();
                break;
            }
            case updateEvt: {
                WindowPtr uw = (WindowPtr)ev.message;
                BeginUpdate(uw);
                if (uw == gBubble) draw_bubble(); else blit();
                EndUpdate(uw);
                break;
            }
            case osEvt:
                if (((ev.message >> 24) & 0xFF) == 1)          /* suspend/resume */
                    gInBack = !(ev.message & 1);
                break;
            }
        }

        /* system-wide activity proxy: did the cursor move? (read in a fixed port) */
        { Point m; SetPort(gWin); GetMouse(&m);
          if (m.h != gLastMouse.h || m.v != gLastMouse.v) { gLastActive = TickCount(); gLastMouse = m; } }

        /* oracle ponder-then-answer, and speech-bubble timeout */
        if (gPend && TickCount() >= gPendAt) {
            speak(gPend); gPend = NULL;
            gState = ST_HAPPY; gStateTimer = 60; gMouth = 1;
        }
        if (gBubble && TickCount() >= gBubbleUntil) {
            DisposeWindow(gBubble); gBubble = NULL;
        }

        if (TickCount() >= nextAnim) {
            nextAnim = TickCount() + 8;                        /* ~7 fps, low CPU */
            { short h = hourNow();                             /* on-the-hour celebration */
              if (gLastHour < 0) gLastHour = h;
              else if (h != gLastHour) {
                  gLastHour = h;
                  if (gState != ST_SLEEP) {                    /* don't wake him at night */
                      gState = ST_HAPPY; gStateTimer = 70; gHop = 8; snd_play(SND_CHIRP);
                      if (!gInBack) {                          /* announce only if up front */
                          tellTime();
                          gState = ST_HAPPY; gStateTimer = 70; gHop = 8;
                      }
                  }
              }
            }
            updateNeeds();
            chooseState();
            animate();
            if (gDirty) { drawPet(); blit(); gDirty = false; }
        }
    }

    savePrefs();
    if (gBubble) DisposeWindow(gBubble);
    if (gChan) SndDisposeChannel(gChan, true);
    if (gOffBits.baseAddr) DisposePtr(gOffBits.baseAddr);
    DisposeWindow(gWin);
    return 0;
}
