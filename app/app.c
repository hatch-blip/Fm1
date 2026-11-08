/* SPDX-License-Identifier: MPL-2.0 */
/* DUBWISE: a dub sound system for the M-VAVE FM-1. This file is the app: the controls, the screen, the settings,
 * MIDI in; the sound is dub.c's (dub.h). Part of fm1-base's unity build (device/unity.c, host/plat_host.c), after the
 * platform: gfx.c and announce.c are in scope.
 *
 *   keys F3..E4   sub bass (two octaves down), recorded into the loop while REC is lit
 *   keys F4..G5   skank: hold one and its chord chops on the offbeats (a stab at once while stopped)
 *   PLAY          the riddim on / off            REC     record the bass loop (held: clear it)
 *   SEQ           the next riddim                SEL     the chord: minor, major, minor 7
 *   FX (held)     throw it all into the echo      ARP (held) the siren
 *   GLO           kick the spring                SAVE    keep the settings and the loop
 *   ENV LFO EDIT  drums, bass, skank in / out     OCT- OCT+ the keys' octave
 *   SELECT tempo  ALGO siren  PRESET riddim  K1 echo feedback  K2 echo time  K3 filter  K4 spring */
#include "dub.h"

#define SET_MAGIC 0x44554231u            /* "DUB1": the settings record; another magic loads defaults */
typedef struct {
    uint32_t magic;
    uint8_t bpm, pattern, siren, chord, echo_div, fb, spring;
    int8_t octave;
    uint8_t loop[DUB_LOOP];
} settings_t;
static settings_t set;
static uint8_t set_dirty;

#define C_RED RGB(230, 50, 45)
#define C_GOLD RGB(250, 200, 40)
#define C_GREEN RGB(40, 190, 90)

#define SPLIT 12                         /* keys 0..11 bass, 12..26 skank */

/* ---------------------------------------------------------------- sound --- */
static float gain_now;
static float buf[2 * APP_BLOCK];

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;   /* MASTER, read once a block; 24-bit out */
    uint32_t done = 0, i;
    while (done < n) {
        uint32_t m = n - done > APP_BLOCK ? APP_BLOCK : n - done;
        dub_render(buf, m);
        for (i = 0; i < m; i++) {
            gain_now += (gain - gain_now) * 0.004f;
            lr[2 * (done + i)] = (int32_t)(buf[2 * i] * gain_now);
            lr[2 * (done + i) + 1] = (int32_t)(buf[2 * i + 1] * gain_now);
        }
        done += m;
    }
}

void app_panic(void) { dub_event(EV_PANIC, 0); }

/* -------------------------------------------------------------- settings --- */
static void settings_defaults(void)
{
    uint32_t i;
    set.magic = SET_MAGIC;
    set.bpm = 76;
    set.pattern = PAT_ONEDROP;
    set.siren = SIREN_WAIL;
    set.chord = CHORD_MIN;
    set.echo_div = ECHO_8D;
    set.fb = 55;
    set.spring = 40;
    set.octave = 0;
    for (i = 0; i < DUB_LOOP; i++)
        set.loop[i] = 0;
}

static void settings_apply(void)         /* the settings into the engine */
{
    uint32_t i;
    dubp.bpm = set.bpm;
    dubp.pattern = set.pattern;
    dubp.siren_mode = set.siren;
    dubp.chord = set.chord;
    dubp.echo_div = set.echo_div;
    dubp.echo_fb = (float)set.fb / 100.0f;
    dubp.spring = (float)set.spring / 100.0f;
    for (i = 0; i < DUB_LOOP; i++)
        dubs.loop[i] = set.loop[i];
}

void app_init(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.bpm < 60 || set.bpm > 180 || set.pattern >= PAT_N || set.siren >= SIREN_N || set.chord >= CHORD_N
        || set.echo_div >= ECHO_N || set.fb > 110 || set.spring > 100 || set.octave < -2 || set.octave > 2)
        settings_defaults();
    dub_init();
    settings_apply();
}

