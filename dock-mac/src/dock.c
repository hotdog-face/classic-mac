/*
 * Claude Picks -- a home-screen launcher ("dock") for the Mac Classic.
 *
 * Runs as the startup application (the boot blocks' shell name is patched
 * from "Finder" to this app), so on boot you land here instead of the
 * Finder.  A row of app icons sits in a bar along the bottom; click one and
 * it launches.  Because we are the startup app, when that app quits the
 * system relaunches US -- so the launcher is always "home", with no
 * MultiFinder needed.  A little Pip frog lives on the screen for company;
 * a Finder button is in the bar for when you need to browse files.
 */
#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include <Files.h>
#include <OSUtils.h>
#include <SegLoad.h>
#include <Sound.h>
#include <string.h>
#include "dock_icons.h"

#ifndef ToolTrap
#define ToolTrap 1
#endif

static WindowPtr gWin;
static short gW, gH;                 /* screen size */
static Rect  gIconR[N_DOCK];         /* hit rects of the dock icons */
static short gBob, gAnim, gBlink;
static Point gPetC;                  /* Pip mascot center */
static Pattern gGray;                /* 50% desktop pattern */
static unsigned char gPip2[64*8], gPipM2[64*8];   /* Pip icon pre-doubled to 64x64 */

/* Pre-double the 32x32 Pip icon into a 64x64 icon+mask.  CopyMask on this
 * QuickDraw won't stretch (mismatched rects tile/garble), so we scale here
 * and blit with equal rects. */
static void make_pip2(void)
{
    const unsigned char *ic = dock_apps[2].icon, *mk = dock_apps[2].icon + 128;
    short row, col, b;
    for (row = 0; row < 32; row++) {
        unsigned char di[8], dm[8];
        for (b = 0; b < 8; b++) { di[b] = 0; dm[b] = 0; }
        for (col = 0; col < 32; col++) {
            short sbyte = row*4 + col/8, sbit = 7 - (col&7);
            short d0 = col*2, d1 = col*2 + 1;
            if (ic[sbyte] & (1<<sbit)) { di[d0>>3] |= 1<<(7-(d0&7)); di[d1>>3] |= 1<<(7-(d1&7)); }
            if (mk[sbyte] & (1<<sbit)) { dm[d0>>3] |= 1<<(7-(d0&7)); dm[d1>>3] |= 1<<(7-(d1&7)); }
        }
        for (b = 0; b < 8; b++) {
            gPip2[(row*2)*8+b]   = di[b]; gPip2[(row*2+1)*8+b]   = di[b];
            gPipM2[(row*2)*8+b]  = dm[b]; gPipM2[(row*2+1)*8+b]  = dm[b];
        }
    }
}

/* ---- draw a 32x32 icon (256B: 128 icon + 128 mask) through its mask ---- */
static void draw_icon(const unsigned char *data, short x, short y)
{
    BitMap ib, mb; Rect sr, dr;
    ib.baseAddr = (Ptr)data;        ib.rowBytes = 4; SetRect(&ib.bounds, 0,0,32,32);
    mb.baseAddr = (Ptr)(data + 128); mb.rowBytes = 4; SetRect(&mb.bounds, 0,0,32,32);
    SetRect(&sr, 0,0,32,32);
    SetRect(&dr, x, y, x+32, y+32);
    CopyMask(&ib, &mb, &gWin->portBits, &sr, &sr, &dr);
}

