/* Full-screen pre-rendered demo player: each frame is exactly the 512x342
 * screen, blitted 1:1 (fills the display, no scaling).  Any key/click quits. */
#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include "generated_frames.h"
#define FRAME_TICKS 3
#define ROWBYTES (DEMO_W/8)
static WindowPtr gWin;
static Rect gDst;
int main(void){
    BitMap bm; Rect full; EventRecord ev; int frame=0; unsigned long nt; Boolean run=true;
    InitGraf(&qd.thePort);InitFonts();InitWindows();InitMenus();TEInit();InitDialogs(NULL);
    HideCursor();FlushEvents(everyEvent,0);
    full=qd.screenBits.bounds;
    gWin=NewWindow(NULL,&full,"\p",true,plainDBox,(WindowPtr)-1,false,0);
    SetPort(gWin);FillRect(&gWin->portRect,&qd.black);
    bm.rowBytes=ROWBYTES;SetRect(&bm.bounds,0,0,DEMO_W,DEMO_H);
    /* 1:1 blit, centered -- QuickDraw STRETCHING is slow on the Classic,
     * so frames are pre-rendered at display size and never scaled */
    { Rect d; SetRect(&d,(short)((full.right-full.left-DEMO_W)/2),
                         (short)((full.bottom-full.top-DEMO_H)/2),
                         (short)((full.right-full.left+DEMO_W)/2),
                         (short)((full.bottom-full.top+DEMO_H)/2));
      gDst=d; }
    nt=TickCount();
    while(run){
        while(GetNextEvent(everyEvent,&ev)){
            if(ev.what==keyDown||ev.what==mouseDown)run=false;
            else if(ev.what==updateEvt){BeginUpdate((WindowPtr)ev.message);EndUpdate((WindowPtr)ev.message);}
        }
        if(TickCount()>=nt){
            bm.baseAddr=(Ptr)demo_frames[frame];
            CopyBits(&bm,&gWin->portBits,&bm.bounds,&gDst,srcCopy,NULL);
            frame=(frame+1)%DEMO_FRAMES; nt+=FRAME_TICKS;
            if(TickCount()>nt+FRAME_TICKS*4)nt=TickCount();
        }
    }
    ShowCursor();DisposeWindow(gWin);return 0;
}
