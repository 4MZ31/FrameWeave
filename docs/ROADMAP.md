# PushFPS roadmap

Where PushFPS is going, in the order things depend on each other. No dates: a step is done when it has been
measured on real hardware, not when the code is written. [How things are measured](BENCHMARKING.md).

## The rules

1. **Real frames first.** The game's own frames always get through; generated frames come second, effects
   third, recording and overlays last. Nothing may build a queue that grows.
2. **No claim above its evidence.** A result from the software renderer proves correctness, never speed. A
   card benchmark is not a whole session. PushFPS's software timing is not button to screen.
3. **Generated frames are smoothness, not response.** They never make the console read the controller sooner,
   and no setting is described as if it did.
4. **Every card keeps working.** The universal path (DirectML) stays. Anything faster for one maker's cards is
   an option on top, with an automatic fallback.
5. **One change, one measurement.** Speed work ships switched on only after it was faster at equal picture
   quality on at least two classes of card. What does not prove itself is taken out again.

## Done in 3.2.5

- Frame generation that is on but shows no frames is said, with its cause, instead of failing silently.
- A performance report to copy (card, driver, settings, real and generated frames, software delay, nothing
  personal), a hardware report template and the benchmarking guide.
- A passed PushFlow half-precision test holds for one card, driver and PushFPS version only.
- PS5 Remote Play: buttons no longer stay pressed after fast combinations. Xbox: damaged frames are rejected
  and recovered from, with optional software decoding. Elgato 4K X: HDR is detected automatically.
