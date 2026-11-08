/* SPDX-License-Identifier: MPL-2.0 */
/* DUBWISE's small float math: no libm (the FM-1's toolchain has no C library headers), branch-light, good enough
 * for audio. Everything is static inline so the engine's -O2 unit keeps it in line. */
#pragma once
#include <stdint.h>

#define DM_PI 3.14159265358979f

typedef union { float f; uint32_t u; int32_t i; } dm_bits_t;

static inline float dm_abs(float x) { dm_bits_t b; b.f = x; b.u &= 0x7FFFFFFFu; return b.f; }
static inline float dm_clamp(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline float dm_floor(float x) { int32_t i = (int32_t)x; return (float)(i - (x < (float)i)); }

/* sin(2 pi ph), ph in turns (any value): a parabola, refined; ~0.1 % */
static inline float dm_sin_turns(float ph)
{
    float x = ph - dm_floor(ph + 0.5f);                  /* [-0.5, 0.5) */
    float y = 8.0f * x - 16.0f * x * dm_abs(x);
    return 0.225f * (y * dm_abs(y) - y) + y;
}

/* 2^x for x in about [-60, 60]: the integer part into the exponent, a cubic for the rest */
static inline float dm_exp2(float x)
{
    dm_bits_t b;
    float fl, f;
    x = dm_clamp(x, -60.0f, 60.0f);
    fl = dm_floor(x);
    f = x - fl;
    b.f = 1.0f + f * (0.69583f + f * (0.22606f + f * 0.07812f));
    b.i += (int32_t)fl << 23;
    return b.f;
}

/* a soft clip shaped like tanh (exact at 0, 1 at +-3) */
static inline float dm_tanh(float x)
{
    float x2;
    x = dm_clamp(x, -3.0f, 3.0f);
    x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

/* tan(x) for x in [0, ~1.2]: a Pade approximant (the filters' prewarp) */
static inline float dm_tan(float x)
{
    float x2 = x * x;
    return x * (15.0f - x2) / (15.0f - 6.0f * x2);
}

/* the per-sample factor of an exponential decay with time constant tau seconds */
static inline float dm_decay(float tau, float sr) { return dm_exp2(-1.442695f / (tau * sr)); }

/* tiny values to zero: no denormals in feedback paths */
static inline float dm_flush(float x) { return dm_abs(x) < 1e-15f ? 0.0f : x; }
