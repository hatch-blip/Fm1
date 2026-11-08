# DUBWISE

**A trippy dub sound system for the M-VAVE FM-1, made to play with a band.** It locks to your MIDI clock (or leads
with its own), plays a riddim with bass and skank on top, and then *dubs it out*:

- echo throws whose repeats spiral down or up in pitch
- a seasick warped tape that you can freeze into an endless loop
- a phaser swirling through the wet signal
- a siren and a spring crash
- a robot dub engineer that drops, throws and bends things on its own while your hands are on your other gear

Built on [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0) and felucca-bsp, from Felucca by Leo
Kuroshita (Hügelton Instruments).

> **Status: a draft that has not been run on an FM-1.** Checked on a computer so far:
>
> - the sound engine (`tests/dsp/render.c`)
> - the app's controls against a fake panel (`tests/stub/ui_test.c`)
>
> Not tried yet: the device build, fm1-base's simulator script (`app/tests/basic.txt`), and the CPU load on the FM-1.

## With the band

DUBWISE is meant to sit in a rig as a follower: a sequencer drives it, and you dub it. It is set up for an
**Arturia BeatStep Pro**'s defaults. Feed the BSP's MIDI out (directly or through a MIDI thru box) into the FM-1's
TRS MIDI IN with a TRS-to-DIN adapter, or use USB.

| From the sequencer | DUBWISE does |
| --- | --- |
| **Clock, Start, Stop, Continue** | Follows the tempo and transport (the header shows **EXT**). If the clock goes away, it carries on at its own tempo |
| **Channel 1** (BSP sequencer 1) | **Bass**: held notes, glides when legato, velocity sets the level |
| **Channel 2** (BSP sequencer 2) | **Skank stabs**: each note is a chord stab (the chord type from SEL), so the BSP plays the rhythm |
| **Channel 10** (BSP drums) | **Drums** by General MIDI note: 36 kick, 37 / 38 / 40 rim, 42 / 44 hat, 46 open hat, and **39 (clap) / 49 / 57 kick the spring**. Velocity counts |
| **CCs** on any of those channels | **The desk**: see below |

All other channels are ignored, so the rest of the gear on the thru chain keeps its own.

- **Timing:** incoming MIDI is stamped as it lands and played at that point in the audio block. Clock and notes keep
  their spacing to within about a sample, at a constant ~6 ms of latency.
- **Change the channels:** hold **SEL** and turn **SELECT** (bass), **ALGO** (skank) or **PRESET** (drums), from
  1 to 16 or OFF. The header shows them while SEL is held (e.g. `B1 S2 D10`). Set bass and skank to the *same*
  channel and that channel splits at C3: bass below, held skank chords above (chopped on the offbeats).
- **Using the BSP's drums instead of DUBWISE's?** Choose the **MIDI** riddim (SEQ or PRESET): no internal drums,
  the drum channel plays them all. Or keep a riddim and layer the BSP's drums on top.
- **Clock out** is off by default. **SEL + PLAY** turns it on (USB only, since the FM-1 has no MIDI out jack), for
  when DUBWISE should lead.

### The desk from a controller (CC)

Put the BSP in Control mode (or use any controller) on one of DUBWISE's channels:

| CC | Does | CC | Does (64 and up = on) |
| --- | --- | --- | --- |
| 12 | Echo feedback | 20 | **Throw** (held) |
| 13 | Echo time | 21 | **Freeze** |
| 14 | DJ filter (64 = open, down low-pass, up high-pass) | 22 | **Siren** (held) |
| 15 | Spring | 23 | Spring **crash** (each press) |
| 16 | Shift | 24 | Drums out |
| 17 | Warp | 25 | Bass out |
| 18 | Auto dub | 26 | Skank out |
| 19 | Space | | |

## Play it

The keyboard is split:

| Keys | What they play |
| --- | --- |
| **F3 – E4** (lowest 12) | **Sub bass**, two octaves down (F1 – E2), mono, glides when played legato |
| **F4 – G5** (upper 15) | **Skank**: hold a key and its chord chops on the offbeats while the riddim runs (a single stab when stopped) |

