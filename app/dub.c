/* SPDX-License-Identifier: MPL-2.0 */
/* DUBWISE's sound engine (dub.h). Compiled apart at -O2 (fm1.toml o2), so it keeps to its own headers. */
#include "dub.h"
#include "dubmath.h"

#ifdef DUB_TEST                          /* tests/dsp/render.c: on a computer, no platform */
#define DUB_POOL
#else
#include "plat.h"
#define DUB_POOL PLAT_POOL               /* big zero-initialised buffers in the device's pool RAM */
#endif

#define SR DUB_SR
#define INV_SR (1.0f / DUB_SR)

dub_params_t dubp;
dub_state_t dubs;

/* ------------------------------------------------------------- patterns --- */
typedef struct {
    const char *name;
    const char *kick, *rim, *hat;        /* 16 steps: x a hit, o an open hat */
    uint16_t skank;                      /* the steps a held skank chord plays on */
    float swing;                         /* the odd 16ths late by this much of a step */
} pat_t;

static const pat_t PAT[PAT_N] = {
    {"ONE DROP", "........x.......", "........x.......", "x.x.x.x.x.x.x.xo", 1u << 4 | 1u << 12, 0.16f},
    {"ROCKERS", "x.......x.......", "....x.......x...", "x.xxx.x.x.xxx.xo", 1u << 4 | 1u << 12, 0.10f},
    {"STEPPERS", "x...x...x...x...", "........x.......", "x.o.x.o.x.o.x.o.", 1u << 2 | 1u << 6 | 1u << 10 | 1u << 14,
     0.0f},
};

const char *dub_pattern_name(int32_t p) { return PAT[(uint32_t)p % PAT_N].name; }
const char *dub_siren_name(int32_t m)
{
    static const char *const N[SIREN_N] = {"WAIL", "WHOOP", "ALARM", "LASER"};
    return N[(uint32_t)m % SIREN_N];
}
const char *dub_chord_name(int32_t c)
{
    static const char *const N[CHORD_N] = {"MIN", "MAJ", "MIN7"};
    return N[(uint32_t)c % CHORD_N];
}
const char *dub_echo_name(int32_t d)
{
    static const char *const N[ECHO_N] = {"1/8", "1/8.", "1/4", "1/4."};
    return N[(uint32_t)d % ECHO_N];
}
int dub_pattern_hit(int32_t p, int inst, int step)
{
    const pat_t *t = &PAT[(uint32_t)p % PAT_N];
    step &= 15;
    if (inst == 0)
        return t->kick[step] == 'x';
    if (inst == 1)
        return t->rim[step] == 'x';
    if (inst == 2)
        return t->hat[step] == 'x' ? 1 : t->hat[step] == 'o' ? 2 : 0;
    return (t->skank >> step) & 1u;
}

/* ---------------------------------------------------------------- events --- */
#define EVQ 64u
static volatile uint32_t ev_type[EVQ];
static volatile int32_t ev_arg[EVQ];
static volatile uint32_t ev_w, ev_r;

void dub_event(uint32_t type, int32_t arg)
{
    uint32_t w = ev_w;
    if (w - ev_r >= EVQ)
        return;                          /* full: dropped (64 in one block would be a lot of fingers) */
    ev_type[w % EVQ] = type;
    ev_arg[w % EVQ] = arg;
    ev_w = w + 1u;
}

/* -------------------------------------------------------------- helpers --- */
static uint32_t rng = 0x2545F491u;
static inline float noise(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (float)(int32_t)rng * (1.0f / 2147483648.0f);
}
static inline float mtof(float note) { return 440.0f * dm_exp2((note - 69.0f) * (1.0f / 12.0f)); }

/* decay factors, worked out once */
static float k_kick_p, k_kick_a, k_rim1, k_rim2, k_rimn, k_hat_c, k_hat_o, k_sk_a, k_sk_f, k_crash;

