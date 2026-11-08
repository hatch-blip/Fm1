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
 *   SELECT tempo  ALGO siren  PRESET riddim  K1 echo feedback  K2 echo time  K3 filter  K4 spring
 *   SEL held:     K1 shift (the repeats transposed)  K2 warp  K3 auto dub  K4 space
 *                 FX freeze the tape   PLAY MIDI clock out on / off
 *   SEL held:     SELECT / ALGO / PRESET the MIDI channels of the bass, the skank, the drums
 * With the band (a BeatStep Pro's defaults): it follows MIDI clock in (USB or TRS); channel 1 plays the bass, 2 stabs
 * the skank, 10 plays the drums (GM notes); CCs 12..26 on any of those channels work the desk (README.md). */
#include "dub.h"

#define SET_MAGIC 0x44554233u            /* "DUB3": the settings record; another magic loads defaults */
typedef struct {
    uint32_t magic;
    uint8_t bpm, pattern, siren, chord, echo_div, fb, spring;
    int8_t octave;
    uint8_t shift, warp, auto_amt, space, clock_out;   /* the SEL page: shift index, the rest 0..100 %, clock 0/1 */
    uint8_t ch[3];                       /* MIDI channels 0..15 (16: off) of the bass, the skank, the drums */
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

/* MIDI in, read here in the audio interrupt, each message placed in the coming block by when it landed (fm1.toml
 * midi_stamp), so a clock and a sequencer keep their spacing to a sample or so: a constant one block late.
 *   the bass channel: the bass (held notes, legato glides)
 *   the skank channel: a chord stab on each note (the sequencer plays the rhythm); the same channel as the bass:
 *     split at C3, the lower notes the bass, the upper ones skank held as the keys are (chopped on the offbeats)
 *   the drum channel: the drums, by General MIDI note
 *   CCs on any of them: the desk (cc_apply, in the main loop); real time: the clock, start, continue, stop */
#define CCQ 64u
static volatile uint32_t ccq[CCQ], ccq_w, ccq_r;   /* CCs for the main loop: number << 8 | value */

static void midi_in(uint32_t n)
{
    uint32_t pkt, blk = n < APP_BLOCK ? n : APP_BLOCK, now = plat_ticks24();
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24, ty = st & 0xF0u, ch = st & 0x0Fu;
        uint32_t age = (now - plat_midi_in_t24()) / PLAT_TICKS_PER_US;   /* us since it landed */
        int32_t at = (int32_t)blk - 1 - (int32_t)(age * 441u / 10000u);
        int on = ty == 0x90u && d2, off = ty == 0x80u || (ty == 0x90u && !d2);
        if (at < 0)
            at = 0;
        if (st >= 0xF8u) {
            dub_midi_rt(st, at);
            continue;
        }
        if (ty == 0xB0u && (ch == set.ch[0] || ch == set.ch[1] || ch == set.ch[2])) {
            uint32_t w = ccq_w;
            if (w - ccq_r < CCQ) {
                ccq[w % CCQ] = d1 << 8 | (d2 & 0x7Fu);
                ccq_w = w + 1u;
            }
            continue;
        }
        if (!on && !off)
            continue;
        if (ch == set.ch[0] && (ch != set.ch[1] || d1 < 48u))
            dub_event_isr(on ? EV_BASS_ON : EV_BASS_OFF, DUB_NV(d1, d2), at);
        else if (ch == set.ch[1] && ch == set.ch[0])
            dub_event_isr(on ? EV_SKANK_ON : EV_SKANK_OFF, DUB_NV(d1, d2), at);
        else if (ch == set.ch[1] && on)
            dub_event_isr(EV_STAB, DUB_NV(d1, d2), at);
        else if (ch == set.ch[2] && on)
            dub_event_isr(EV_DRUM, DUB_NV(d1, d2), at);
    }
}

static void midi_out(void)               /* the engine's clock out, over USB (single-byte real time, CIN F) */
{
    if (dubs.out_start)
        plat_midi_send(0x0Fu | 0xFAu << 8, TO_USB);
    while (dubs.out_clocks) {
        plat_midi_send(0x0Fu | 0xF8u << 8, TO_USB);
        dubs.out_clocks--;
    }
    if (dubs.out_stop)
        plat_midi_send(0x0Fu | 0xFCu << 8, TO_USB);
    dubs.out_start = dubs.out_stop = 0;
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;   /* MASTER, read once a block; 24-bit out */
    uint32_t done = 0, i;
    midi_in(n);
    while (done < n) {
        uint32_t m = n - done > APP_BLOCK ? APP_BLOCK : n - done;
        dub_render(buf, m);
        for (i = 0; i < m; i++) {
            gain_now += (gain - gain_now) * 0.004f;
            lr[2 * (done + i)] = (int32_t)(buf[2 * i] * gain_now);
            lr[2 * (done + i) + 1] = (int32_t)(buf[2 * i + 1] * gain_now);
        }
        done += m;
        midi_out();
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
    set.shift = DUB_SHIFT_OFF;
    set.warp = 0;
    set.auto_amt = 0;
    set.space = 0;
    set.clock_out = 0;                   /* a follower by default: the band's clock leads */
    set.ch[0] = 0;                       /* a BeatStep Pro's: sequencer 1, sequencer 2, drums */
    set.ch[1] = 1;
    set.ch[2] = 9;
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
    dubp.shift = set.shift;
    dubp.warp = (float)set.warp / 100.0f;
    dubp.auto_amt = (float)set.auto_amt / 100.0f;
    dubp.space = (float)set.space / 100.0f;
    dubp.clock_out = set.clock_out;
    for (i = 0; i < DUB_LOOP; i++)
        dubs.loop[i] = set.loop[i];
}

void app_init(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.bpm < 60 || set.bpm > 180 || set.pattern >= PAT_N || set.siren >= SIREN_N || set.chord >= CHORD_N
        || set.echo_div >= ECHO_N || set.fb > 110 || set.spring > 100 || set.octave < -2 || set.octave > 2
        || set.shift >= DUB_SHIFT_N || set.warp > 100 || set.auto_amt > 100 || set.space > 100 || set.clock_out > 1
        || set.ch[0] > 16 || set.ch[1] > 16 || set.ch[2] > 16)
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
    uint8_t sel_used;                    /* SEL held and something done with it: no chord change on its release */
    uint32_t auto_seen;                  /* the robot's move count last shown */
    uint8_t cc_throw, cc_siren;          /* held by a CC (a controller's pad or button) */
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

static void ch_text(uint32_t c, char *t)   /* "1".."16", "OFF" */
{
    t[0] = 0;
    if (c > 15u)
        text_cat(t, "OFF");
    else
        int_text((int32_t)c + 1, t);
}

/* the CCs that came in (midi_in): the desk from a controller. 12 echo feedback, 13 echo time, 14 filter (64 open),
 * 15 spring, 16 shift, 17 warp, 18 auto dub, 19 space; 64 and up: 20 throw, 21 freeze, 22 siren, 23 crash (each
 * time it goes up), 24 25 26 drums / bass / skank out */
static void cc_apply(void)
{
    static uint8_t crash_was;
    while (ccq_r != ccq_w) {
        uint32_t c = ccq[ccq_r % CCQ], cc = c >> 8, v = c & 0x7Fu, hi = v >= 64u;
        ccq_r++;
        switch (cc) {
        case 12: set.fb = (uint8_t)(v * 110u / 127u); dubp.echo_fb = (float)set.fb / 100.0f; break;
        case 13: set.echo_div = (uint8_t)(v * ECHO_N / 128u); dubp.echo_div = set.echo_div; break;
        case 14: {
            int32_t w = v >= 62u && v <= 66u ? 0 : ((int32_t)v - 64) * 20 / 63;
            dubp.filter = (float)clamp(w, -20, 20) / 20.0f;
            break;
        }
        case 15: set.spring = (uint8_t)(v * 100u / 127u); dubp.spring = (float)set.spring / 100.0f; break;
        case 16: set.shift = (uint8_t)(v * DUB_SHIFT_N / 128u); dubp.shift = set.shift; break;
        case 17: set.warp = (uint8_t)(v * 100u / 127u); dubp.warp = (float)set.warp / 100.0f; break;
        case 18: set.auto_amt = (uint8_t)(v * 100u / 127u); dubp.auto_amt = (float)set.auto_amt / 100.0f; break;
        case 19: set.space = (uint8_t)(v * 100u / 127u); dubp.space = (float)set.space / 100.0f; break;
        case 20: ui.cc_throw = (uint8_t)hi; break;
        case 21: dubp.freeze = (uint8_t)hi; break;
        case 22: ui.cc_siren = (uint8_t)hi; break;
        case 23:
            if (hi && !crash_was)
                dub_event(EV_CRASH, 0);
            crash_was = (uint8_t)hi;
            break;
        case 24: dubp.mute_drums = (uint8_t)hi; break;
        case 25: dubp.mute_bass = (uint8_t)hi; break;
        case 26: dubp.mute_skank = (uint8_t)hi; break;
        default: continue;
        }
        if (cc <= 19u)
            set_dirty = 1;
        ui.redraw = 1;
    }
}

static void input(void)
{
    uint32_t keys = plat_keys(), btn = plat_buttons(), down = btn & ~ui.btn, k, leds, key_leds;
    uint32_t sel = btn >> B_SEL & 1u;    /* SEL held: the second page */
    int32_t e;
    cc_apply();
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            ui.key_note[k] = key_note(k);
            dub_event(k < SPLIT ? EV_BASS_ON : EV_SKANK_ON, ui.key_note[k]);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            dub_event(k < SPLIT ? EV_BASS_OFF : EV_SKANK_OFF, ui.key_note[k]);
        }
    }
    if (down >> B_SEL & 1u)
        ui.sel_used = 0;
    if (sel && (down & (1u << B_PLAY | 1u << B_FX)))
        ui.sel_used = 1;
    if (sel && (down >> B_PLAY & 1u)) {  /* SEL + PLAY: MIDI clock out */
        set.clock_out = (uint8_t)!set.clock_out;
        dubp.clock_out = set.clock_out;
        set_dirty = 1;
        app_say("CLOCK OUT", set.clock_out ? "ON" : "OFF");
    } else if (down >> B_PLAY & 1u) {
        play_toggle();
    }
    if (sel && (down >> B_FX & 1u)) {    /* SEL + FX: freeze the tape */
        dubp.freeze = (uint8_t)!dubp.freeze;
        app_say("FREEZE", dubp.freeze ? "ON" : "OFF");
    }
    if (down >> B_SEQ & 1u) {
        set.pattern = (uint8_t)((set.pattern + 1u) % PAT_N);
        dubp.pattern = set.pattern;
        set_dirty = 1;
        app_say("RIDDIM", dub_pattern_name(set.pattern));
    }
    if ((ui.btn >> B_SEL & 1u) && !sel && !ui.sel_used) {   /* SEL tapped: the chord */
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
    if ((uint8_t)((btn >> B_FX & 1u && !sel) || ui.cc_throw) != dubp.throw_on) {
        dubp.throw_on = (uint8_t)((btn >> B_FX & 1u && !sel) || ui.cc_throw);
        if (dubp.throw_on)
            app_say("THROW", "");
        ui.redraw = 1;
    }
    if ((uint8_t)(btn >> B_ARP & 1u || ui.cc_siren) != dubp.siren_on) {
        dubp.siren_on = (uint8_t)(btn >> B_ARP & 1u || ui.cc_siren);
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
    if (sel) {                           /* the SEL page's top row: the MIDI channels */
        static const int ENC[3] = {EN_SELECT, EN_ALGO, EN_PRESET};
        static const char *const WHAT[3] = {"BASS CH", "SKANK CH", "DRUM CH"};
        uint32_t r;
        for (r = 0; r < 3u; r++)
            if ((e = plat_enc(ENC[r])) != 0) {
                char t[8];
                set.ch[r] = (uint8_t)clamp((int32_t)set.ch[r] + (e > 0 ? 1 : -1), 0, 16);
                ch_text(set.ch[r], t);
                app_say(WHAT[r], t);
                ui.sel_used = 1;
                set_dirty = 1;
            }
    }
    if (!sel && (e = plat_enc(EN_SELECT)) != 0) {
        set.bpm = (uint8_t)clamp((int32_t)set.bpm + e, 60, 180);
        dubp.bpm = set.bpm;
        set_dirty = 1;
        say_int("TEMPO", set.bpm, " BPM");
    }
    if (!sel && (e = plat_enc(EN_ALGO)) != 0) {
        set.siren = (uint8_t)(((int32_t)set.siren + e % SIREN_N + SIREN_N) % SIREN_N);
        dubp.siren_mode = set.siren;
        set_dirty = 1;
        app_say("SIREN", dub_siren_name(set.siren));
    }
    if (!sel && (e = plat_enc(EN_PRESET)) != 0) {
        set.pattern = (uint8_t)(((int32_t)set.pattern + e % PAT_N + PAT_N) % PAT_N);
        dubp.pattern = set.pattern;
        set_dirty = 1;
        app_say("RIDDIM", dub_pattern_name(set.pattern));
    }
    if (sel) {                           /* the SEL page: the trippy one */
        if ((e = plat_enc(EN_K1)) != 0) {
            set.shift = (uint8_t)clamp((int32_t)set.shift + (e > 0 ? 1 : -1), 0, DUB_SHIFT_N - 1);
            dubp.shift = set.shift;
            app_say("SHIFT", dub_shift_name(set.shift));
            ui.sel_used = 1;
            set_dirty = 1;
        }
        if ((e = plat_enc(EN_K2)) != 0) {
            set.warp = (uint8_t)clamp((int32_t)set.warp + 4 * e, 0, 100);
            dubp.warp = (float)set.warp / 100.0f;
            say_int("WARP", set.warp, "%");
            ui.sel_used = 1;
            set_dirty = 1;
        }
        if ((e = plat_enc(EN_K3)) != 0) {
            set.auto_amt = (uint8_t)clamp((int32_t)set.auto_amt + 4 * e, 0, 100);
            dubp.auto_amt = (float)set.auto_amt / 100.0f;
            say_int("AUTO DUB", set.auto_amt, "%");
            ui.sel_used = 1;
            set_dirty = 1;
        }
        if ((e = plat_enc(EN_K4)) != 0) {
            set.space = (uint8_t)clamp((int32_t)set.space + 4 * e, 0, 100);
            dubp.space = (float)set.space / 100.0f;
            say_int("SPACE", set.space, "%");
            ui.sel_used = 1;
            set_dirty = 1;
        }
    }
    if (!sel && (e = plat_enc(EN_K1)) != 0) {
        set.fb = (uint8_t)clamp((int32_t)set.fb + 2 * e, 0, 110);
        dubp.echo_fb = (float)set.fb / 100.0f;
        set_dirty = 1;
        say_int("FEEDBACK", set.fb, "%");
    }
    if (!sel && (e = plat_enc(EN_K2)) != 0) {
        set.echo_div = (uint8_t)clamp((int32_t)set.echo_div + (e > 0 ? 1 : -1), 0, ECHO_N - 1);
        dubp.echo_div = set.echo_div;
        set_dirty = 1;
        app_say("ECHO", dub_echo_name(set.echo_div));
    }
    if (!sel && (e = plat_enc(EN_K3)) != 0) {   /* the filter snaps open as it crosses the middle */
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
    if (!sel && (e = plat_enc(EN_K4)) != 0) {
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
           | (dubp.mute_skank ? 0u : 1u << B_EDIT) | (dubp.throw_on || dubp.freeze ? 1u << B_FX : 0u)
           | (dubp.siren_on ? 1u << B_ARP : 0u) | (set_dirty ? 1u << B_SAVE : 0u) | (btn & (1u << B_GLO | 1u << B_SEQ));
    key_leds = keys;
    if (dubs.playing && dubs.step >= 0) {
        static const uint8_t WK[16] = PLAT_WHITE_KEYS;
        key_leds |= 1u << WK[dubs.step & 15];
    }
    plat_glow(1);
    plat_leds(leds, key_leds);
}

void app_input_only(void) { input(); }   /* (MIDI in is read in the render: app_render) */

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
            if (ui.btn >> B_SEL & 1u) {  /* SEL held: the MIDI channels (SELECT / ALGO / PRESET set them) */
                static const char *const L[3] = {"B", " S", " D"};
                uint32_t r;
                for (r = 0; r < 3u; r++) {
                    text_cat(t, L[r]);
                    ch_text(set.ch[r], o);
                    text_cat(t, o[0] == 'O' ? "-" : o);
                }
                cv_text(x + 8, 12, &FONT_XS, t, C_GOLD);
            } else {
                text_cat(t, set.octave >= 0 ? "OCT +" : "OCT ");
                int_text(set.octave, o);
                text_cat(t, o);
                cv_text(x + 10, 12, &FONT_XS, t, C_GRAY);
            }
        }
        int_text((int32_t)(dubs.bpm_now + 0.5f), t);
        x = cv_text(200 - text_w(&FONT_B, t), 6, &FONT_B, t, dubs.playing ? C_WHITE : C_GRAY);
        cv_text(x + 4, 4, &FONT_XS, dubs.ext ? "EXT" : (set.clock_out ? "OUT" : ""), dubs.ext ? C_GREEN : C_DIM);
        cv_text(x + 4, 16, &FONT_XS, "BPM", C_GRAY);
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
    /* (green: in; grey: out by hand; red: out by the robot) */
    box(4, 122, 52, "DRUMS", dubp.mute_drums ? C_LINE : dubs.auto_drums_out ? C_RED : C_GREEN,
        dubp.mute_drums ? C_GRAY : C_BLACK);
    box(60, 122, 46, "BASS", dubp.mute_bass ? C_LINE : dubs.auto_bass_out ? C_RED : C_GREEN,
        dubp.mute_bass ? C_GRAY : C_BLACK);
    box(110, 122, 52, "SKANK", dubp.mute_skank ? C_LINE : C_GREEN, dubp.mute_skank ? C_GRAY : C_BLACK);
    if (dubp.throw_on || dubs.auto_throw)
        box(166, 122, 70, "THROW", C_GOLD, C_BLACK);
    else if (dubp.freeze)
        box(166, 122, 70, "FREEZE", C_WHITE, C_BLACK);
    else
        box(166, 122, 70, dub_siren_name(set.siren), dubp.siren_on || dubs.auto_siren ? C_RED : C_LINE,
            dubp.siren_on || dubs.auto_siren ? C_WHITE : C_GRAY);
    cv_blit(0, 34);

    /* the knobs */
    cv_begin(240, 60, C_BLACK);
    {
        static const char *const LBL[2][4] = {{"K1 FDBK", "K2 TIME", "K3 FILTER", "K4 SPRING"},
                                              {"K1 SHIFT", "K2 WARP", "K3 AUTO", "K4 SPACE"}};
        int pg = (int)(ui.btn >> B_SEL & 1u);
        float lvl[4];
        if (pg) {                        /* SEL held: the trippy page, its labels lit */
            lvl[0] = ((float)set.shift - DUB_SHIFT_OFF) / (float)DUB_SHIFT_OFF;
            lvl[1] = (float)set.warp / 100.0f;
            lvl[2] = (float)set.auto_amt / 100.0f;
            lvl[3] = (float)set.space / 100.0f;
        } else {
            lvl[0] = (float)set.fb / 110.0f;
            lvl[1] = (float)(set.echo_div + 1) / (float)ECHO_N;
            lvl[2] = dubp.filter;
            lvl[3] = (float)set.spring / 100.0f;
        }
        for (i = 0; i < 4; i++) {
            int32_t x = 4 + i * 59;
            int bip = pg ? i == 0 : i == 2;   /* drawn from the middle: shift, filter */
            cv_text(x, 4, &FONT_XS, LBL[pg][i], pg ? C_GOLD : C_GRAY);
            t[0] = 0;
            if (pg) {
                if (i == 0) {
                    text_cat(t, dub_shift_name(set.shift));
                } else {
                    int_text(i == 1 ? set.warp : i == 2 ? set.auto_amt : set.space, t);
                    text_cat(t, "%");
                }
            } else if (i == 0) {
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
            cv_text(x, 20, &FONT_B, t, !pg && i == 0 && set.fb > 100 ? C_RED : C_HI);
            cv_rect(x, 48, 54, 4, C_DIM);
            if (bip) {                   /* from the middle: left down, right up */
                int32_t w = (int32_t)(lvl[i] * 27.0f);
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
    input();
    speak();
    if (dubs.step != ui.last_step) {     /* the playhead moved */
        ui.last_step = dubs.step;
        ui.redraw = 1;
    }
    if (dubs.auto_count != ui.auto_seen) {   /* the robot made a move: say which */
        ui.auto_seen = dubs.auto_count;
        app_say("AUTO", dub_auto_name(dubs.auto_move));
    }
    {
        static uint32_t last_sig;
        uint32_t sig = (uint32_t)dubs.ext | (uint32_t)(dubs.bpm_now + 0.5f) << 1 | (uint32_t)dubs.auto_drums_out << 10
                       | (uint32_t)dubs.auto_bass_out << 11 | (uint32_t)dubs.auto_throw << 12
                       | (uint32_t)dubs.auto_siren << 13 | (uint32_t)dubp.freeze << 14;
        if (sig != last_sig) {
            last_sig = sig;
            ui.redraw = 1;
        }
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
    else if (!strcmp(what, "freeze"))
        *got = dubp.freeze;
    else if (!strcmp(what, "shift"))
        *got = set.shift;
    else if (!strcmp(what, "warp"))
        *got = set.warp;
    else if (!strcmp(what, "auto"))
        *got = set.auto_amt;
    else if (!strcmp(what, "space"))
        *got = set.space;
    else if (!strcmp(what, "clock_out"))
        *got = set.clock_out;
    else if (!strcmp(what, "chord"))
        *got = set.chord;
    else if (!strncmp(what, "ch", 2))   /* ch0 ch1 ch2: the bass, skank and drum channels (0-based, 16 off) */
        *got = set.ch[atoi(what + 2) % 3];
    else if (!strcmp(what, "stabs"))
        *got = (int)dubs.stabs;
    else if (!strcmp(what, "hits"))
        *got = (int)dubs.hits;
    else if (!strcmp(what, "ext"))
        *got = dubs.ext;
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
