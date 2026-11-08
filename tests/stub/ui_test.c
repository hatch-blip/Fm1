/* SPDX-License-Identifier: MPL-2.0 */
/* DUBWISE's app.c and engine together against a fake panel (tests/stub/plat.h): buttons, keys, encoders, MIDI in
 * and out, the store, a screen that draws nothing. Checks the controls' logic without fm1-base.
 *   cc -std=c99 -DPLAT_HOST -DDUB_TEST -Itests/stub -Iapp -o build/ui_test tests/stub/ui_test.c app/dub.c -lm */
#include <stdio.h>
#include "plat.h"
#include "app.h"

/* ---- the fake platform ---- */
static uint32_t t_ms, t_btn, t_keys;
static int32_t t_enc[NE];
static uint32_t t_midi_in[256], t_in_r, t_in_w, t_clocks_out, t_starts_out;
static uint8_t t_store[4][256];
static int t_store_n[4];
uint16_t pal[5];
const felucca_font_t FONT_XS, FONT_B;

uint32_t plat_ms(void) { return t_ms; }
uint32_t plat_buttons(void) { return t_btn; }
uint32_t plat_keys(void) { return t_keys; }
int32_t plat_enc(int role) { int32_t e = t_enc[role]; t_enc[role] = 0; return e; }
void plat_leds(uint32_t b, uint32_t k) { (void)b; (void)k; }
void plat_glow(int on) { (void)on; }
int plat_midi_in(uint32_t *pkt, int *port)
{
    if (t_in_r == t_in_w)
        return 0;
    *pkt = t_midi_in[t_in_r++ % 256u];
    *port = 0;
    return 1;
}
void plat_midi_send(uint32_t pkt, uint32_t to)
{
    (void)to;
    if ((pkt >> 8 & 0xFFu) == 0xF8u)
        t_clocks_out++;
    if ((pkt >> 8 & 0xFFu) == 0xFAu)
        t_starts_out++;
}
int plat_store_load(int obj, void *p, uint32_t n)
{
    if (t_store_n[obj] != (int)n)
        return -1;
    memcpy(p, t_store[obj], n);
    return (int)n;
}
int plat_store_save(int obj, const void *p, uint32_t n)
{
    memcpy(t_store[obj], p, n);
    t_store_n[obj] = (int)n;
    return 0;
}
void cv_begin(uint32_t w, uint32_t h, uint16_t bg) { (void)w; (void)h; (void)bg; }
void cv_blit(uint32_t x, uint32_t y) { (void)x; (void)y; }
void cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
int32_t cv_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    (void)y; (void)f; (void)c;
    return x + 6 * (int32_t)strlen(s);
}
int32_t text_w(const felucca_font_t *f, const char *s) { (void)f; return 6 * (int32_t)strlen(s); }
void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
int ann_on(void) { return 0; }
int ann_where(void) { return 0; }
void ann_say(const char *a, const char *b, uint32_t f) { (void)a; (void)b; (void)f; }
void ann_tick(uint32_t ms) { (void)ms; }

#include "app.c"

/* ---- the run: frames every 16 ms, the audio in its blocks between ---- */
static int fails;
static void run(uint32_t ms)
{
    static int32_t lr[2 * APP_BLOCK];
    static double audio_ms;
    uint32_t end = t_ms + ms;
    while (t_ms < end) {
        t_ms++;
        audio_ms += 1.0;
        while (audio_ms >= 256.0 * 1000.0 / 44100.0) {
            audio_ms -= 256.0 * 1000.0 / 44100.0;
            app_render(lr, 256u, 4096u);
        }
        if (t_ms % 16u == 0u)
            app_frame();
    }
}
static void press(int b) { t_btn |= 1u << b; run(40); }
static void release(int b) { t_btn &= ~(1u << b); run(40); }
static void tap(int b) { press(b); release(b); }
static void turn(int e, int32_t n) { t_enc[e] += n; run(40); }
static void expect(const char *what, int want)
{
    int got = -999;
    app_expect(what, &got);
    if (got != want) {
        printf("  FAIL %s: %d, wanted %d\n", what, got, want);
        fails++;
    } else {
        printf("  ok   %s %d\n", what, want);
    }
}

