/*
 * mac_main.c -- classic Mac Toolbox entry point for Celeste Classic.
 * Same shell as the Just One Boss port: black window, 30fps fixed-timestep
 * loop with decoupled render, p8_screen presented through mac_display.c.
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
#include <string.h>

#include "mac_display.h"
#include "mac_input.h"
#include "mac_sound.h"
#include "mac_prefs.h"
#include "p8.h"
#include "game.h"

extern int celeste_glue_last_music;    /* 40 while the title theme plays */

#define FRAME_TICKS 2          /* 60 ticks/sec / 30 fps = 2 ticks per frame */

static WindowPtr gWindow;
static Boolean   gRunning = true;

static void do_menu(long sel);         /* fwd: menu commands */

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

static void open_window(void)
{
    Rect r;
    short w = DISP_W, h = DISP_H;
    short left = (short)((qd.screenBits.bounds.right  - w) / 2);
    short top  = (short)((qd.screenBits.bounds.bottom - h) / 2);
    SetRect(&r, left, top, left + w, top + h);

    gWindow = NewWindow(NULL, &r, "\pCeleste",
                        true, plainDBox, (WindowPtr)-1, false, 0);
    SetPort(gWindow);
    FillRect(&gWindow->portRect, &qd.black);
}

#define MENU_FILE 128

static void setup_menus(void)
{
    MenuHandle m = NewMenu(MENU_FILE, "\pFile");
    AppendMenu(m, "\pPause/P;Restart;Sound/S;(-;Quit/Q");
    CheckItem(m, 3, true);              /* Sound starts on */
    InsertMenu(m, 0);
    DrawMenuBar();
}

static void handle_events(void)
{
    EventRecord ev;
    WindowPtr   win;
    while (GetNextEvent(everyEvent, &ev)) {
        switch (ev.what) {
        case keyDown:
        case autoKey:
            if (ev.modifiers & cmdKey) {
                char ch = (char)(ev.message & charCodeMask);
                long sel = MenuKey(ch);
                if (sel) do_menu(sel);
                else if (ch == 'q') gRunning = false;
            }
            break;
        case mouseDown:
            if (FindWindow(ev.where, &win) == inMenuBar)
                do_menu(MenuSelect(ev.where));
            break;
        case updateEvt:
            BeginUpdate((WindowPtr)ev.message);
            EndUpdate((WindowPtr)ev.message);
            mac_display_invalidate();
            break;
        default:
            break;
        }
    }
}

/* ---- pause menu (Esc or P opens; W/S or arrows move; Enter selects) ---- */
#define KEY_ESC   0x35
#define KEY_P     0x23
#define KEY_W2    0x0D
#define KEY_S2    0x01
#define KEY_UP2   0x7E
#define KEY_DOWN2 0x7D
#define KEY_RET2  0x24
#define KEY_ENT2  0x4C

static Boolean gPaused = false;
static Boolean gSwallowJump = false;   /* eat the menu's Enter press so it
                                          doesn't jump on resume */
static int gMenuSel = 0;

#define KM_BIT(km, vk) (((km)[(vk) >> 3] >> ((vk) & 7)) & 1)

static void draw_pause_menu(void)
{
    static const char *items[3] = { "continue", "restart", "quit" };
    int i;
    p8_camera_reset();
    p8_rectfill(40, 46, 88, 82, 0);
    p8_rect(41, 47, 87, 81, 7);
    p8_print("paused", 52, 51, 7);
    for (i = 0; i < 3; i++) {
        p8_print(items[i], 52, 61 + i * 7, 7);
        if (i == gMenuSel) p8_print(">", 45, 61 + i * 7, 7);
    }
    mac_display_present();
}

