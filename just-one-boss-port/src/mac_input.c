/*
 * mac_input.c -- read the hardware keyboard map with GetKeys and fold it into
 * the 6-button PICO-8 model.  We read the raw key bitmap rather than the event
 * queue so held keys (movement) work without key-repeat latency.
 */
#include "mac_input.h"
#include <MacTypes.h>
#include <Events.h>

/* Mac virtual key codes.  Arrow codes differ by KEYBOARD: extended/modern
 * ADB = 0x7B-0x7E, compact ADB (Apple Standard, on the real Classic) =
 * 0x3B-0x3E, Plus-era M0110A = 0x46/0x42/0x48/0x4D -- accept all three,
 * plus WASD. */
#define KEY_LEFT   0x7B
#define KEY_RIGHT  0x7C
#define KEY_DOWN   0x7D
#define KEY_UP     0x7E
#define KEY_LEFT2  0x3B
#define KEY_RIGHT2 0x3C
#define KEY_DOWN2  0x3D
#define KEY_UP2    0x3E
#define KEY_LEFT3  0x46
#define KEY_RIGHT3 0x42
#define KEY_DOWN3  0x48
#define KEY_UP3    0x4D
#define KEY_W      0x0D
#define KEY_A      0x00
#define KEY_S      0x01
#define KEY_D      0x02
#define KEY_Z      0x06
#define KEY_X      0x07
#define KEY_COMMA  0x2B   /* alt for O */
#define KEY_PERIOD 0x2F   /* alt for X */
#define KEY_RETURN 0x24   /* alt for O (menu confirm) */

static unsigned char cur[6], prev[6];

/* Retro68 has no KeyMap typedef; GetKeys() fills a raw 16-byte key bitmap. */
static int key_down(const unsigned char *km, int vk)
{
    return (km[vk >> 3] >> (vk & 7)) & 1;
}

void mac_input_poll(void)
{
    unsigned char km[16];
    int i;
    GetKeys(km);
    for (i = 0; i < 6; i++) prev[i] = cur[i];

    cur[P8_BTN_LEFT]  = key_down(km, KEY_LEFT)  || key_down(km, KEY_LEFT2)
                     || key_down(km, KEY_LEFT3)  || key_down(km, KEY_A);
    cur[P8_BTN_RIGHT] = key_down(km, KEY_RIGHT) || key_down(km, KEY_RIGHT2)
                     || key_down(km, KEY_RIGHT3) || key_down(km, KEY_D);
    cur[P8_BTN_UP]    = key_down(km, KEY_UP)    || key_down(km, KEY_UP2)
                     || key_down(km, KEY_UP3)    || key_down(km, KEY_W);
    cur[P8_BTN_DOWN]  = key_down(km, KEY_DOWN)  || key_down(km, KEY_DOWN2)
                     || key_down(km, KEY_DOWN3)  || key_down(km, KEY_S);
    cur[P8_BTN_O]     = key_down(km, KEY_Z) || key_down(km, KEY_COMMA) ||
                        key_down(km, KEY_RETURN);
    cur[P8_BTN_X]     = key_down(km, KEY_X) || key_down(km, KEY_PERIOD);
}

int p8_btn(int b)  { return (b >= 0 && b < 6) ? cur[b] : 0; }
int p8_btnp(int b) { return (b >= 0 && b < 6) ? (cur[b] && !prev[b]) : 0; }
