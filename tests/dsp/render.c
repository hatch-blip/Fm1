/* SPDX-License-Identifier: MPL-2.0 */
/* DUBWISE's engine alone on a computer: a scripted dub session into a WAV, with checks.
 *   cc -O2 -std=c99 -DDUB_TEST -Iapp -o build/render tests/dsp/render.c app/dub.c && build/render out.wav
 * Exit 1 if a check fails. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "dub.h"

#define BLOCK 256u
static FILE *wav;
static uint32_t frames;
static double sumsq;
static int bad;
static double tick_iv, tick_next;        /* a MIDI clock coming in: samples between ticks (0: none) */
static uint32_t ticks_sent, clocks_out;

static void put16(FILE *f, uint32_t v) { fputc(v & 255, f); fputc(v >> 8 & 255, f); }
static void put32(FILE *f, uint32_t v) { put16(f, v & 0xFFFF); put16(f, v >> 16); }

static void run_ms(uint32_t ms)          /* render ms milliseconds in device-sized blocks */
{
    static float lr[2 * BLOCK];
    uint32_t total = (uint32_t)((double)ms * DUB_SR / 1000.0), i;
    while (total) {
        uint32_t n = total < BLOCK ? total : BLOCK;
        if (tick_iv > 0.0)               /* the ticks due, at the block's start (as the device reads MIDI) */
            while (tick_next <= (double)frames) {
                dub_midi_rt(0xF8);
                ticks_sent++;
                tick_next += tick_iv;
            }
        dub_render(lr, n);
        clocks_out += dubs.out_clocks;   /* (the app sends these) */
        dubs.out_clocks = 0;
        for (i = 0; i < 2 * n; i++) {
            float s = lr[i];
            if (s != s || s > 1.0f || s < -1.0f) {
                if (!bad)
                    fprintf(stderr, "FAIL: sample %g at frame %u\n", s, frames + i / 2);
                bad = 1;
                s = 0.0f;
            }
            sumsq += (double)s * s;
            if (wav)
                put16(wav, (uint16_t)(int16_t)(s * 32000.0f));
        }
        frames += n;
        total -= n;
    }
}