/* one keyboard poll per loop pass; edge-detected menu handling */
static void poll_pause_menu(void)
{
    static unsigned char prev[16];
    unsigned char km[16];
    int toggle, up, down, select;

    GetKeys(km);
    toggle = (KM_BIT(km, KEY_ESC) && !KM_BIT(prev, KEY_ESC)) ||
             (KM_BIT(km, KEY_P)   && !KM_BIT(prev, KEY_P));
    if (KM_BIT(km, 0x37)) toggle = 0;   /* cmd-P goes through the menu bar */
    up     = (KM_BIT(km, KEY_W2)   && !KM_BIT(prev, KEY_W2)) ||
             (KM_BIT(km, KEY_UP2)  && !KM_BIT(prev, KEY_UP2));
    down   = (KM_BIT(km, KEY_S2)   && !KM_BIT(prev, KEY_S2)) ||
             (KM_BIT(km, KEY_DOWN2) && !KM_BIT(prev, KEY_DOWN2));
    select = (KM_BIT(km, KEY_RET2) && !KM_BIT(prev, KEY_RET2)) ||
             (KM_BIT(km, KEY_ENT2) && !KM_BIT(prev, KEY_ENT2));
    memcpy(prev, km, sizeof km);

    if (!gPaused) {
        if (toggle) {
            gPaused = true;
            gMenuSel = 0;
            draw_pause_menu();
        }
        return;
    }

    if (toggle) { gPaused = false; return; }        /* Esc again = continue */
    if (up)   { gMenuSel = (gMenuSel + 2) % 3; draw_pause_menu(); }
    if (down) { gMenuSel = (gMenuSel + 1) % 3; draw_pause_menu(); }
    if (select) {
        switch (gMenuSel) {
        case 0:                                     /* continue */
            gPaused = false;
            gSwallowJump = true;
            break;
        case 1:                                     /* restart -> title */
            game_restart((unsigned)TickCount());
            gPaused = false;
            gSwallowJump = true;
            break;
        case 2:                                     /* quit */
            gRunning = false;
            break;
        }
    }
}

/* menu-bar commands (File menu; also reached via command keys) */
static void do_menu(long sel)
{
    short menu = HiWord(sel), item = LoWord(sel);
    if (menu == MENU_FILE) {
        switch (item) {
        case 1:                                 /* Pause */
            if (!gPaused) {
                gPaused = true;
                gMenuSel = 0;
                draw_pause_menu();
            } else {
                gPaused = false;
            }
            break;
        case 2:                                 /* Restart */
            game_restart((unsigned)TickCount());
            gPaused = false;
            break;
        case 3: {                               /* Sound on/off */
            extern int mac_sound_muted;
            void mac_music_stop(void);
            MenuHandle fm = GetMenuHandle(MENU_FILE);
            mac_sound_muted = !mac_sound_muted;
            if (mac_sound_muted) mac_music_stop();
            else if (celeste_glue_last_music >= 0)
                mac_music_play(celeste_glue_last_music);
            if (fm) CheckItem(fm, 3, !mac_sound_muted);
            break;
        }
        case 5:                                 /* Quit */
            gRunning = false;
            break;
        }
    }
    HiliteMenu(0);
}

/* true while the Enter that closed the menu is still held down */
static int jump_key_held(void)
{
    unsigned char km[16];
    GetKeys(km);
    return KM_BIT(km, KEY_RET2) || KM_BIT(km, KEY_ENT2);
}

#define SHOW_FPS   1   /* real-hardware perf diagnosis: fps readout ON */
#define AUDIO_DIAG 0   /* release: no audio telemetry line */
#ifndef PHASE_DIAG
#define PHASE_DIAG 0   /* "Celeste DIAG" build: per-phase tick costs */
#endif

#if PHASE_DIAG
/* ticks (1/60s) spent per second in update/draw/present, present split
 * into dither(D) + blit(B), and R = dirty rows on the last present.
 * u+d+p ~= 60 fully accounts the second; biggest = bottleneck. */
extern long mac_disp_dither_t, mac_disp_blit_t;
extern int  mac_disp_dirty_n, mac_disp_content_n;
extern int  celeste_glue_rebuilds;
static long gAccU, gAccD, gAccP, gAccN, gAccDi, gAccBl;
static long gShowU, gShowD, gShowP, gShowRb, gShowC, gShowR;
static long gRbMark;
static void draw_phases(void)
{
    char b[32];
    int  k = 0, i;
    long v[6];
    const char tag[6] = { 'u', 'd', 'p', 'b', 'C', 'R' };
    v[0] = gShowU; v[1] = gShowD; v[2] = gShowP;
    v[3] = gShowRb; v[4] = gShowC; v[5] = gShowR;
    for (i = 0; i < 6; i++) {
        long x = v[i] > 99 ? 99 : v[i];
        b[k++] = tag[i];
        b[k++] = (char)('0' + (x / 10) % 10);
        b[k++] = (char)('0' + x % 10);
    }
    b[k] = 0;
    p8_camera_reset();
    p8_rectfill(38, 7, 127, 13, 0);
    p8_print(b, 39, 8, 7);
}
#endif
#if AUDIO_DIAG
#include <stdio.h>
extern int mac_snd_diag_chunks, mac_snd_diag_err, mac_snd_diag_sfx;
void mac_sound_diag(int *pending, int *qhead, int *qtail);
int  mac_input_bits(void);
#endif

/* small "fps NN" readout, top-RIGHT on a black box (top-left belongs to the
 * game's own timer; perf-tuning aid, dirties only its rows) */
