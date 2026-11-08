/* SPDX-License-Identifier: MPL-2.0 */
/* DUBWISE's sound engine: a riddim (drums), a sub bass with a 2-bar loop recorder, auto-skank chords, a dub siren,
 * a tape echo, a spring reverb and a DJ filter. Pure C, no platform calls: the app (app.c) drives it from the main
 * loop through dubp (parameters) and dub_event (notes and actions); dub_render runs in the audio interrupt.
 * tests/dsp/render.c runs it alone on a computer. */
#pragma once
#include <stdint.h>

#define DUB_SR 44100.0f
#define DUB_LOOP 32                      /* the bass loop: 2 bars of 16ths */
#define DUB_TIE 128u                     /* a loop step that holds the note before it */

enum { PAT_ONEDROP, PAT_ROCKERS, PAT_STEPPERS, PAT_N };
enum { SIREN_WAIL, SIREN_WHOOP, SIREN_ALARM, SIREN_LASER, SIREN_N };
enum { CHORD_MIN, CHORD_MAJ, CHORD_MIN7, CHORD_N };
enum { ECHO_8, ECHO_8D, ECHO_4, ECHO_4D, ECHO_N };   /* the echo's time: 1/8, dotted 1/8, 1/4, dotted 1/4 */

enum {                                   /* dub_event types */
    EV_BASS_ON = 1, EV_BASS_OFF, EV_SKANK_ON, EV_SKANK_OFF,
    EV_PLAY, EV_STOP, EV_CRASH, EV_CLEAR_LOOP, EV_PANIC
};

typedef struct {                         /* written by the main loop, read once a block by the render */
    volatile int32_t bpm;                /* 60 .. 180 */
    volatile int32_t pattern;            /* PAT_* */
    volatile int32_t siren_mode;         /* SIREN_* */
    volatile int32_t chord;              /* CHORD_* */
    volatile int32_t echo_div;           /* ECHO_* */
    volatile float echo_fb;              /* 0 .. 1.1 (past 1 it runs away, held by the tape's saturation) */
    volatile float filter;               /* -1 low-pass .. 0 open .. +1 high-pass */
    volatile float spring;               /* the spring reverb's level, 0 .. 1 */
    volatile uint8_t mute_drums, mute_bass, mute_skank;
    volatile uint8_t throw_on;           /* everything but kick and bass into the echo */
    volatile uint8_t siren_on;
    volatile uint8_t rec_on;             /* bass notes played go into the loop */
} dub_params_t;

typedef struct {                         /* written by the render, read by the UI */
    volatile int32_t step;               /* 0 .. 31, -1 stopped */
    volatile uint8_t playing;
    volatile int32_t bass_note;          /* sounding, -1 none */
    volatile int32_t skank_note;         /* held, -1 none */
    volatile float peak;                 /* the output's peak over the last block */
    volatile uint8_t loop[DUB_LOOP];     /* 0 rest, 1..127 a note, DUB_TIE */
} dub_state_t;

extern dub_params_t dubp;
extern dub_state_t dubs;

void dub_init(void);                     /* boot, before the audio starts (keeps dubp and dubs.loop) */
void dub_event(uint32_t type, int32_t arg);   /* main loop: queued for the next block */
void dub_render(float *lr, uint32_t n);  /* audio interrupt: n interleaved stereo frames, about +-1 */
const char *dub_pattern_name(int32_t p);
const char *dub_siren_name(int32_t m);
const char *dub_chord_name(int32_t c);
const char *dub_echo_name(int32_t d);
/* the drum pattern's hits, for the screen: inst 0 kick, 1 rim, 2 hat (2 = open), 3 skank */
int dub_pattern_hit(int32_t p, int inst, int step);