static void settings_save(void)
{
    uint32_t i;
    for (i = 0; i < DUB_LOOP; i++)
        set.loop[i] = dubs.loop[i];
    if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
        set_dirty = 0;
        app_say("SAVED", "");
    } else {
        app_say("SAVE", "FAILED");
    }
}

/* ------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    int32_t key_note[NKEYS];             /* the note each held key started (the octave may change meanwhile) */
    uint32_t rec_t;                      /* REC went down (0: not held, or its hold already acted) */
    int32_t last_step;
    uint8_t redraw;
} ui;

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {                      /* and to a screen reader (fm1-reader) */
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void say_int(const char *what, int32_t v, const char *unit)
{
    char t[16];
    int_text(v, t);
    text_cat(t, unit);
    app_say(what, t);
}

static void note_name(int32_t note, char *b)
{
    static const char *const N[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char o[12];
    b[0] = 0;
    text_cat(b, N[note % 12]);
    int_text(note / 12 - 1, o);
    text_cat(b, o);
}

static void filter_text(char *t)         /* "OPEN", "LP 40", "HP 25" */
{
    int32_t v = (int32_t)(dubp.filter * 100.0f + (dubp.filter < 0.0f ? -0.5f : 0.5f));
    t[0] = 0;
    if (v == 0) {
        text_cat(t, "OPEN");
        return;
    }
    text_cat(t, v < 0 ? "LP " : "HP ");
    int_text(v < 0 ? -v : v, t + 3);
}

static int32_t key_note(uint32_t k)      /* key 0 is F3 (MIDI 53) */
{
    return k < SPLIT ? 53 + (int32_t)k - 24 + 12 * set.octave   /* bass: F1 .. E2 */
                     : 53 + (int32_t)k - 12 + 12 * set.octave;  /* skank: F3 .. G4 */
}

static void all_keys_off(void)
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++)
        if (ui.keys >> k & 1u)
            dub_event(k < SPLIT ? EV_BASS_OFF : EV_SKANK_OFF, ui.key_note[k]);
    ui.keys = 0;
}

static void play_toggle(void)
{
    if (dubs.playing) {
        dub_event(EV_STOP, 0);
        app_say("STOP", "");
    } else {
        dub_event(EV_PLAY, 0);
        app_say("PLAY", dub_pattern_name(dubp.pattern));
    }
}