#if SHOW_FPS || AUDIO_DIAG
static void draw_fps(int fps)
{
    char buf[7];
    buf[0] = 'f'; buf[1] = 'p'; buf[2] = 's'; buf[3] = ' ';
    buf[4] = (char)('0' + (fps / 10) % 10);
    buf[5] = (char)('0' + fps % 10);
    buf[6] = 0;
    p8_camera_reset();
    p8_rectfill(102, 0, 127, 6, 0);
    p8_print(buf, 103, 1, 7);
#if AUDIO_DIAG
    {
        char d[30];
        int pend, qh, qt;
        mac_sound_diag(&pend, &qh, &qt);
        /* b = live button bits (hex: 10=jump, 20=dash) -- a value that stays
         * nonzero with hands OFF the keyboard = stuck key, case closed.
         * s = total sfx plays: racing s = something machine-gunning sounds. */
        sprintf(d, "p%d c%04d e%d b%02x s%04d",
                pend, mac_snd_diag_chunks % 10000, mac_snd_diag_err,
                mac_input_bits(), mac_snd_diag_sfx % 10000);
        (void)qh; (void)qt;
        p8_rectfill(36, 7, 127, 13, 0);
        p8_print(d, 37, 8, 7);
    }
#endif
}
#endif /* SHOW_FPS || AUDIO_DIAG */

int main(void)
{
    unsigned long next_tick, fps_mark;
    int frame_count = 0, fps = 0;

    toolbox_init();
    setup_menus();
    open_window();
    if (mac_display_init(gWindow) != noErr) return 1;
    mac_sound_init();                  /* failure = silence, still playable */
    mac_prefs_load();

    game_init((unsigned)TickCount());
    next_tick = TickCount();
    fps_mark = next_tick;

    /* Fixed-timestep loop with decoupled render (see JOB port for rationale):
     * game logic advances at a true 30Hz; slow machines drop visual frames
     * instead of running in slow motion. */
    while (gRunning) {
        unsigned long now;
        int updates = 0;

        handle_events();
        poll_pause_menu();

        if (gPaused) {                  /* frozen: music keeps looping */
            next_tick = TickCount();
            mac_music_idle();
            continue;
        }
        if (gSwallowJump) {             /* hold until the menu Enter lifts */
            if (jump_key_held()) {
                next_tick = TickCount();
                mac_music_idle();
                continue;
            }
            gSwallowJump = false;
        }

        now = TickCount();
#if PHASE_DIAG
        {
            unsigned long t0 = TickCount();
#endif
        while (now >= next_tick && updates < 8) {
            mac_input_poll();
            game_update();
            next_tick += FRAME_TICKS;
            updates++;
        }
#if PHASE_DIAG
            gAccU += (long)(TickCount() - t0);
            gAccN += updates;
        }
#endif
        mac_music_idle();               /* keep the music loop queued */
        if (TickCount() > next_tick + FRAME_TICKS * 8)
            next_tick = TickCount();

        if (updates > 0) {
#if PHASE_DIAG
            unsigned long td = TickCount();
#endif
            game_draw();
#if PHASE_DIAG
            gAccD += (long)(TickCount() - td);
#endif
            if (celeste_glue_last_music == 40) {   /* title: show best run */
                char best[32];
                if (mac_prefs_best_line(best)) {
                    p8_camera_reset();
                    p8_print(best, (128 - p8_print_width(best)) / 2, 116, 7);
                }
            }
            frame_count++;
            if (TickCount() - fps_mark >= 60) {
                fps = frame_count;
                frame_count = 0;
                fps_mark += 60;
#if PHASE_DIAG
                gShowU = gAccU; gShowD = gAccD; gShowP = gAccP;
                gShowRb = celeste_glue_rebuilds - gRbMark;
                gRbMark = celeste_glue_rebuilds;
                gShowC = mac_disp_content_n; gShowR = mac_disp_dirty_n;
                gAccU = gAccD = gAccP = gAccN = gAccDi = gAccBl = 0;
#endif
            }
#if SHOW_FPS || AUDIO_DIAG
            draw_fps(fps);
#endif
#if PHASE_DIAG
            draw_phases();
#endif
            (void)fps;
#if PHASE_DIAG
            {
                unsigned long tp = TickCount();
                mac_display_present();
                gAccP += (long)(TickCount() - tp);
                gAccDi += mac_disp_dither_t;
                gAccBl += mac_disp_blit_t;
            }
#else
            mac_display_present();
#endif
        }
    }

    mac_sound_dispose();
    mac_display_dispose();
    return 0;
}
