# DUBWISE

**A dub sound system for the M-VAVE FM-1.** A riddim plays, you play the bass and the skank on top, and then you
*dub it out*: throw hits into the tape echo, wail on the siren, kick the spring, sweep the filter, and drop
channels in and out like a mixing desk.

Built on [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0) and felucca-bsp, from Felucca by Leo
Kuroshita (Hügelton Instruments).

> **Status: first draft, not yet run on an FM-1.** The sound engine runs and passes its checks on a computer
> (`tests/dsp/render.c`); the app compiles against a stand-in of the platform. The device build, the host simulator
> script (`app/tests/basic.txt`) and the CPU load on the FM-1 are still to be tried.

## Play it

The keyboard is split:

| Keys | What they play |
| --- | --- |
| **F3 – E4** (lowest 12) | **Sub bass**, two octaves down (F1 – E2), mono, glides when played legato |
| **F4 – G5** (upper 15) | **Skank**: hold a key and its chord chops on the offbeats while the riddim runs (a single stab when stopped) |

| Button | Does |
| --- | --- |
| **PLAY** | Riddim on / off |
| **SEQ** | Next riddim: ONE DROP, ROCKERS, STEPPERS |
| **REC** | Record the bass into the 2-bar loop (lit while armed). **Hold 1 s**: clear the loop |
| **FX** *(hold)* | **Echo throw**: rim, hats and skank all into the tape echo while held |
| **ARP** *(hold)* | **Dub siren** |
| **GLO** | **Kick the spring**: the spring-reverb crash |
| **ENV / LFO / EDIT** | Drums / bass / skank in or out (lit = in): the dub drop |
| **SEL** | Skank chord: minor, major, minor 7 |
| **OCT− / OCT+** | Keyboard octave |
| **SAVE** | Keep the settings and the bass loop across power-off |

| Encoder | Sets |
| --- | --- |
| **SELECT** | Tempo, 60 – 180 BPM (76 to start) |
| **ALGO** | Siren: WAIL, WHOOP, ALARM, LASER (all locked to the tempo) |
| **PRESET** | Riddim |
| **K1** | Echo feedback, 0 – 110 %. Past 100 % it runs away, held in check by the tape saturation |
| **K2** | Echo time: 1/8, dotted 1/8, 1/4, dotted 1/4. Change it while the echo rings and the pitch bends like tape |
| **K3** | DJ filter: turn left for low-pass, right for high-pass. It snaps open as it crosses the middle |
| **K4** | Spring reverb level |

MIDI in (USB or TRS): notes below C3 play the bass, the rest play the skank. Start and Stop start and stop the riddim.

The white keys light up in turn with the 16 steps. The screen shows the riddim grid with its playhead, the bass loop,
the desk (which channels are in), and the four knobs.

### A dub in 60 seconds

1. **PLAY**. The one drop starts.
2. Tap **REC**, play a bassline on the low keys over two bars, then tap **REC** again. It loops.
3. Hold a key on the upper keys: the skank comes in on the offbeats.
4. Hold **FX** on a hit and let go. The repeats come back darker each time.
5. Press **ENV** to drop the drums out under the echo, then press it again to slam them back in.
6. Hold **ARP** for the siren, press **GLO** to crash the spring, and turn **K3** left for the underwater filter.

## The sound

- **Drums:** a deep sine kick, a cross-stick rim (two tones and a tick), and noise hats with swing on the one drop.
- **Bass:** a sine with a touch of second harmonic, warmed by soft saturation.
- **Skank:** four slightly detuned saws through a swept band-pass, a short "chk".
- **Siren:** a softened square swept by a tempo-locked LFO.
- **Tape echo:** 1.25 s of 16-bit tape. Each repeat is darker (low-pass) and leaner (high-pass) and saturates
  softly. It has a little wow, and its time glides when changed.
- **Spring reverb:** six dispersive allpasses (the chirp) into two damped feedback loops, left and right.
- **DJ filter:** a resonant state-variable filter over the whole mix.

The whole engine is `app/dub.c` (float, no libm: `app/dubmath.h`). `app/app.c` is the controls, the screen and the
settings.

## Build

Once, add fm1-base (this commit is the one DUBWISE was written against):

    git submodule add https://github.com/charlesvestal/fm1-base base
    git -C base checkout b0976f295ccaeaf77a8053a5a785a9cc21f5345f
    git submodule update --init --recursive

For the device you need the JieLi toolchain and the AC79 SDK (see fm1-base's README and `base/tools/get_toolchain.sh`)
and Python with Pillow:

    ./build.sh                                  # app/build/dubwise.fwsc
    python3 base/tools/fm1_install.py app/build/dubwise.fwsc
    ./build.sh --host                           # the simulator: app/build/host/dubwise_host
    ./build.sh --web                            # the browser emulator: app/build/emu/

To get back to another firmware, use the [FM-1 Firmware Hub](https://thefirmwarehub.com). If something goes wrong,
hold **OCT−** while switching on to reach the USB updater.

## Test

    tests/run_tests.sh

This runs:

- **engine**: a scripted session through the sound engine alone, checked (silence before PLAY, the loop records and
  plays back, the filter cuts, the tails die, no clipping or NaN), written to `build/dubwise-demo.wav`. Give it a
  listen.
- **app**: `app.c` compiles against `tests/stub/plat.h`.
- **host**: with fm1-base in `base/`, the whole firmware in fm1-base's simulator, running `app/tests/basic.txt`.

## Ideas for next

- Follow MIDI clock in, and send it out.
- A "bubble" organ on the off-16ths, and a melodica-ish lead for the upper keys.
- More riddims (nyabinghi, rub-a-dub), plus fills on the fourth bar.
- Per-channel echo sends on a shift layer, and a "kill" on the echo input.
- Velocity from the keys (fm1-base's `APP_TOUCH`).
- A release: fill in `install` and `screenshot` in `app/fm1.toml`, and add fm1-base's release workflow.

## Licence

MPL-2.0 (see `LICENSE`), like fm1-base.