/* held notes: a small stack, the last one wins and a release falls back to the one under it */
typedef struct { int32_t n[8]; int32_t count; } stack_t;
static void stack_push(stack_t *s, int32_t note)
{
    int32_t i, j;
    for (i = 0; i < s->count; i++)       /* already there: moved to the top */
        if (s->n[i] == note) {
            for (j = i; j < s->count - 1; j++)
                s->n[j] = s->n[j + 1];
            s->count--;
            break;
        }
    if (s->count == 8) {
        for (j = 0; j < 7; j++)
            s->n[j] = s->n[j + 1];
        s->count--;
    }
    s->n[s->count++] = note;
}
static void stack_pop(stack_t *s, int32_t note)
{
    int32_t i, j;
    for (i = 0; i < s->count; i++)
        if (s->n[i] == note) {
            for (j = i; j < s->count - 1; j++)
                s->n[j] = s->n[j + 1];
            s->count--;
            return;
        }
}
static inline int32_t stack_top(const stack_t *s) { return s->count ? s->n[s->count - 1] : -1; }

/* ---------------------------------------------------------------- voices --- */
static struct { float ph, aenv, penv; } kick;
static struct { float ph1, ph2, e1, e2, en; } rim;
static struct { float env, k, hp; } hat;

enum { OWN_NONE, OWN_LIVE, OWN_LOOP };
static struct { float ph, freq, target, amp, gate; int32_t note; int owner; } bass;
static stack_t bass_held, skank_held;

static struct { float ph[4], inc[4]; int nv; float env, fenv, lo, band; } sk;

static struct { float ph, lfo, amp, lp; } siren;
static float crash_env, crash_ph;

static void kick_hit(void) { kick.ph = 0.0f; kick.aenv = 1.0f; kick.penv = 1.0f; }
static void rim_hit(void) { rim.e1 = rim.e2 = rim.en = 1.0f; rim.ph1 = rim.ph2 = 0.0f; }
static void hat_hit(int open) { hat.env = open ? 0.8f : 1.0f; hat.k = open ? k_hat_o : k_hat_c; }

static void bass_play(int32_t note, int owner)
{
    float f = mtof((float)note);
    if (bass.gate > 0.0f && note == bass.note)
        bass.amp *= 0.35f;               /* the same note again: a pluck, not a smear */
    if (bass.gate <= 0.0f || bass.amp < 0.05f)
        bass.freq = f;                   /* from silence: straight there; legato: a glide */
    bass.target = f;
    bass.note = note;
    bass.gate = 1.0f;
    bass.owner = owner;
    dubs.bass_note = note;
}
static void bass_release(void)
{
    bass.gate = 0.0f;
    bass.owner = OWN_NONE;
    dubs.bass_note = -1;
}

static void skank_hit(int32_t root)
{
    static const int8_t IV[CHORD_N][4] = {{0, 3, 7, 12}, {0, 4, 7, 12}, {0, 3, 7, 10}};
    const int8_t *iv = IV[(uint32_t)dubp.chord % CHORD_N];
    int i;
    sk.nv = 4;
    for (i = 0; i < 4; i++) {
        sk.inc[i] = mtof((float)(root + iv[i]) + (i & 1 ? 0.06f : -0.04f)) * INV_SR;
        sk.ph[i] = (float)i * 0.25f;
    }
    sk.env = 1.0f;
    sk.fenv = 1.0f;
}

/* ------------------------------------------------------------- sequencer --- */
static float seq_left;                   /* samples to the next step */
static int32_t step = -1;
static int32_t rec_note = -1;            /* the bass note being recorded, -1 none */
static uint8_t rec_pending, rec_released;   /* write the note at the next step (played late), then stop */

static float step_len(int32_t s, const pat_t *p)
{
    float base = SR * 60.0f / ((float)dubp.bpm * 4.0f);
    return base * (1.0f + ((s & 1) ? -p->swing : p->swing));
}

static void fire_step(int32_t s, const pat_t *p)
{
    int32_t ps = s & 15, live = stack_top(&bass_held);
    uint8_t v;
    if (p->kick[ps] == 'x')
        kick_hit();
    if (p->rim[ps] == 'x')
        rim_hit();
    if (p->hat[ps] != '.')
        hat_hit(p->hat[ps] == 'o');
    if ((p->skank >> ps) & 1u && stack_top(&skank_held) >= 0)
        skank_hit(stack_top(&skank_held));
    /* the loop: record what is held, then play what is there (a live note wins) */
    if (dubp.rec_on && rec_note >= 0) {
        dubs.loop[s] = rec_pending ? (uint8_t)rec_note : (uint8_t)DUB_TIE;
        rec_pending = 0;
        if (rec_released)
            rec_note = -1;
    }
    if (live >= 0)
        return;
    v = dubs.loop[s];
    if (v == 0) {
        if (bass.owner == OWN_LOOP)
            bass_release();
    } else if (v < DUB_TIE) {
        bass_play(v, OWN_LOOP);
    }                                    /* DUB_TIE: the note before it goes on (if the loop is playing it) */
}