/* ---- the little Pip frog, drawn 2x from its icon, with a gentle bob ---- */
static void draw_pip(void)
{
    BitMap ib, mb; Rect sr, dr, clear;
    short y = gPetC.v - 32 + gBob;
    /* erase the whole area the frog can occupy (bob range) first, else he smears */
    SetRect(&clear, gPetC.h - 34, gPetC.v - 68, gPetC.h + 34, gPetC.v + 34);
    FillRect(&clear, &gGray);
    ib.baseAddr = (Ptr)gPip2;  ib.rowBytes = 8; SetRect(&ib.bounds, 0,0,64,64);
    mb.baseAddr = (Ptr)gPipM2; mb.rowBytes = 8; SetRect(&mb.bounds, 0,0,64,64);
    SetRect(&sr, 0,0,64,64);
    SetRect(&dr, gPetC.h - 32, y, gPetC.h + 32, y + 64);   /* equal rects -> clean */
    CopyMask(&ib, &mb, &gWin->portBits, &sr, &sr, &dr);
    if (gBlink) { MoveTo(gPetC.h-18, y+22); LineTo(gPetC.h-4, y+22);   /* wink */
                  MoveTo(gPetC.h+4, y+22);  LineTo(gPetC.h+18, y+22); }
}

static void draw_all(void)
{
    Rect r; short i;
    SetPort(gWin);
    r = gWin->portRect;
    FillRect(&r, &gGray);                               /* soft gray desktop */

    /* title */
    TextFont(0); TextSize(24); TextFace(bold);
    { char *t = "Claude Picks"; short w = 0, c;
      for (c=0; t[c]; c++) w += CharWidth(t[c]);
      MoveTo((gW - w)/2, 46); DrawText(t, 0, (short)strlen(t)); }
    TextFace(0);

    draw_pip();

    /* the dock bar along the bottom */
    { short barH = 58, top = gH - barH;
      SetRect(&r, 4, top, gW-4, gH-4);
      EraseRect(&r); FrameRoundRect(&r, 12, 12);
      for (i = 0; i < N_DOCK; i++) {
          short slot = (gW - 8) / N_DOCK;
          short cx = 4 + slot*i + slot/2;
          short ix = cx - 16, iy = top + 6;
          SetRect(&gIconR[i], cx-24, top+2, cx+24, gH-4);   /* generous hit box */
          draw_icon(dock_apps[i].icon, ix, iy);
          TextFont(0); TextSize(9);
          { const char *L = dock_apps[i].label; short w=0,c;
            for (c=0; L[c]; c++) w += CharWidth(L[c]);
            MoveTo(cx - w/2, gH-8); DrawText((Ptr)L, 0, (short)strlen(L)); }
      }
    }
}

/* Old Segment-Loader _Launch (trap 0xA9F2, A0 -> {pfName, pfParam}).  This is
 * the System 6 launch path -- no Process Manager needed.  Does not return on
 * success (the launched app replaces us). */
static void OldLaunch(void *pb)
{
    __asm__ __volatile__("move.l %0,%%a0\n\t.word 0xA9F2"
                         : : "g"(pb) : "a0","a1","d0","d1","d2","memory");
}