static void midi_in(void)                /* notes below C3 play the bass, the rest the skank; start / stop */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24;
        int bass = d1 < 48u;
        if ((st & 0xF0u) == 0x90u && d2)
            dub_event(bass ? EV_BASS_ON : EV_SKANK_ON, (int32_t)d1);
        else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
            dub_event(bass ? EV_BASS_OFF : EV_SKANK_OFF, (int32_t)d1);
        else if ((st == 0xFAu || st == 0xFBu) && !dubs.playing)
            dub_event(EV_PLAY, 0);
        else if (st == 0xFCu && dubs.playing)
            dub_event(EV_STOP, 0);
    }
}

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), down = btn & ~ui.btn, k, leds, key_leds;
    int32_t e;
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            ui.key_note[k] = key_note(k);
            dub_event(k < SPLIT ? EV_BASS_ON : EV_SKANK_ON, ui.key_note[k]);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            dub_event(k < SPLIT ? EV_BASS_OFF : EV_SKANK_OFF, ui.key_note[k]);
        }
    }
    if (down >> B_PLAY & 1u)
        play_toggle();
    if (down >> B_SEQ & 1u) {
        set.pattern = (uint8_t)((set.pattern + 1u) % PAT_N);
        dubp.pattern = set.pattern;
        set_dirty = 1;
        app_say("RIDDIM", dub_pattern_name(set.pattern));
    }
    if (down >> B_SEL & 1u) {
        set.chord = (uint8_t)((set.chord + 1u) % CHORD_N);
        dubp.chord = set.chord;
        set_dirty = 1;
        app_say("CHORD", dub_chord_name(set.chord));
    }
    if (down >> B_GLO & 1u) {
        dub_event(EV_CRASH, 0);
        app_say("SPRING", "CRASH");
    }
    if (down >> B_ENV & 1u) {
        dubp.mute_drums = (uint8_t)!dubp.mute_drums;
        app_say("DRUMS", dubp.mute_drums ? "OUT" : "IN");
    }
    if (down >> B_LFO & 1u) {
        dubp.mute_bass = (uint8_t)!dubp.mute_bass;
        app_say("BASS", dubp.mute_bass ? "OUT" : "IN");
    }
    if (down >> B_EDIT & 1u) {
        dubp.mute_skank = (uint8_t)!dubp.mute_skank;
        app_say("SKANK", dubp.mute_skank ? "OUT" : "IN");
    }
    if (down >> B_SAVE & 1u)
        settings_save();
    if (down & (1u << B_OCTDN | 1u << B_OCTUP)) {
        int32_t o = clamp(set.octave + ((down >> B_OCTUP & 1u) ? 1 : -1), -2, 2);
        if (o != set.octave) {
            set.octave = (int8_t)o;
            set_dirty = 1;
            say_int("OCTAVE", o, "");
        }
    }
    /* held: the throw and the siren, as long as the finger stays */
    if ((uint8_t)(btn >> B_FX & 1u) != dubp.throw_on) {
        dubp.throw_on = (uint8_t)(btn >> B_FX & 1u);
        if (dubp.throw_on)
            app_say("THROW", "");
        ui.redraw = 1;
    }
    if ((uint8_t)(btn >> B_ARP & 1u) != dubp.siren_on) {
        dubp.siren_on = (uint8_t)(btn >> B_ARP & 1u);
        if (dubp.siren_on)
            app_say("SIREN", dub_siren_name(dubp.siren_mode));
        ui.redraw = 1;
    }
    /* REC: a press arms the loop's recording, a second's hold clears the loop */
    if (down >> B_REC & 1u)
        ui.rec_t = plat_ms() | 1u;
    if (ui.rec_t) {
        if (btn >> B_REC & 1u) {
            if (plat_ms() - ui.rec_t >= 1000u) {
                ui.rec_t = 0;
                dub_event(EV_CLEAR_LOOP, 0);
                dubp.rec_on = 0;
                set_dirty = 1;
                app_say("LOOP", "CLEARED");
            }
        } else {
            ui.rec_t = 0;
            dubp.rec_on = (uint8_t)!dubp.rec_on;
            if (dubp.rec_on)
                set_dirty = 1;
            app_say("REC", dubp.rec_on ? "ON" : "OFF");
        }
    }
    /* the encoders */
    if ((e = plat_enc(EN_SELECT)) != 0) {
        set.bpm = (uint8_t)clamp((int32_t)set.bpm + e, 60, 180);
        dubp.bpm = set.bpm;
        set_dirty = 1;
        say_int("TEMPO", set.bpm, " BPM");
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        set.siren = (uint8_t)(((int32_t)set.siren + e % SIREN_N + SIREN_N) % SIREN_N);
        dubp.siren_mode = set.siren;
        set_dirty = 1;
        app_say("SIREN", dub_siren_name(set.siren));
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        set.pattern = (uint8_t)(((int32_t)set.pattern + e % PAT_N + PAT_N) % PAT_N);
        dubp.pattern = set.pattern;
        set_dirty = 1;
        app_say("RIDDIM", dub_pattern_name(set.pattern));
    }
    if ((e = plat_enc(EN_K1)) != 0) {
        set.fb = (uint8_t)clamp((int32_t)set.fb + 2 * e, 0, 110);
        dubp.echo_fb = (float)set.fb / 100.0f;
        set_dirty = 1;
        say_int("FEEDBACK", set.fb, "%");
    }
    if ((e = plat_enc(EN_K2)) != 0) {
        set.echo_div = (uint8_t)clamp((int32_t)set.echo_div + (e > 0 ? 1 : -1), 0, ECHO_N - 1);
        dubp.echo_div = set.echo_div;
        set_dirty = 1;
        app_say("ECHO", dub_echo_name(set.echo_div));
    }
    if ((e = plat_enc(EN_K3)) != 0) {    /* the filter snaps open as it crosses the middle */
        int32_t v = (int32_t)(dubp.filter * 20.0f + (dubp.filter < 0.0f ? -0.5f : 0.5f)), w = clamp(v + e, -20, 20);
        if ((v < 0 && w > 0) || (v > 0 && w < 0))
            w = 0;
        dubp.filter = (float)w / 20.0f;
        {
            char t[12];
            filter_text(t);
            app_say("FILTER", t);
        }
    }
    if ((e = plat_enc(EN_K4)) != 0) {
        set.spring = (uint8_t)clamp((int32_t)set.spring + 4 * e, 0, 100);
        dubp.spring = (float)set.spring / 100.0f;
        set_dirty = 1;
        say_int("SPRING", set.spring, "%");
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;

    /* the lights: what is on, and the step running along the white keys */
    leds = (dubs.playing ? 1u << B_PLAY : 0u) | (dubp.rec_on ? 1u << B_REC : 0u)
           | (dubp.mute_drums ? 0u : 1u << B_ENV) | (dubp.mute_bass ? 0u : 1u << B_LFO)
           | (dubp.mute_skank ? 0u : 1u << B_EDIT) | (dubp.throw_on ? 1u << B_FX : 0u)
           | (dubp.siren_on ? 1u << B_ARP : 0u) | (set_dirty ? 1u << B_SAVE : 0u) | (btn & (1u << B_GLO | 1u << B_SEQ));
    key_leds = keys;
    if (dubs.playing && dubs.step >= 0) {
        static const uint8_t WK[16] = PLAT_WHITE_KEYS;
        key_leds |= 1u << WK[dubs.step & 15];
    }
    plat_glow(1);
    plat_leds(leds, key_leds);
}

