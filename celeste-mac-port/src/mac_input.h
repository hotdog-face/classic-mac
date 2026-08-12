/*
 * mac_input.h -- PICO-8 button model over the Mac keyboard.
 * Button ids: 0 left, 1 right, 2 up, 3 down, 4 O (Z / , ), 5 X (X / . ).
 */
#ifndef MAC_INPUT_H
#define MAC_INPUT_H

#define P8_BTN_LEFT  0
#define P8_BTN_RIGHT 1
#define P8_BTN_UP    2
#define P8_BTN_DOWN  3
#define P8_BTN_O     4
#define P8_BTN_X     5

void mac_input_poll(void);   /* call once per frame before update */
int  mac_input_bits(void);   /* diag: 6 button bits packed */
int  p8_btn(int b);          /* held this frame */
int  p8_btnp(int b);         /* pressed this frame (rising edge, no auto-repeat) */

#endif /* MAC_INPUT_H */