static void events(const pat_t *p)
{
    while (ev_r != ev_w) {
        uint32_t i = ev_r % EVQ, t = ev_type[i];
        int32_t a = ev_arg[i];
        ev_r++;
        switch (t) {
        case EV_BASS_ON:
            stack_push(&bass_held, a);
            bass_play(a, OWN_LIVE);
            if (dubp.rec_on && dubs.playing && step >= 0) {   /* into the loop, on the nearest step */
                float frac = 1.0f - seq_left / step_len(step, p);
                rec_note = a;
                rec_released = 0;
                if (frac < 0.5f) {
                    dubs.loop[step] = (uint8_t)a;
                    rec_pending = 0;
                } else {
                    rec_pending = 1;
                }
            }
            break;
        case EV_BASS_OFF:
            stack_pop(&bass_held, a);
            if (a == rec_note) {
                if (rec_pending)
                    rec_released = 1;    /* written at the next step, then done */
                else
                    rec_note = -1;
            }
            if (stack_top(&bass_held) >= 0)
                bass_play(stack_top(&bass_held), OWN_LIVE);
            else if (bass.owner == OWN_LIVE)
                bass_release();
            break;
        case EV_SKANK_ON:
            stack_push(&skank_held, a);
            if (!dubs.playing)
                skank_hit(a);            /* stopped: a stab at once; playing: on the skank steps */
            break;
        case EV_SKANK_OFF:
            stack_pop(&skank_held, a);
            break;
        case EV_PLAY:
            if (!dubs.playing) {
                step = DUB_LOOP - 1;
                seq_left = 0.0f;         /* step 0 at the next sample */
                dubs.playing = 1;
            }
            break;
        case EV_STOP:
            dubs.playing = 0;
            step = -1;
            dubs.step = -1;
            if (bass.owner == OWN_LOOP)
                bass_release();
            rec_note = -1;
            break;
        case EV_CRASH:
            crash_env = 1.0f;
            crash_ph = 0.0f;
            break;
        case EV_CLEAR_LOOP: {
            int32_t k;
            for (k = 0; k < DUB_LOOP; k++)
                dubs.loop[k] = 0;
            if (bass.owner == OWN_LOOP)
                bass_release();
            break;
        }
        case EV_PANIC:
            bass_held.count = skank_held.count = 0;
            bass_release();
            sk.env = 0.0f;
            siren.amp = 0.0f;
            break;
        }
    }
}

/* ---------------------------------------------------------------- effects --- */
#define ECHO_MAX 55000u                  /* 1.25 s of tape */
static int16_t echo_buf[ECHO_MAX] DUB_POOL;
static uint32_t echo_w;
static float echo_d = 10000.0f, echo_lp, echo_hp, wow_ph;

/* the spring: dispersive allpasses (the chirp), then two damped loops (left, right) */
#define AP_N 6
static const uint16_t AP_LEN[AP_N] = {37, 53, 71, 97, 131, 173};
#define AP_TOTAL (37 + 53 + 71 + 97 + 131 + 173)
static float ap_buf[AP_TOTAL] DUB_POOL;
static uint16_t ap_i[AP_N];
#define CL_LEN 1601u
#define CR_LEN 1867u
static float cl_buf[CL_LEN] DUB_POOL, cr_buf[CR_LEN] DUB_POOL;
static uint32_t cl_i, cr_i;
static float cl_lp, cr_lp;

/* the DJ filter: a TPT state-variable filter, both channels */
static struct { float ic1[2], ic2[2], g, k, f_now; } djf;

static float mute_g[3] = {1.0f, 1.0f, 1.0f};   /* drums, bass, skank: glided, no clicks */
static float fb_now, spring_now;