- Reveal comparison clips, and split and reveal clips at up to 4K.
- The first [tested graphics cards](../README.md#tested-graphics-cards).

## Done in 4.0

- What stands still in front of a moving picture keeps its real pixels in generated frames (the HUD), with
  every method PushFPS runs itself. An option, off unless switched on since 4.0.1: on for everyone in 4.0 it
  made camera pans soft and broke the edges of moving things up, because many moving pixels match by chance
  between two frames (measured on natural pictures: `scripts/studies/still_rule`). It stays a choice.
- 60 → 30 duplicate removal passes a repeated picture over when a game hitches or dips under 30 and takes
  the next new one, so no picture of the game is lost and none is used twice. 4.0.1, 4.1.0 and 4.1.1 ran
  without it; it is back since 4.1.2.
- Lip sync by hand, on top of automatic sync.
- A line between the two sides of the live comparison. The latency readout can be dragged.
- The record button asks first: horizontal or vertical, fill or fit, the size and the quality, with the
  bitrate shown before it starts.
- A new capture card starts with its own sound input chosen where that is beyond doubt.
- PS5 Remote Play: a frame arriving while the queue is read is no longer taken for too old; quick presses
  are not lost; recoveries are logged with their cause.
- AV1 files on the card's own decoder, Dolby Vision 8.4 base layers shown as HLG, a display that reports the
  format's limit as its peak no longer believed.
- The performance report says how the picture reaches the screen and how long the screen took after the
  hand-over.

Still open after 4.0: ghosting in the moving world beside a character (nothing measured so far improves it).

## Done in 4.1

- The frames a frame generation method is given, and what is done with the frames it makes, are the same code
  as in 3.2.10 again (compared line by line). A saved "on" for the still rule is switched off once.
- Check my setup: the running picture is read for a few seconds and what costs sharpness, smoothness or sound
  is listed, each with its one change and its price. Its thresholds are estimates.
- AI picture enhancement at 1080p, 1440p or the picture's own size; the quality of NVIDIA's frame generation
  as a row in quick setup; a finer filter when the picture is resized to the window, as a switch.
- Capture cards: a card is chosen before a webcam, a missing card gets help (ports, drivers, Windows' camera
  permission), and a card that only sends a compressed picture at the chosen rate is offered its uncompressed
  format at another rate.
- Recording: clip markers, a vertical copy beside a wide recording (a second encoder), parts by size, the
  console's name in comparison clips, the picture as a virtual camera, and local control for button boxes
  and scripts on the same PC.
- Reading the text on the picture with Windows' own recognition, on the PC, with an HDR frame brought into
  range first; translating it with a model that runs on the PC or an engine the user chose.
- PS5 Remote Play's sound no longer held seconds behind; a target above the screen's refresh rate put out at
  the refresh rate; the low latency switch as the low queue on cards without NVIDIA's; the method chosen on the
  home page.
- Dolby Vision profiles 5, 7, 8 and 10 as input (shown as HDR10 or SDR; this is not Dolby Vision output),
  live translated subtitles, network streams over RTP and UDP, live timeshift.
- The interface in sections, each setting once per page, six backgrounds and seven accents, a high contrast
  look, a larger latency readout.
- Linux and macOS: a session no longer starts with extra delay, and spare margin is given back in seconds.

Not built in 4.1, on purpose: a new direct path for processed frames into a streaming program (the plugin, the
virtual camera and a custom RTMP output cover it today), and cutting a recording into parts without a gap
between them (the recorder starts a new encoder between parts).

## Done in 4.2

- Comparison clips have a picture quality: standard, high (the default: a fixed quality instead of a fixed
  bitrate) and near lossless, for every layout. With clip size 4K a split or reveal clip is a 4K clip that
  is almost impossible to tell from the picture.
- A recording in parts is cut without a gap: one encoder runs through the whole recording, a part ends at a
  keyframe and the next starts with it, and a part by size ends before that size. Every part is a standard
  MP4 by itself.
- A quick tour for new users, from the capture card to the tools, offered once on a new install and always
  there on the home page.
- Korean, the fifteenth interface language.

## Done in 4.2.1

- Upscale to my screen: one switch in quick setup that enlarges a capture smaller than the screen to the
  screen's resolution, with an upscaler the card can run. Check my setup offers it.
- The before/after line moves in full screen, and its sides can be swapped.
- The colour strength of AI picture enhancement no longer breaks colours above 100%.
- Professional mode: the panel over the full-screen picture can be made wider, and the picture's settings
  stand together in the Quality tab.

## Done in 4.3

- A new design: navigation by name, a title bar and a status strip around the same pages; a home page with
  the sources, the picture and quick setup side by side; an index over quick setup that brings any part of
  it to the top; an *All* tab in Professional mode; a start-up animation of its own; edges that light up in
  the accent colour under the pointer.
- Legacy: the interface of 4.2, one click away behind the palette, with the same features and settings.
- The whole of quick setup on the home page and in Streaming mode in the new design (smooth frame pacing,
  low latency, connecting at start-up, full screen on connecting).
- AI picture enhancement can be switched on in Professional mode with no layer yet, upscale to my screen is
  in Professional mode too, and the update buttons in Settings stand side by side.

## Done in 4.3.1

- The new design's sides fold: the navigation to its icons, and the settings side of the home page and of
  Professional mode away, each with its own button in the title bar. The picture takes the room.
- Professional mode's *All* tab: each category folds by its title and stays folded.
- A capture card connected from the new home page plays there, with a switch for landing in Professional
  mode as before.
- The motion source choice stays on screen for every frame generation method and says when a method does
  not use it; a folded quick setup section takes no room however it is folded.
- Linux and macOS preview 0.1.1 (a contribution by ShyVortex): connecting at start works with a single
  capture card, the sound devices are remembered, and the interface always draws with its theme.

## Done in 4.4

- Crosshair Hold: the crosshair (lines, a dot, a ring) is found in the middle of the screen, kept out of what
  frame generation is given and laid back over every generated frame, so it does not flicker or double when
  the camera moves. A switch in quick setup's HUD section, with NVIDIA VFG, DLSS frame generation, FSR, AMD
  FRC and PushFlow; its row says when a crosshair is held.
- The installer finishes an update while another program has the virtual camera's file loaded, and a copy
  that fails names the files.

## Next: can be built now, ships after it is measured

These need card test results and performance reports from real cards before they are switched on by default.

| Area | Step | What it should bring |
|---|---|---|
| PushFlow | Encode every real frame once: split the network into an encoder and a motion part, and reuse a frame's features for the pair before it, the pair after it and every in-between time | Less work per generated frame, most at 3× and 4× |
| PushFlow | Take redundant warps and full-size temporary pictures out of the network | Less time and memory per frame |
| PushFlow | Half precision checked per card with worst-case pictures, not only an average | Faster where it is safe, never a broken frame |
| PushFlow | A fixed pool of buffers leased per frame, released only when the last reader is done | No allocation while playing, safer teardown |
| Correctness | The end-to-end test PushFlow has (real frames through the real graph) for every other method | A method can never again be selected and silently do nothing |
| Diagnostics | One frame number from capture to screen, with a per-frame log to export | Counters that add up; exact answers to "where did that frame go" |
| Capture cards | Show the mode asked for, the mode the card agreed to and the rate that arrives; exact 59.94, 60 and 50; no silent fallback | Fewer wrong-mode sessions |
| Cadence | Duplicate detection from the picture itself with confidence and hysteresis | Right more often on noisy cards and over Wi-Fi |
| Quality test | Works on hardware-decoded sources; adds structural similarity and the worst frame | A score for every source, and no hidden bad frame |
| Sound | A small clocked buffer with drift correction instead of skips | No clicks, steady lip sync |

The larger plan for PushFlow (finding wrong places in a generated frame, reusing a pair's motion, a separate
timeline for recording) and what was measured for it so far: [PushFlow 2](PUSHFLOW2.md).

## Needs hardware, or an agreement outside the code

| Step | What it waits for |
|---|---|
| An optional NVIDIA-native PushFlow path with a per-card engine cache | NVIDIA's licence terms for shipping the runtime; tests on RTX 30, 40 and 50 |
| Latency modes (lowest delay, balanced, smoothest) with their measured cost shown | Camera measurements on real screens |
| Smoothing real frame-rate dips, as an opt-in with its extra delay shown | Real games that dip; blind comparisons |
| Processed frames to a streaming program straight from the graphics card (the virtual camera is built since 4.1 and waits for the same sessions) | Real sessions at 60 and 120 FPS on several cards |
| Windows code signing | A certificate (application in progress) |

## Later

- Frame input API version 2: NV12 and P010, HDR metadata, per-frame "new / repeated / damaged" flags,
  optional compressed input, sound, and shared graphics card surfaces.
- Linux and macOS beyond the preview: tested capture cards, recording and going live.

## Not planned

- 4K neural inference by default. PushFlow works out motion at up to 1080p and rebuilds the picture at full
  size; that is what keeps 4K playable.
- Anything that predicts frames the game has not sent yet as the normal mode.

## How to help

Run **Test my graphics card**, play a session, copy the **performance report** (both in *Settings*) and open
a [hardware report](https://github.com/DosukaSOL/PushFPS/issues/new?template=hardware-report.yml). Every
step in the first table above is waiting for exactly that.
