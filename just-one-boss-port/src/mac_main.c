/*
 * mac_main.c -- classic Mac Toolbox entry point.  Sets up a black window,
 * initializes the display + game, and pumps a 30fps loop:
 *   poll input -> game_update -> game_draw (into p8_screen) -> present.
 *
 * Built with Retro68.  Targets System 6/7 on a 68000; no Color QuickDraw.
 */
#include <Quickdraw.h>
#include <Windows.h>
#include <Fonts.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Events.h>
#include <ToolUtils.h>

#include "mac_display.h"

void platform_set_room(const unsigned char *room);
#include "mac_input.h"
#include "mac_audio.h"
#include "p8.h"
#include "game.h"

#define FRAME_TICKS 2          /* 60 ticks/sec / 30 fps = 2 ticks per frame */

#define MENU_APPLE 128
#define MENU_FILE  129

static WindowPtr gWindow;
static Boolean   gRunning = true;
static int       gAboutTimer;   /* frames left to show the About overlay */

static void toolbox_init(void)
{
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();
    FlushEvents(everyEvent, 0);
}

/* a real menu bar so the game can be quit the Mac way (File > Quit, Cmd-Q) */
static void setup_menus(void)
{
    MenuHandle m;
    m = NewMenu(MENU_APPLE, "\p\024");           /* \024 = the Apple mark */
    AppendMenu(m, "\pAbout Just One Boss\311");  /* \311 = ellipsis */
    InsertMenu(m, 0);
    m = NewMenu(MENU_FILE, "\pFile");
    AppendMenu(m, "\pQuit/Q");
    InsertMenu(m, 0);
    DrawMenuBar();
}

static void do_menu_choice(long choice)
{
    short menu = HiWord(choice), item = (short)(choice & 0xFFFF);
    if (menu == MENU_FILE && item == 1)
        gRunning = false;
    else if (menu == MENU_APPLE && item == 1)
        gAboutTimer = 150;                       /* ~5s in-game overlay */
    HiliteMenu(0);
}

static void open_window(void)
{
    Rect r;
    /* centered window a touch larger than the 256x256 image */
    short w = DISP_W, h = DISP_H;
    short left = (short)((qd.screenBits.bounds.right  - w) / 2);
    short top  = (short)((qd.screenBits.bounds.bottom - h) / 2);
    SetRect(&r, left, top, left + w, top + h);

    gWindow = NewWindow(NULL, &r, "\pJust One Boss",
                        true, plainDBox, (WindowPtr)-1, false, 0);
    SetPort(gWindow);
    /* paint the content black so the letterbox matches PICO-8's border */
    FillRect(&gWindow->portRect, &qd.black);
}

static void handle_events(void)
{
    EventRecord ev;
    while (GetNextEvent(everyEvent, &ev)) {
        switch (ev.what) {
        case keyDown:
        case autoKey:
            if (ev.modifiers & cmdKey)      /* Cmd keys go to the menus */
                do_menu_choice(MenuKey((char)(ev.message & charCodeMask)));
            break;
        case updateEvt:
            BeginUpdate((WindowPtr)ev.message);
            EndUpdate((WindowPtr)ev.message);
            mac_display_invalidate();   /* dirty-band optimizer: full redraw */
            break;
        case mouseDown: {
            WindowPtr wp;
            if (FindWindow(ev.where, &wp) == inMenuBar)
                do_menu_choice(MenuSelect(ev.where));
        } break;
        default:
            break;
        }
    }
}

/* small in-game About overlay (Apple menu), drawn over the logical screen */
static void draw_about(void)
{
    if (gAboutTimer <= 0) return;
    gAboutTimer--;
    p8_camera_reset();
    p8_rectfill(8, 50, 118, 76, 0);
    p8_rect(8, 50, 118, 76, 7);
    p8_print("just one boss", 38, 54, 7);
    p8_print("by ayla nonsense", 32, 62, 6);
    p8_print("mac port, with permission", 14, 70, 6);
}

/* draw a small "fps NN" readout in the top-left of the logical screen */
static void draw_fps(int fps)
{
    char buf[8];
    buf[0] = 'f'; buf[1] = 'p'; buf[2] = 's'; buf[3] = ' ';
    buf[4] = (char)('0' + (fps / 10) % 10);
    buf[5] = (char)('0' + fps % 10);
    buf[6] = 0;
    p8_camera_reset();
    p8_print(buf, 2, 2, 7);
}

void platform_set_room(const unsigned char *room)
{
    mac_display_set_room(room);
}

int main(void)
{
    unsigned long next_tick, fps_mark;
    int frame_count = 0, fps = 0;

    toolbox_init();
    setup_menus();
    open_window();
    if (mac_display_init(gWindow) != noErr) return 1;
    mac_audio_init();             /* game runs silent if this fails */

    game_init();
    next_tick = TickCount();
    fps_mark = next_tick;

    /* Fixed-timestep loop with DECOUPLED render.  The game advances at a true
     * 30Hz (FRAME_TICKS = 2 ticks) no matter how slowly we can draw, so timers
     * and animations run at the correct wall-clock speed -- when the machine
     * can't keep up we just drop visual frames instead of running in slow
     * motion.  update() is cheap; draw()/present() is the expensive part. */
    while (gRunning) {
        unsigned long now;
        int updates = 0;

        handle_events();
        mac_audio_tick();          /* resume/loop music when the channel frees */

        now = TickCount();
        while (now >= next_tick && updates < 8) {   /* catch up on game logic */
            mac_input_poll();
            game_update();
            next_tick += FRAME_TICKS;
            updates++;
        }
        /* if we're hopelessly behind, resync rather than spiral */
        if (TickCount() > next_tick + FRAME_TICKS * 8)
            next_tick = TickCount();

        if (updates > 0) {                          /* something changed -> draw */
            game_draw();
            frame_count++;
            if (TickCount() - fps_mark >= 60) {
                fps = frame_count;
                frame_count = 0;
                fps_mark += 60;
            }
            draw_fps(fps);
            draw_about();
            mac_display_present();
        }
    }

    mac_audio_dispose();
    mac_display_dispose();
    return 0;
}