void dub_init(void)
{
    k_kick_p = dm_decay(0.035f, SR);
    k_kick_a = dm_decay(0.30f, SR);
    k_rim1 = dm_decay(0.012f, SR);
    k_rim2 = dm_decay(0.028f, SR);
    k_rimn = dm_decay(0.004f, SR);
    k_hat_c = dm_decay(0.014f, SR);
    k_hat_o = dm_decay(0.11f, SR);
    k_sk_a = dm_decay(0.075f, SR);
    k_sk_f = dm_decay(0.025f, SR);
    k_crash = dm_decay(0.06f, SR);
    bass.note = -1;
    bass.freq = bass.target = 55.0f;
    dubs.step = -1;
    dubs.bass_note = -1;
    dubs.skank_note = -1;
    djf.k = 1.2f;                        /* resonance: a little peak at the cutoff */
}

static float siren_freq(float beats)     /* the siren's pitch, from the tempo's beat (a quarter) */
{
    float ph;
    switch (dubp.siren_mode) {
    case SIREN_WHOOP:                    /* a rise each beat */
        ph = beats - dm_floor(beats);
        return 260.0f * dm_exp2(2.2f * ph);
    case SIREN_ALARM:                    /* two tones, an eighth each */
        ph = beats * 2.0f;
        return ((int32_t)dm_floor(ph) & 1) ? 830.0f : 620.0f;
    case SIREN_LASER:                    /* a fall each eighth */
        ph = beats * 2.0f;
        ph -= dm_floor(ph);
        return 110.0f + 2600.0f * dm_exp2(-9.0f * ph);
    default: {                           /* WAIL: up and down over two beats */
        float tri;
        ph = beats * 0.5f;
        ph -= dm_floor(ph);
        tri = ph < 0.5f ? 2.0f * ph : 2.0f - 2.0f * ph;
        return 380.0f * dm_exp2(1.5f * tri);
    }
    }
}