| Button | Does |
| --- | --- |
| **PLAY** | Riddim on / off |
| **SEQ** | Next riddim: ONE DROP, ROCKERS, STEPPERS, MIDI (no drums of its own) |
| **REC** | Record the bass into the 2-bar loop (lit while armed). **Hold 1 s**: clear the loop |
| **FX** *(hold)* | **Echo throw**: rim, hats and skank all into the tape echo while held |
| **ARP** *(hold)* | **Dub siren** |
| **GLO** | **Kick the spring**: the spring-reverb crash |
| **ENV / LFO / EDIT** | Drums / bass / skank in or out (lit = in): the dub drop |
| **SEL** *(tap)* | Skank chord: minor, major, minor 7 |
| **SEL** *(hold)* | The **trippy page**: the knobs below change, see the next table |
| **SEL + FX** | **Freeze** the tape: whatever is in the echo loops forever, nothing new goes in (lit FX) |
| **SEL + PLAY** | MIDI clock out on / off (off to start) |
| **SEL** + SELECT / ALGO / PRESET | MIDI channel of the bass / skank / drums |
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

Hold **SEL** for the trippy page (its labels turn gold):

| Encoder | Sets |
| --- | --- |
| **K1 SHIFT** | Each echo repeat transposed: −12, −7, −5, OFF, +5, +7, +12 semitones. Repeats spiral down into the mud or up into the stars, and keep going if frozen |
| **K2 WARP** | Tape wow, flutter and wandering delay time, from subtle to seasick. It also bends the skank and the siren a little |
| **K3 AUTO** | **Auto dub**: how hungry the robot engineer is. Off at 0; at 100 it makes a move every couple of beats. Its moves: THROW, DROP (a bar without drums), BASS OUT, SIREN, CRASH, JUMP (a new echo time, so the tape bends), DIP (a filter sweep down and back), SPIRAL (a burst of octave-shifted repeats). The screen names each move, and what it takes out shows red on the desk |
| **K4 SPACE** | A slow four-stage phaser over the echo and the spring, its sweep a quarter turn apart left and right |

The white keys light up in turn with the 16 steps. The screen shows the riddim grid with its playhead, the bass loop,
the desk (which channels are in), and the four knobs.

### A dub in 60 seconds

1. **PLAY**. The one drop starts.
2. Tap **REC**, play a bassline on the low keys over two bars, then tap **REC** again. It loops.
3. Hold a key on the upper keys: the skank comes in on the offbeats.
4. Hold **FX** on a hit and let go. The repeats come back darker each time.
5. Press **ENV** to drop the drums out under the echo, then press it again to slam them back in.
6. Hold **ARP** for the siren, press **GLO** to crash the spring, and turn **K3** left for the underwater filter.
7. Get weird: hold **SEL** and turn **K1** to −12, then throw a skank. The repeats tumble down in octaves. Press
   **SEL + FX** to freeze them, and add **WARP** and **SPACE**.
8. Go play your other instrument: turn up **AUTO** and let the robot dub it.

## The sound

- **Drums:** a deep sine kick, a cross-stick rim (two tones and a tick), and noise hats with swing on the one drop.
- **Bass:** a sine with a touch of second harmonic, warmed by soft saturation.
- **Skank:** four slightly detuned saws through a swept band-pass, a short "chk".
- **Siren:** a softened square swept by a tempo-locked LFO.
- **Tape echo:** 1.25 s of 16-bit tape. Each repeat is darker (low-pass) and leaner (high-pass) and saturates
  softly. Its time glides when changed. There is a pitch shifter in its feedback loop (two crossfaded heads
  sweeping a 36 ms window), so each pass is transposed again. Warp adds wow, flutter and drift. Freeze closes the
  loop.
- **Space:** first-order allpasses with feedback over the wet (echo and spring) only, so the dry riddim stays solid.
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
  plays back, the filter cuts, a frozen tape holds, auto dub makes its moves, clock out is 24 per beat, a 120 BPM
  clock is followed step for step, the tails die, no clipping or NaN), written to `build/dubwise-demo.wav`. Give it
  a listen.
- **app**: `app.c` compiles against `tests/stub/plat.h`.
- **ui**: `app.c` and the engine together against a fake panel: the SEL page, freeze vs. throw, clock out, following
  a MIDI clock with notes in, and the settings across a reboot.
- **host**: with fm1-base in `base/`, the whole firmware in fm1-base's simulator, running `app/tests/basic.txt`.

## Ideas for next

- A "bubble" organ on the off-16ths, and a melodica-ish lead for the upper keys.
- More riddims (nyabinghi, rub-a-dub), plus fills on the fourth bar.
- Per-channel echo sends on a shift layer, and a "kill" on the echo input.
- Velocity from the keys (fm1-base's `APP_TOUCH`).
- A release: fill in `install` and `screenshot` in `app/fm1.toml`, and add fm1-base's release workflow.

## Licence

MPL-2.0 (see `LICENSE`), like fm1-base.