void app_input_only(void)
{
    midi_in();
    input();
}

/* --------------------------------------------------------------- screen --- */
static void box(int32_t x, int32_t y, int32_t w, const char *s, uint16_t fill, uint16_t ink)
{
    cv_rect(x, y, w, 18, fill);
    cv_text(x + (w - text_w(&FONT_XS, s)) / 2, y + 2, &FONT_XS, s, ink);
}

static void draw(void)
{
    static const char *const ROW[4] = {"KICK", "RIM", "HAT", "SKNK"};
    char t[24];
    uint32_t now = plat_ms();
    int32_t i, r, st = dubs.playing ? dubs.step : -1, ps = st & 15;

    /* the header: the name, the octave and the tempo, or a message */
    cv_begin(240, 32, C_BLACK);
    if (now < ui.msg_until) {
        int32_t x = cv_text(8, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_GOLD);
    } else {
        int32_t x = cv_text(8, 6, &FONT_B, "DUB", C_RED);
        x = cv_text(x, 6, &FONT_B, "WI", C_GOLD);
        x = cv_text(x, 6, &FONT_B, "SE", C_GREEN);
        {
            char o[8];
            t[0] = 0;
            text_cat(t, set.octave >= 0 ? "OCT +" : "OCT ");
            int_text(set.octave, o);
            text_cat(t, o);
            cv_text(x + 10, 12, &FONT_XS, t, C_GRAY);
        }
        int_text(set.bpm, t);
        x = cv_text(200 - text_w(&FONT_B, t), 6, &FONT_B, t, dubs.playing ? C_WHITE : C_GRAY);
        cv_text(x + 4, 12, &FONT_XS, "BPM", C_GRAY);
    }
    cv_blit(0, 0);

    /* the riddim's grid, the playhead over it; then the bass loop */
    cv_begin(240, 144, C_BLACK);
    t[0] = 0;
    text_cat(t, dub_pattern_name(set.pattern));
    cv_text(4, 0, &FONT_XS, t, C_GOLD);
    t[0] = 0;
    if (dubs.skank_note >= 0) {
        note_name(dubs.skank_note, t);
        text_cat(t, " ");
    }
    text_cat(t, dub_chord_name(set.chord));
    cv_text(236 - text_w(&FONT_XS, t), 0, &FONT_XS, t, dubs.skank_note >= 0 ? C_WHITE : C_GRAY);
    if (st >= 0)
        cv_rect(43 + ps * 12, 14, 12, 56, C_LINE);
    for (r = 0; r < 4; r++) {
        uint16_t c = r == 0 ? C_RED : r == 1 ? C_GOLD : r == 2 ? C_GREEN : C_HI;
        int muted = r < 3 ? dubp.mute_drums : dubp.mute_skank;
        cv_text(4, 14 + r * 14, &FONT_XS, ROW[r], muted ? C_DIM : C_GRAY);
        for (i = 0; i < 16; i++) {
            int h = dub_pattern_hit(set.pattern, r, i);
            uint16_t fill = !h ? C_DIM : muted ? C_GRAY : (h == 2 ? C_WHITE : c);
            if (h)
                cv_rect(44 + i * 12, 16 + r * 14, 10, 10, fill);
            else
                cv_rect(48 + i * 12, 20 + r * 14, 2, 2, fill);
        }
    }
    cv_text(4, 78, &FONT_XS, "BASS", dubp.mute_bass ? C_DIM : C_GRAY);
    cv_text(4, 96, &FONT_XS, dubp.rec_on ? "REC" : "LOOP", dubp.rec_on ? C_RED : C_DIM);
    for (r = 0; r < 2; r++)
        for (i = 0; i < 16; i++) {
            int32_t s = r * 16 + i, x = 44 + i * 12, y = 76 + r * 20;
            uint8_t v = dubs.loop[s];
            if (s == st)
                cv_rect(x - 1, y, 12, 18, dubp.rec_on ? C_RED : C_LINE);
            if (v > 0 && v < DUB_TIE) {
                int32_t h = clamp(((int32_t)v - 24) * 16 / 36, 3, 16);
                cv_rect(x, y + 17 - h, 10, h, dubp.mute_bass ? C_GRAY : C_GOLD);
            } else if (v == DUB_TIE) {
                cv_rect(x - 2, y + 14, 12, 3, dubp.mute_bass ? C_GRAY : C_AMB);
            } else {
                cv_rect(x + 4, y + 15, 2, 2, C_DIM);
            }
        }
    /* the desk: channels in or out, the siren, the throw */
    box(4, 122, 52, "DRUMS", dubp.mute_drums ? C_LINE : C_GREEN, dubp.mute_drums ? C_GRAY : C_BLACK);
    box(60, 122, 46, "BASS", dubp.mute_bass ? C_LINE : C_GREEN, dubp.mute_bass ? C_GRAY : C_BLACK);
    box(110, 122, 52, "SKANK", dubp.mute_skank ? C_LINE : C_GREEN, dubp.mute_skank ? C_GRAY : C_BLACK);
    if (dubp.throw_on)
        box(166, 122, 70, "THROW", C_GOLD, C_BLACK);
    else
        box(166, 122, 70, dub_siren_name(set.siren), dubp.siren_on ? C_RED : C_LINE, dubp.siren_on ? C_WHITE : C_GRAY);
    cv_blit(0, 34);

    /* the knobs */
    cv_begin(240, 60, C_BLACK);
    {
        static const char *const LBL[4] = {"K1 FDBK", "K2 TIME", "K3 FILTER", "K4 SPRING"};
        float lvl[4];
        lvl[0] = (float)set.fb / 110.0f;
        lvl[1] = (float)(set.echo_div + 1) / (float)ECHO_N;
        lvl[2] = dubp.filter;
        lvl[3] = (float)set.spring / 100.0f;
        for (i = 0; i < 4; i++) {
            int32_t x = 4 + i * 59;
            cv_text(x, 4, &FONT_XS, LBL[i], C_GRAY);
            t[0] = 0;
            if (i == 0) {
                int_text(set.fb, t);
                text_cat(t, "%");
            } else if (i == 1) {
                text_cat(t, dub_echo_name(set.echo_div));
            } else if (i == 2) {
                filter_text(t);
            } else {
                int_text(set.spring, t);
                text_cat(t, "%");
            }
            cv_text(x, 20, &FONT_B, t, i == 0 && set.fb > 100 ? C_RED : C_HI);
            cv_rect(x, 48, 54, 4, C_DIM);
            if (i == 2) {                /* the filter: from the middle, left low-pass, right high-pass */
                int32_t w = (int32_t)(lvl[2] * 27.0f);
                if (w < 0)
                    cv_rect(x + 27 + w, 48, -w, 4, C_HI);
                else if (w > 0)
                    cv_rect(x + 27, 48, w, 4, C_HI);
                cv_rect(x + 26, 46, 2, 8, C_GRAY);
            } else {
                cv_rect(x, 48, (int32_t)(lvl[i] * 54.0f), 4, C_HI);
            }
        }
    }
    cv_blit(0, 180);
}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.last_step = -2;
    ui.redraw = 1;
}