void dub_render(float *lr, uint32_t n)
{
    static const float DIV16[ECHO_N] = {2.0f, 3.0f, 4.0f, 6.0f};
    const pat_t *p = &PAT[(uint32_t)dubp.pattern % PAT_N];
    float step16 = SR * 60.0f / ((float)dubp.bpm * 4.0f);
    float echo_target = dm_clamp(step16 * DIV16[(uint32_t)dubp.echo_div % ECHO_N], 200.0f, (float)ECHO_MAX - 4.0f);
    float beat_inc = (float)dubp.bpm / (60.0f * SR);
    float mt[3], thr = dubp.throw_on ? 1.0f : 0.0f, siren_gate = dubp.siren_on ? 1.0f : 0.0f, peak = 0.0f;
    float f_target = dm_clamp(dubp.filter, -1.0f, 1.0f), fb_t = dm_clamp(dubp.echo_fb, 0.0f, 1.1f);
    float sp_t = dm_clamp(dubp.spring, 0.0f, 1.0f);
    int filt_on;
    uint32_t i, a;

    events(p);
    mt[0] = dubp.mute_drums ? 0.0f : 1.0f;
    mt[1] = dubp.mute_bass ? 0.0f : 1.0f;
    mt[2] = dubp.mute_skank ? 0.0f : 1.0f;
    dubs.skank_note = stack_top(&skank_held);

    /* the DJ filter's coefficients, once a block (its knob glides block to block) */
    djf.f_now += (f_target - djf.f_now) * 0.25f;
    filt_on = dm_abs(djf.f_now) > 0.02f;
    if (filt_on) {
        float fc = djf.f_now < 0.0f ? 16000.0f * dm_exp2(djf.f_now * 7.6f)   /* low-pass down to ~80 Hz */
                                    : 30.0f * dm_exp2(djf.f_now * 8.5f);      /* high-pass up to ~11 kHz */
        djf.g = dm_tan(DM_PI * dm_clamp(fc, 20.0f, 16000.0f) * INV_SR);
    }

    for (i = 0; i < n; i++) {
        float kk, rr, hh, bb, ss, sr_, dry, echo_in, rev_in, echo_out, revl, revr, out[2];

        /* the clock */
        if (dubs.playing) {
            seq_left -= 1.0f;
            if (seq_left <= 0.0f) {
                step = (step + 1) % DUB_LOOP;
                dubs.step = step;
                fire_step(step, p);
                seq_left += step_len(step, p);
            }
        }
        siren.lfo += beat_inc;
        if (siren.lfo >= 64.0f)
            siren.lfo -= 64.0f;

        for (a = 0; a < 3; a++)
            mute_g[a] += (mt[a] - mute_g[a]) * 0.003f;

        /* kick: a sine falling from ~155 Hz to 45 Hz */
        kk = 0.0f;
        if (kick.aenv > 0.0002f) {       /* (each voice only while it sounds: the FM-1's cycles are few) */
            kick.penv *= k_kick_p;
            kick.aenv *= k_kick_a;
            kick.ph += (45.0f + 110.0f * kick.penv) * INV_SR;
            if (kick.ph >= 1.0f)
                kick.ph -= 1.0f;
            kk = dm_tanh(1.6f * dm_sin_turns(kick.ph) * kick.aenv);
        }

        /* rim: the cross-stick, two tones and a tick */
        rr = 0.0f;
        if (rim.e2 > 0.0002f) {
            rim.e1 *= k_rim1;
            rim.e2 *= k_rim2;
            rim.en *= k_rimn;
            rim.ph1 += 1750.0f * INV_SR;
            rim.ph2 += 560.0f * INV_SR;
            if (rim.ph1 >= 1.0f)
                rim.ph1 -= 1.0f;
            if (rim.ph2 >= 1.0f)
                rim.ph2 -= 1.0f;
            rr = 0.55f * dm_sin_turns(rim.ph1) * rim.e1 + 0.35f * dm_sin_turns(rim.ph2) * rim.e2
                 + 0.5f * noise() * rim.en;
        }

        /* hat: noise, high-passed */
        hh = 0.0f;
        if (hat.env > 0.0002f) {
            float nz = noise();
            hat.env *= hat.k;
            hh = (nz - hat.hp) * hat.env;
            hat.hp = nz;
        }

        /* bass: a sine and a little second harmonic, warmed */
        bass.freq += (bass.target - bass.freq) * 0.0012f;
        bass.amp += (bass.gate - bass.amp) * (bass.gate > bass.amp ? 0.006f : 0.0009f);
        bass.amp = dm_flush(bass.amp);
        bb = 0.0f;
        if (bass.amp > 0.0002f) {
            bass.ph += bass.freq * INV_SR;
            if (bass.ph >= 1.0f)
                bass.ph -= 1.0f;
            bb = dm_tanh(1.4f * (dm_sin_turns(bass.ph) + 0.2f * dm_sin_turns(2.0f * bass.ph))) * bass.amp;
        }

        /* skank: four saws into a swept band-pass, a short chop */
        ss = 0.0f;
        if (sk.env > 0.0005f) {
            float x = 0.0f, f;
            int v;
            for (v = 0; v < sk.nv; v++) {
                sk.ph[v] += sk.inc[v];
                if (sk.ph[v] >= 1.0f)
                    sk.ph[v] -= 1.0f;
                x += 2.0f * sk.ph[v] - 1.0f;
            }
            x = x * 0.25f + 0.3f * noise() * sk.fenv;
            f = 2.0f * DM_PI * (650.0f + 2600.0f * sk.fenv) * INV_SR;   /* Chamberlin: fine below 4 kHz */
            sk.lo += f * sk.band;
            sk.band += f * (x - sk.lo - 0.9f * sk.band);
            sk.band = dm_flush(sk.band);
            sk.lo = dm_flush(sk.lo);
            ss = sk.band * sk.env * 1.6f;
            sk.env *= k_sk_a;
            sk.fenv *= k_sk_f;
        }

        /* siren: a square swept by the beat, softened */
        siren.amp += (siren_gate - siren.amp) * (siren_gate > siren.amp ? 0.004f : 0.0002f);
        sr_ = 0.0f;
        if (siren.amp > 0.0005f) {
            siren.ph += siren_freq(siren.lfo) * INV_SR;
            if (siren.ph >= 1.0f)
                siren.ph -= 1.0f;
            siren.lp += ((siren.ph < 0.5f ? 1.0f : -1.0f) - siren.lp) * 0.3f;
            sr_ = siren.lp * siren.amp;
        }

        /* the desk: channels, mutes, sends */
        kk *= mute_g[0];
        rr *= mute_g[0];
        hh *= mute_g[0];
        bb *= mute_g[1];
        ss *= mute_g[2];
        dry = 0.62f * kk + 0.55f * bb + 0.42f * rr + 0.22f * hh + 0.45f * ss + 0.22f * sr_;
        echo_in = 0.10f * rr + 0.22f * ss + 0.55f * sr_ + thr * (0.6f * rr + 0.4f * hh + 0.8f * ss);

        /* the tape echo: its time glides when changed (the pitch bends, as tape does), its repeats darken */
        {
            float rp, fr, y, x;
            uint32_t i0, i1;
            echo_d += (echo_target - echo_d) * 0.00012f;
            wow_ph += 0.6f * INV_SR;
            if (wow_ph >= 1.0f)
                wow_ph -= 1.0f;
            rp = (float)echo_w - echo_d - 12.0f * (1.0f + dm_sin_turns(wow_ph));
            if (rp < 0.0f)
                rp += (float)ECHO_MAX;
            i0 = (uint32_t)rp;
            fr = rp - (float)i0;
            if (i0 >= ECHO_MAX)
                i0 -= ECHO_MAX;
            i1 = i0 + 1u == ECHO_MAX ? 0u : i0 + 1u;
            y = ((float)echo_buf[i0] * (1.0f - fr) + (float)echo_buf[i1] * fr) * (1.0f / 32767.0f);
            fb_now += (fb_t - fb_now) * 0.0005f;
            x = echo_in + y * fb_now;
            echo_lp += (x - echo_lp) * 0.28f;           /* ~2.3 kHz: each repeat darker */
            echo_hp += (echo_lp - echo_hp) * 0.012f;    /* ~85 Hz out: no mud */
            x = dm_tanh(echo_lp - echo_hp);
            echo_buf[echo_w] = (int16_t)(x * 32000.0f);
            if (++echo_w == ECHO_MAX)
                echo_w = 0;
            echo_out = y;
        }

        /* the spring: kicked by the crash, fed by the rim, the skank, the siren and the echo */
        rev_in = 0.45f * rr + 0.22f * ss + 0.18f * sr_ + 0.08f * hh + 0.25f * echo_out;
        if (crash_env > 0.0002f) {
            crash_env *= k_crash;
            crash_ph += 58.0f * INV_SR;
            if (crash_ph >= 1.0f)
                crash_ph -= 1.0f;
            rev_in += crash_env * (1.4f * noise() + 1.2f * dm_sin_turns(crash_ph));
        }
        {
            float x = rev_in;
            uint32_t off = 0, k;
            for (k = 0; k < AP_N; k++) {
                float *b = &ap_buf[off], d = b[ap_i[k]], y = d - 0.62f * x;
                b[ap_i[k]] = dm_flush(x + 0.62f * y);
                if (++ap_i[k] == AP_LEN[k])
                    ap_i[k] = 0;
                x = y;
                off += AP_LEN[k];
            }
            revl = cl_buf[cl_i];
            revr = cr_buf[cr_i];
            cl_lp = dm_flush(cl_lp + (revl - cl_lp) * 0.35f);
            cr_lp = dm_flush(cr_lp + (revr - cr_lp) * 0.35f);
            cl_buf[cl_i] = x + 0.8f * cl_lp - 0.1f * cr_lp;
            cr_buf[cr_i] = x + 0.8f * cr_lp - 0.1f * cl_lp;
            if (++cl_i == CL_LEN)
                cl_i = 0;
            if (++cr_i == CR_LEN)
                cr_i = 0;
        }
        spring_now += (sp_t - spring_now) * 0.0005f;

        out[0] = dry + 0.7f * echo_out + 0.9f * spring_now * revl;
        out[1] = dry + 0.7f * echo_out + 0.9f * spring_now * revr;

        /* the DJ filter: low-pass left of centre, high-pass right of it */
        if (filt_on) {
            float g = djf.g, k = djf.k, a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
            for (a = 0; a < 2; a++) {
                float v3 = out[a] - djf.ic2[a];
                float v1 = a1 * djf.ic1[a] + a2 * v3;
                float v2 = djf.ic2[a] + a2 * djf.ic1[a] + a3 * v3;
                djf.ic1[a] = dm_flush(2.0f * v1 - djf.ic1[a]);
                djf.ic2[a] = dm_flush(2.0f * v2 - djf.ic2[a]);
                out[a] = djf.f_now < 0.0f ? v2 : out[a] - k * v1 - v2;
            }
        }

        for (a = 0; a < 2; a++) {
            float o = dm_tanh(out[a]);
            float ab = dm_abs(o);
            if (ab > peak)
                peak = ab;
            lr[2 * i + a] = o;
        }
    }
    dubs.peak = peak;
}
