/* SPDX-License-Identifier: MPL-2.0 */
/* A stand-in for the parts of fm1-base's plat.h, gfx.c and announce.c that DUBWISE's app.c uses: only for
 * tests/run_tests.sh's syntax check of app.c without fm1-base (the real build uses fm1-base's own). */
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define PLAT_POOL
enum { B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC, B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };
#define NKEYS 27
#define PLAT_WHITE_KEYS {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26}
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
uint32_t plat_ms(void); uint32_t plat_buttons(void); uint32_t plat_keys(void); int32_t plat_enc(int role);
void plat_leds(uint32_t b, uint32_t k); void plat_glow(int on);
int plat_midi_in(uint32_t *pkt, int *port);
void plat_midi_send(uint32_t pkt, uint32_t to);
uint32_t plat_midi_in_t24(void);
uint32_t plat_ticks24(void);
#define PLAT_TICKS_PER_US 24u
#define TO_USB 1u
int plat_store_load(int obj, void *p, uint32_t n); int plat_store_save(int obj, const void *p, uint32_t n);
#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
typedef struct { int h; } felucca_font_t;
extern const felucca_font_t FONT_XS, FONT_B;
extern uint16_t pal[5];
#define C_BLACK 0x0000u
#define C_WHITE 0xFFFFu
#define C_LINE pal[0]
#define C_DIM pal[1]
#define C_GRAY pal[2]
#define C_AMB pal[3]
#define C_HI pal[4]
void cv_begin(uint32_t w, uint32_t h, uint16_t bg); void cv_blit(uint32_t x, uint32_t y);
void cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c);
int32_t cv_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c);
int32_t text_w(const felucca_font_t *f, const char *s);
void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c);
#define ANN_NOW 1u
int ann_on(void); int ann_where(void); void ann_say(const char *a, const char *b, uint32_t f); void ann_tick(uint32_t ms);