/* a screen reader that has just started listening (or asks) hears the whole screen */
static void speak(void)
{
    char t[96], n[12];
    if (ann_on() && ann_where()) {
        t[0] = 0;
        text_cat(t, APP_NAME ". ");
        text_cat(t, dub_pattern_name(set.pattern));
        text_cat(t, ". ");
        int_text(set.bpm, n);
        text_cat(t, n);
        text_cat(t, " BPM. ");
        text_cat(t, dubs.playing ? "PLAYING" : "STOPPED");
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    midi_in();
    input();
    speak();
    if (dubs.step != ui.last_step) {     /* the playhead moved */
        ui.last_step = dubs.step;
        ui.redraw = 1;
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "playing"))
        *got = dubs.playing;
    else if (!strcmp(what, "step"))
        *got = dubs.step;
    else if (!strcmp(what, "bpm"))
        *got = set.bpm;
    else if (!strcmp(what, "pattern"))
        *got = set.pattern;
    else if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "bass"))
        *got = dubs.bass_note;
    else if (!strcmp(what, "skank"))
        *got = dubs.skank_note;
    else if (!strcmp(what, "rec"))
        *got = dubp.rec_on;
    else if (!strcmp(what, "siren"))
        *got = dubp.siren_on;
    else if (!strcmp(what, "throw"))
        *got = dubp.throw_on;
    else if (!strcmp(what, "mute_drums"))
        *got = dubp.mute_drums;
    else if (!strcmp(what, "mute_bass"))
        *got = dubp.mute_bass;
    else if (!strcmp(what, "mute_skank"))
        *got = dubp.mute_skank;
    else if (!strcmp(what, "fb"))
        *got = set.fb;
    else if (!strcmp(what, "spring"))
        *got = set.spring;
    else if (!strcmp(what, "filter"))    /* -20 .. 20 */
        *got = (int)(dubp.filter * 20.0f + (dubp.filter < 0.0f ? -0.5f : 0.5f));
    else if (!strncmp(what, "loop", 4))  /* loopN: the loop's step N (0 rest, 128 a tie) */
        *got = dubs.loop[atoi(what + 4) % DUB_LOOP];
    else
        return 0;
    return 1;
}
#endif