static void check(int ok, const char *what)
{
    printf("  %s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok)
        bad = 1;
}

static double rms_of(uint32_t ms)        /* the RMS of the next ms */
{
    double s0 = sumsq;
    uint32_t f0 = frames;
    run_ms(ms);
    return sqrt((sumsq - s0) / (2.0 * (frames - f0)));
}

int main(int argc, char **argv)
{
    uint32_t bar;
    if (argc > 1) {
        wav = fopen(argv[1], "wb");
        if (!wav) {
            perror(argv[1]);
            return 1;
        }
        fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, wav);
        put32(wav, 16); put16(wav, 1); put16(wav, 2); put32(wav, 44100); put32(wav, 44100 * 4); put16(wav, 4); put16(wav, 16);
        fwrite("data\0\0\0\0", 1, 8, wav);
    }
    dubp.bpm = 76;
    dubp.pattern = PAT_ONEDROP;
    dubp.echo_div = ECHO_8D;
    dubp.echo_fb = 0.55f;
    dubp.spring = 0.45f;
    dub_init();

    check(rms_of(300) < 1e-6, "silent before PLAY");
    /* bar length at 76 bpm: 4 beats = 3158 ms */
    dub_event(EV_PLAY, 0);
    run_ms(10);
    check(dubs.playing && dubs.step == 0, "PLAY starts at step 0");

    /* record a bassline over 2 bars: G1 . . Bb1 | C2 . Bb1 G1 (one note per beat, roughly) */
    dubp.rec_on = 1;
    {
        static const int notes[8] = {43, 0, 46, 0, 48, 0, 46, 43};
        int b;
        for (b = 0; b < 8; b++) {        /* each beat is 789.5 ms */
            if (notes[b]) {
                dub_event(EV_BASS_ON, notes[b]);
                run_ms(500);
                dub_event(EV_BASS_OFF, notes[b]);
                run_ms(289);
            } else {
                run_ms(789);
            }
        }
    }
    dubp.rec_on = 0;
    check(dubs.loop[0] == 43 || dubs.loop[1] == 43, "the loop starts with G1");
    check(dubs.loop[8] == 46 || dubs.loop[9] == 46, "Bb1 on beat 3");
    {
        int k, notes = 0, ties = 0;
        for (k = 0; k < DUB_LOOP; k++) {
            notes += dubs.loop[k] > 0 && dubs.loop[k] < DUB_TIE;
            ties += dubs.loop[k] == DUB_TIE;
        }
        check(notes == 5 && ties > 5, "5 notes recorded, held with ties");
    }

    /* the loop plays back on its own, then the skank comes in (Gm), then a throw, then the siren */
    run_ms(30);
    check(dubs.bass_note == 43, "the loop plays G1 back at its start");
    run_ms(3128);
    dub_event(EV_SKANK_ON, 55);
    for (bar = 0; bar < 2; bar++)
        run_ms(3158);
    dubp.throw_on = 1;
    run_ms(800);
    dubp.throw_on = 0;
    dub_event(EV_SKANK_OFF, 55);
    dubp.mute_drums = 1;                 /* the drop: the echo tail alone over the bass */
    run_ms(3158);
    dubp.mute_drums = 0;
    dub_event(EV_CRASH, 0);
    dubp.siren_on = 1;
    dubp.siren_mode = SIREN_WAIL;
    run_ms(1579);
    dubp.siren_mode = SIREN_LASER;
    run_ms(1579);
    dubp.siren_on = 0;
    dubp.echo_fb = 1.05f;                /* runaway echo, held by the tape */
    dubp.echo_div = ECHO_4;              /* and the time changed: the pitch bends */
    run_ms(3158);
    dubp.echo_fb = 0.4f;
    {                                    /* the filter: low-pass down, then high-pass up */
        int s;
        for (s = 0; s <= 20; s++) {
            dubp.filter = -0.05f * (float)s;
            run_ms(80);
        }
        double lp = rms_of(400);
        dubp.filter = 0.0f;
        run_ms(600);
        for (s = 0; s <= 20; s++) {
            dubp.filter = 0.05f * (float)s;
            run_ms(80);
        }
        double hp = rms_of(400);
        dubp.filter = 0.0f;
        double open = rms_of(1600);
        printf("  rms: open %.3f, low-passed %.3f, high-passed %.3f\n", open, lp, hp);
        check(lp < open * 0.7 && hp < open * 0.7, "the DJ filter cuts both ways");
    }

    /* trippy: repeats spiralling down an octave, a warped tape, the phaser in the wet */
    dubp.shift = 0;
    dubp.warp = 0.8f;
    dubp.space = 0.8f;
    dub_event(EV_SKANK_ON, 58);
    dubp.throw_on = 1;
    run_ms(1600);
    dubp.throw_on = 0;
    dub_event(EV_SKANK_OFF, 58);
    run_ms(3158);
    dubp.shift = 6;                      /* and up */
    dubp.throw_on = 1;
    dub_event(EV_SKANK_ON, 55);
    run_ms(800);
    dubp.throw_on = 0;
    dub_event(EV_SKANK_OFF, 55);
    run_ms(3158);
    dubp.shift = DUB_SHIFT_OFF;
    dubp.warp = 0.0f;
    dubp.space = 0.0f;

    /* freeze: a throw caught on the tape, then everything else out: it keeps going */
    dubp.echo_fb = 0.5f;
    dub_event(EV_SKANK_ON, 55);
    dubp.throw_on = 1;
    run_ms(1579);
    dubp.throw_on = 0;
    dub_event(EV_SKANK_OFF, 55);
    dubp.freeze = 1;
    dubp.mute_drums = dubp.mute_bass = dubp.mute_skank = 1;
    dubp.spring = 0.0f;
    run_ms(500);
    {
        double a = rms_of(1000);
        run_ms(4000);
        double b = rms_of(1000);
        printf("  freeze rms: %.4f, 5 s later %.4f\n", a, b);
        check(a > 0.01 && b > a * 0.6, "a frozen tape holds");
        dubp.freeze = 0;
        run_ms(5000);
        double c = rms_of(1000);
        printf("  thawed, 5 s later %.4f\n", c);
        check(c < b * 0.3, "thawed, it fades");
    }
    dubp.mute_drums = dubp.mute_bass = dubp.mute_skank = 0;
    dubp.spring = 0.45f;

    /* the robot: at full appetite it makes moves of its own */
    {
        uint32_t c0 = dubs.auto_count, seen = 0;
        int b;
        dubp.auto_amt = 1.0f;
        for (b = 0; b < 32; b++) {       /* 8 bars, a beat at a time */
            uint32_t c = dubs.auto_count;
            run_ms(789);
            if (dubs.auto_count != c)
                seen |= 1u << dubs.auto_move;
        }
        dubp.auto_amt = 0.0f;
        printf("  auto: %u moves in 8 bars, kinds seen %03X\n", dubs.auto_count - c0, seen);
        check(dubs.auto_count - c0 >= 6 && __builtin_popcount(seen) >= 3, "auto dub moves");
        run_ms(3158);                    /* (its last moves run out) */
    }

    /* clock out: 24 a beat on our own tempo */
    dubp.clock_out = 1;
    clocks_out = 0;
    run_ms(3158);                        /* a bar at 76 */
    printf("  clocks out in a bar: %u\n", clocks_out);
    check(clocks_out >= 95 && clocks_out <= 97, "MIDI clock out at 24 per beat");
    dubp.clock_out = 0;

    dub_event(EV_STOP, 0);
    run_ms(4000);
    check(!dubs.playing && dubs.step == -1, "STOP");

    /* following a MIDI clock at 120: start, the steps on every sixth tick, the tempo, stop */
    tick_iv = 44100.0 * 60.0 / (120.0 * 24.0);
    tick_next = (double)frames;
    run_ms(1000);                        /* the clock running, stopped */
    check(dubs.ext && !dubs.playing, "a clock coming in, not started");
    dub_midi_rt(0xFA);
    ticks_sent = 0;
    run_ms(4000);
    printf("  following: %.1f bpm, %u ticks, step %d\n", dubs.bpm_now, ticks_sent, dubs.step);
    check(dubs.bpm_now > 118.0f && dubs.bpm_now < 122.0f, "the clock's tempo, 120");
    check(dubs.playing && dubs.step == (int)((ticks_sent - 1u) / 6u) % DUB_LOOP, "a step every 6 ticks from start");
    dub_midi_rt(0xFC);
    run_ms(100);
    check(!dubs.playing, "MIDI stop");
    tick_iv = 0.0;                       /* the clock goes away: our own tempo again */
    run_ms(800);
    check(!dubs.ext && dubs.bpm_now == 76.0f, "back on our own tempo without a clock");
    check(dubs.peak < 0.02f, "tails die away after STOP");

    if (wav) {
        uint32_t bytes = frames * 4u;
        fseek(wav, 4, SEEK_SET);
        put32(wav, 36 + bytes);
        fseek(wav, 40, SEEK_SET);
        put32(wav, bytes);
        fclose(wav);
        printf("  wrote %s (%.1f s)\n", argv[1], frames / 44100.0);
    }
    check(!bad, "no sample out of range or NaN");
    return bad;
}