int main(void)
{
    app_init();
    app_ui_init();
    run(200);
    expect("chord", CHORD_MIN);
    tap(B_SEL);                          /* SEL tapped: the chord */
    expect("chord", CHORD_MAJ);
    press(B_SEL);                        /* SEL held + knobs: the trippy page, no chord change */
    turn(EN_K1, -3);
    turn(EN_K2, 5);
    turn(EN_K3, 10);
    turn(EN_K4, 2);
    release(B_SEL);
    expect("chord", CHORD_MAJ);
    expect("shift", DUB_SHIFT_OFF - 1);  /* (one step a turn, either way) */
    expect("warp", 20);
    expect("auto", 40);
    expect("space", 8);
    expect("fb", 55);                    /* the first page untouched */
    expect("filter", 0);
    turn(EN_K3, -4);                     /* SEL up: K3 is the filter again */
    expect("filter", -4);
    expect("auto", 40);
    press(B_SEL);                        /* SEL + FX: freeze, no throw */
    tap(B_FX);
    release(B_SEL);
    expect("freeze", 1);
    expect("throw", 0);
    expect("chord", CHORD_MAJ);
    press(B_FX);                         /* FX alone: the throw */
    expect("throw", 1);
    release(B_FX);
    expect("throw", 0);
    expect("freeze", 1);

    /* clock out while playing on our own tempo: about 24 a beat at 76 bpm */
    expect("clock_out", 1);
    t_clocks_out = 0;
    tap(B_PLAY);
    expect("playing", 1);
    run(3158);
    printf("  clocks out in ~a bar: %u, starts %u\n", t_clocks_out, t_starts_out);
    if (t_clocks_out < 92 || t_clocks_out > 100 || t_starts_out != 1) {
        printf("  FAIL clock out\n");
        fails++;
    }
    press(B_SEL);                        /* SEL + PLAY: clock out off, still playing */
    tap(B_PLAY);
    release(B_SEL);
    expect("clock_out", 0);
    expect("playing", 1);
    tap(B_PLAY);
    expect("playing", 0);

    /* MIDI in: a start and a clock at 100 bpm, and a note under C3 for the bass */
    {
        uint32_t k;
        t_midi_in[t_in_w++ % 256u] = 0x0Fu | 0xFAu << 8;
        for (k = 0; k < 24u * 8u; k++) {         /* 2 bars of ticks, 25 ms apart */
            t_midi_in[t_in_w++ % 256u] = 0x0Fu | 0xF8u << 8;
            if (k == 30u)
                t_midi_in[t_in_w++ % 256u] = 0x09u | 0x90u << 8 | 36u << 16 | 100u << 24;
            run(25);
            if (k == 30u)
                expect("bass", 36);
        }
        expect("ext", 1);
        expect("playing", 1);
        printf("  following at %.1f bpm, step %d\n", dubs.bpm_now, dubs.step);
        if (dubs.bpm_now < 97.0f || dubs.bpm_now > 103.0f || dubs.step != 31) {
            printf("  FAIL follow the clock\n");
            fails++;
        }
        t_midi_in[t_in_w++ % 256u] = 0x0Fu | 0xFCu << 8;
        run(50);
        expect("playing", 0);
    }

    /* saved and back after a reboot */
    tap(B_SAVE);
    expect("dirty", 0);
    memset(&set, 0, sizeof set);
    app_init();
    expect("auto", 40);
    expect("warp", 20);
    expect("clock_out", 0);
    expect("chord", CHORD_MAJ);
    printf(fails ? "  %d FAILED\n" : "  all ok\n", fails);
    return fails != 0;
}