/* ---- launch an app by partial path (":Games:Celeste") from the boot root ---- */
static void launch_app(const char *cpath)
{
    Str255 p; short i = 0, j, n, k;
    CInfoPBRec ci; FSSpec spec;
    SysEnvRec env; WDPBRec wd; short vref;

    while (cpath[i]) { p[i+1] = cpath[i]; i++; }
    p[0] = (unsigned char)i;

    /* Pull the leaf name from cpath NOW -- PBGetCatInfo overwrites the buffer
     * we pass in ioNamePtr, so reading the name back out of p afterwards yields
     * an empty string (that was the "busy or damaged: <blank>" bug). */
    j = i; while (j > 0 && cpath[j-1] != ':') j--;   /* j = leaf start in cpath */
    n = 0; for (k = j; cpath[k]; k++) spec.name[++n] = cpath[k];
    spec.name[0] = (unsigned char)n;

    SysEnvirons(1, &env);
    memset(&wd, 0, sizeof wd);
    wd.ioVRefNum = env.sysVRefNum; wd.ioWDIndex = 0;
    if (PBGetWDInfoSync(&wd) == noErr) vref = wd.ioWDVRefNum;
    else vref = env.sysVRefNum;

    memset(&ci, 0, sizeof ci);
    ci.hFileInfo.ioNamePtr = p;
    ci.hFileInfo.ioVRefNum = vref;
    ci.hFileInfo.ioDirID   = 2;                 /* boot volume root */
    ci.hFileInfo.ioFDirIndex = 0;
    if (PBGetCatInfoSync(&ci) != noErr) { SysBeep(20); return; }
    spec.vRefNum = ci.hFileInfo.ioVRefNum;
    spec.parID   = ci.hFileInfo.ioFlParID;

    /* Make the app's folder the default directory, then old-_Launch it by name.
     * (The extended LaunchApplication block is misread by the System 6 Segment
     * Loader -- its reserved first long is taken as the name pointer.) */
    memset(&wd, 0, sizeof wd);
    wd.ioNamePtr = NULL;
    wd.ioVRefNum = spec.vRefNum;
    wd.ioWDDirID = spec.parID;
    PBHSetVolSync(&wd);
    {
        struct { StringPtr pfName; short pfParam; } lp;
        lp.pfName  = spec.name;
        lp.pfParam = 0;
        OldLaunch(&lp);
    }
    SysBeep(20);                                   /* only reached if launch fails */
}

int main(void)
{
    EventRecord ev; long nextAnim; Boolean quit = false, hasWNE;

    InitGraf(&qd.thePort); InitFonts(); InitWindows(); InitMenus();
    TEInit(); InitDialogs(NULL); InitCursor(); FlushEvents(everyEvent, 0);

    hasWNE = (NGetTrapAddress(0xA860, ToolTrap) != NGetTrapAddress(0xA89F, ToolTrap));
    { short k; for (k=0;k<8;k++) gGray.pat[k] = (k&1)?0x55:0xAA; }   /* 50% */
    make_pip2();
    gW = qd.screenBits.bounds.right; gH = qd.screenBits.bounds.bottom;
    { Rect r = qd.screenBits.bounds;
      gWin = NewWindow(NULL, &r, "\p", true, plainDBox, (WindowPtr)-1L, false, 0); }
    SetPort(gWin);
    gPetC.h = gW/2; gPetC.v = gH/2 - 10;
    draw_all();
    nextAnim = TickCount();

    while (!quit) {
        Boolean got;
        if (hasWNE) got = WaitNextEvent(everyEvent, &ev, 20, NULL);
        else { SystemTask(); got = GetNextEvent(everyEvent, &ev); }
        if (got) {
            if (ev.what == mouseDown) {
                Point pt = ev.where; short i;
                SetPort(gWin); GlobalToLocal(&pt);
                for (i = 0; i < N_DOCK; i++)
                    if (PtInRect(pt, &gIconR[i])) { launch_app(dock_apps[i].path); break; }
                if (i >= N_DOCK) {                 /* clicked Pip? he hops */
                    Rect pr; SetRect(&pr, gPetC.h-32, gPetC.v-32, gPetC.h+32, gPetC.v+32);
                    if (PtInRect(pt, &pr)) { gBob = -10; SysBeep(1); }
                }
            } else if (ev.what == keyDown) {
                char c = (char)(ev.message & 0xFF);
                if ((ev.modifiers & cmdKey) && (c=='q'||c=='Q')) quit = true;
            } else if (ev.what == updateEvt) {
                BeginUpdate((WindowPtr)ev.message); draw_all();
                EndUpdate((WindowPtr)ev.message);
            }
        }
        if (TickCount() >= nextAnim) {
            nextAnim = TickCount() + 8;
            gAnim++;
            gBlink = (gAnim % 40) == 0;
            if (gBob < 0) gBob++;
            else gBob = (gAnim/6) & 1;               /* breathing bob */
            draw_pip();                              /* only redraw Pip (cheap) */
        }
    }
    DisposeWindow(gWin);
    return 0;
}
