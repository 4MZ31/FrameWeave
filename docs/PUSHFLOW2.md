# PushFlow 2: the plan, and where it stands

On 19 November 2026 a reviewer sent an engineering guide for the next generation of PushFlow: six parts, each
with its own tests and gates, and one rule above all of them: nothing that exists today may break. This file
is how PushFPS takes that guide: what each part is, what has been done, what was measured, and what waits
for something that code alone cannot give. It follows the [roadmap](ROADMAP.md)'s rules and adds none that
contradict them. No dates.

## The six parts

| Part | What it is | Where it stands |
|---|---|---|
| Guard | Find the places in a generated frame that are probably wrong and show something safe there instead | Two rules were measured on the shipped models and neither held (below): one was built and taken out, one never built. What a user has from it since 3.2.7 is the measurement itself: the frame quality test says its **worst frame** and its **worst place**. The next rule is written down; it needs real clips before it is built |
| Fusion | Encode every real frame once and work a pair's motion out once, then make every in-between time from it | **Its first step is built, as an experiment that starts off** (*Professional → PushFlow 2: encode each real frame once*): the model cut into its per-frame encoder and the rest, each real frame encoded once (below). The second step, a pair's motion worked out once, does not exist in these models: the time enters at the first motion block |
| Predict | Make frames from what has already arrived, without waiting for the next real frame | Measured (below): a frame half a step ahead can be made from what the network already gives, with no extra run; on drawn pictures it beats holding the last frame for slow motion and is no better for fast motion. Not built, and never the normal mode. It needs its own way onto the screen, Guard's checking of predictions against the frames that arrive later, and a camera for any claim about delay |
| Dual | A second, deliberately delayed timeline for recording and streaming, beside the one the player sees | Not started. It needs real recording and OBS sessions on real cards to show the player's side is not slowed |
| Response | Turn the picture a little from the stick's movement before the console's own frame arrives | Research only. Whether it can work from a video signal at all is unproven; it needs a PS5, a high-speed camera and people to judge it |
| Memory | Bring back detail that earlier real frames showed | Research only. It depends on motion that has been checked (Guard, Fusion) |

## The rules these parts are built under

1. **Off means unchanged.** A part that is switched off leaves the picture, the timing and the saved settings
   exactly as they were. That is tested, not assumed.
2. **Measured before believed.** A rule built on the network's own tensors is run on the shipped models first
   (`scripts/pushflow/prepare_runtime.py`, `motion_report`, a few minutes in the `pushflow-models` workflow).
   The first rule failed exactly there, before any user saw it.
3. **Blocked is a status.** A step that cannot be tested here (no card, no camera, no clips, no licence) stays
   off and is listed below with what it waits for. It is not shipped "experimental" to find out.
4. **No switch without something behind it.** Settings, flags and files are added with the feature that needs
   them, not ahead of it.
5. **Every guard stays plugged in.** `tests/protected-features.json` names what must keep working and the test
   that guards each part; `scripts/checks/protected_features.py` fails the `checks` workflow when such a test
   is deleted, no longer run, or loses the check that matters.
6. **A held frame is not a generated frame.** Whatever a part falls back to is counted as what it is.

## Done

- **The motion contract is code.** What PushFlow's models give besides their picture (two sampling grids and a
  blend mask), what those are *not* (not optical flow between the real frames; not a confidence), and how
  positions convert between the motion field and the picture: `include/veyra/pushflow/PushFlowMotion.h`, with
  its own unit test (`veyra_push_flow_motion_tests`). Until now this lived only in a shader and a Python check.
- **The rebuild is checked against it.** The shader that makes the full-size picture
  (`shaders/PushFlowReconstruct.hlsl`) is compared pixel by pixel with the contract's reference, from the
  network's own grids and mask, on five kinds of pair (`testPushFlow`, software renderer). Before, it was only
  judged by how close its picture came to a drawn truth.
- **A motion report for every model build**: how the grids and mask behave when the network can and cannot
  follow a motion. It decides nothing; it is the measurement the next rule starts from.
- **The protected-feature list**, checked on every push (rule 5).
- **The frame quality test says its worst frame and its worst place** (3.2.7). The test already withheld
  every second real frame, generated it and compared; it reported averages. An object left out of a generated
  frame costs the whole picture a fraction of a decibel and one place nearly everything, so the test now also
  gives the least accurate frame, and the least accurate of 144 tiles averaged over the frames
  (`worstTilePsnr`, `include/veyra/engine/PushQuality.h`). In the unit test a missing object leaves the
  picture at 29.6 dB and its place at 8.1 dB. The score is what it was.
- **PushFlow's motion size can be chosen apart from its tier** (3.2.7, *Professional → PushFlow motion
  size*): automatic (each tier's own, as before), or at most 540, 720 or 1080 lines whatever the tier's
  model. It is a choice and not a new automatic rule, because of what the measurement below says: the models
  do not behave the same at every size, and not in one direction. The row shows the size really in use; the
  too-slow watch puts a size that is too much for the card back to automatic before it lowers a tier, and
  every step it can take is cheaper than the one before (unit test). Without a choice, every tier's size is
  bit for bit what it was (unit test), and the graph test runs PushFlow both ways.
- **Experiments have switches, and every one starts off** (`include/veyra/pushflow/PushFlow2.h`, preference
  `pushFlow2`). A part is not in the build, not available here, off, on and waiting, or running, and the row
  in Professional mode says which: asked for is not the same as in use. The preference is read strictly:
  damaged text, text of another kind and a file cut off mid-save all read as everything off, and a newer
  version's names are skipped. `PushFPS.exe --safe-mode` keeps every experiment off for one run and changes
  nothing saved. If PushFPS ever stops inside PushFlow, the next start switches them all off.
- **The encoder cache** (Fusion's first step; the owner's decision of 19 November 2026 to build every part
  without waiting for hardware results, each behind a switch). `scripts/pushflow/rife_graph.py`
  `split_encoder` cuts each model into two small encoders (one for the input's first frame, one for its
  second) and the rest; nothing is retrained, the same nodes and weights. `prepare_runtime.py` ships them
  only when together they give the whole model's output on the CPU: every output, seven times between
  the frames, two sizes, features kept from the pair before and made anew. Measured: the largest
  difference is 0.0 for both models (run 44629175911). In PushFlow (`PushFlowBackend.cpp`, `Split`) the
  second frame is encoded when its pair arrives and its features stay for the pair after it, where it is
  the first frame; another time between the same two frames runs the rest alone. So a steady 2× costs one
  encoder run per pair instead of two, and 3× and 4× one instead of four and six. It is used only after it
  gave the whole model's picture on the user's own graphics card at start, for a pair with nothing kept
  and for the pair after it. The whole model stays loaded: a split run that fails hands the same frame to
  it, and the row says so. Frames are told apart by the graph's own count; a frame whose number is
  unknown, skipped or outdated is encoded again. Not available with half precision yet. **Unverified on
  real hardware, and the saving is unmeasured there**: the ceiling in arithmetic is 7 to 24 % (below); the
  start-up line in the log and the performance report (`pushFlowEncoderCacheStartMs`) give one frame each
  way on the user's card.
- **Three studies of the shipped models** run in the `pushflow-models` workflow (`--study`): what the
  tensors do under motion (`motion_report`), where the network's work goes (`fusion_report`), and whether a
  frame ahead can be made from it (`predict_report`). They print numbers and decide nothing.

## Measured: what the models do when motion is too fast

`pushflow-models` run 44595965906 (19 November 2026), on the CPU, a 512 × 320 picture at the network's own
size, the middle time. A 40-pixel square moves across a still background:

| The square moved | Fast model | Quality model | What the in-between frame shows |
|---|---|---|---|
| 16 px (3 % of the width) | 47.6 dB | 62.5 dB | the square, half-way |
| 48 px (9 %) | 37.6 dB | 40.8 dB | the square, half-way |
| 96 px (19 %) | 36.1 dB | 35.5 dB | the square, half-way, less exactly |
| 160 px (31 %) | 29.0 dB | 29.0 dB | **no square at all** |
| 256 px (50 %) | 29.1 dB | 29.1 dB | **no square at all** |

What the tensors say in the last two rows: at the square's old place the mask takes the current frame, at its
new place the previous one; each shows background there. Nowhere does the mask hesitate, and where the mask
does keep both samples (12 to 68 % of these pictures) the two samples agree. Three things follow.

1. **A failure is a missing object, not a see-through double.** An object that moves further than the network
   can follow is left out of the generated frame. At 2× that is a blink at the game's own rate.
2. **The mask says nothing about it.** It is as decided where the network is wrong as where it is right.
3. **A smaller motion size reaches further, sometimes.** The same pairs in a 512 × 384 picture, with the
   network at full, half and a quarter of the picture's size and the picture rebuilt at full size (run
   44600838736):

   | The square moved | Model | Full size | Half | Quarter |
   |---|---|---|---|---|
   | 96 px | Quality | 35.6 dB | 38.7 dB | 37.7 dB |
   | 160 px | Quality | 29.8 dB, left out | 32.7 dB, back | 33.7 dB, back |
   | 160 px | Fast | 29.8 dB, left out | 29.3 dB, left out | 31.7 dB, back |
   | 256 px | both | left out | left out | left out |

   So the reach is not a fixed share of the screen, and the largest motion size is not the best one for
   fast motion: on these pairs the Quality model did better at half size than at full size. It is not
   a law. A quarter-size network still loses the square at 256 px, the Fast model gains only at a
   quarter, and a fine texture sliding sideways (run 44605491581) goes every way at once:

   | The texture moved | Model | Full size | Half | Quarter |
   |---|---|---|---|---|
   | 8 px | Quality | 22.9 dB | 30.7 dB | 27.2 dB |
   | 16 px | Quality | 31.7 dB | 23.1 dB | 14.7 dB |
   | 32 px | Quality | 25.2 dB | 33.9 dB | 20.5 dB |
   | 64 px | Quality | 20.4 dB | 23.6 dB | 15.0 dB |
   | 8 px | Fast | 22.2 dB | 27.8 dB | 14.9 dB |
   | 16 px | Fast | 14.4 dB | 14.6 dB | 22.3 dB |
   | 32 px | Fast | 19.9 dB | 19.2 dB | 14.9 dB |

   and a slow square is always best at full size (16 px, Quality: 63.0, 50.9, 48.3 dB). No rule that
   picks the size from the motion follows from this, so none was built. What was built is the choice
   (3.2.7, above) and the number to judge it by on a real game: change the motion size, run the frame
   quality test, compare the worst place.

A fade (the whole picture 0.3 brighter, nothing moving) is the opposite case: the mask keeps both samples
almost everywhere and they differ everywhere, because a blend is the right picture there.

These are drawn pictures. They show what the tensors do; they do not rank the tiers and say nothing about
how a game looks.

## Built, measured, taken out: the ghost guard

The first Guard rule assumed that a failure looks like a mixture: two samples that differ, and a mask that
cannot choose. Where that happened, the pixel was given to one sample, the real frame nearer in time. It was
written whole (shader, reference, tests, a switch in Professional mode), and its report on the shipped models
(run 44595135167) said:

- on every square pair, followed or not, it changed 0.00 to 0.03 % of the picture: there was nothing for it
  to act on, because a failure is not a mixture (above);
- on the fade it changed 91 to 98 % of the picture at its two stronger levels and turned a correct blend into
  a held frame (27 to 29 dB down to 16 to 17 dB).

It acted where the network was right and not where it was wrong. It did not prove itself, so it is not in the
product (roadmap rule 5). The contract, the reference and the stricter rebuild check that were written for it
are what stayed.

## Measured and not built: holding a whole frame

A second idea was a rule for the whole frame instead of for a place: when the two samples disagree over
much of the picture, the pair has failed, so show a real frame instead. Its number (the mean difference
between the two samples, run 44605491581) does not separate failure from success:

| Pair | Difference between the samples | The generated frame |
|---|---|---|
| a square, followed | 0.0004 to 0.0023 | 36 to 62 dB |
| a square, left out | 0.007 to 0.009 | 29 dB |
| a fade of 0.07, generated correctly | 0.062 to 0.076 | 38 to 39 dB |
| a fine texture, followed (8 px) | 0.058 (Fast), 0.092 (Quality) | 22 dB, 18 dB |
| a fine texture, lost (64 px and more) | 0.135 to 0.149 | 14 to 15 dB |
| a fade of 0.3, generated correctly | 0.30 to 0.32 | as good as a blend can be |
| two unrelated pictures (a cut) | 0.18 to 0.19 | no right answer |

A left-out square scores ten times lower than a correct fade, and a correct strong fade higher than a cut.
And where the network has lost a texture, its picture (14 to 15 dB) is as close to the truth as a blend of
the two frames (15.2 dB) and closer than a held frame (13.9 dB): holding would not have been better by the
only measure there is here. No threshold on this number does more good than harm, so the rule was not built.

## Measured: what Fusion could save

`fusion_report` follows every tensor of the model from its input (previous frame, current frame, time) and
counts the multiply-adds of the convolutions (run 44608578177):

| | Lite model (Fast, Balanced) | Full model (Quality) |
|---|---|---|
| Work that does not depend on the time | 28.4 % | 13.6 % |
| Work that depends on the time (done for every generated frame, whatever is reused) | 71.6 % | 86.4 % |

The part that does not depend on the time is the feature encoder, which takes the two frames as a batch of
two. That it treats them separately was measured and not assumed: with one frame replaced, the other
frame's half of every such tensor stayed identical (difference 0.0), and so did all of it at another time.
So each real frame's features could be made once and used for the pair before, the pair after and every
in-between time. The most that saves, in convolution work:

| | 2× | 3× | 4× |
|---|---|---|---|
| Lite model | 14.2 % | 21.3 % | 23.6 % |
| Full model | 6.8 % | 10.2 % | 11.4 % |

This is a ceiling in arithmetic, not in milliseconds: the samplings and resizes between the convolutions are
not counted, and on a graphics card the cost of a second session and of handing the features across is not
either. Nothing else in the network can be reused between in-between times: the time enters at the first
motion block, before any motion is worked out. The guide's "work a pair's motion out once" does not exist in
these models. So Fusion here means one thing, the encoder split, and its ceiling is 7 to 24 %. It is not
built: closing PushFlow's one model session safely already needed its own fix after a crash in the field
(3.2.2), and a second session for a gain that may be a few per cent in practice has to be measured on real
cards first.

## Measured: a frame ahead, from what the network already gives

Predict needs motion that points forward in time. Two ways to get it from the shipped models were tried
(`predict_report`, runs 44605491581 and 44606025232; a 320 × 192 picture, the frame after the newest one
forecast from the two before it and compared with the real one):

- **Asking the network for time 1.** It gives back the newest frame (64 to 80 dB), but its sampling grid is
  not the motion between the two frames: for a square moving 8 to 48 px to the right it reads −5.7 to
  −20.6 px. A forecast built on it is no better than holding the newest frame. A dead end.
- **From the half-way grids.** The grids of the ordinary in-between frame say where each of its points lies
  in the newest frame; carried one more half-step along the same line, they give a frame half an interval
  ahead with no extra run of the network:

  | The pair | Hold the newest frame | Forecast, Fast model | Forecast, Quality model |
  |---|---|---|---|
  | a square, 8 px a frame | 27.9 dB | 44.7 dB | 51.7 dB |
  | a square, 16 px | 24.8 dB | 35.4 dB | 48.3 dB |
  | a square, 32 px | 30.1 dB | 31.3 dB | 30.3 dB |
  | a square, 48 px | 25.4 dB | 27.7 dB | 27.1 dB |
  | a fine texture, 4 px | 13.9 dB | 19.7 dB | 36.6 dB |
  | a fine texture, 8 px | 13.9 dB | 21.2 dB | 17.7 dB |
  | a fine texture, 16 px | 13.9 dB | 15.6 dB | 27.6 dB |

So a forecast is possible, cheaply, for slow and steady motion, and falls back to about a held frame when the
motion is fast, which is exactly when a wrong forecast would show most. That is a proof that the tensors
allow it, on drawn pictures moving in straight lines. It is not a feature: a game's motion changes between
frames, a forecast frame has to be checked against the real frame that follows (Guard), it needs its own way
onto the screen and its own pacing, and whether it lowers the delay a player feels can only be shown with a
camera. Not built.

## Guard's rule, found by measuring: changed, and shown nowhere

Three more rules were run on the shipped models before any shader was written (`guard_report`, runs
44631423182, 44632325354 and 44633268004). The first two failed in ways worth keeping:

- **"The frames differ here and the network found no motion near."** It never touched a correct picture,
  and it never acted either: where the network loses an object it still invents a few pixels of motion
  around it (5 to 17 px), and that counts as motion near.
- **"Something one frame shows is used nowhere in the generated frame", counted by the mask's weights.**
  It restored the left-out square, and it replaced 61 to 97 % of a slowly sliding texture with the earlier
  frame (34 dB down to 16 dB). The network often takes one frame only; the other frame is then "unused"
  everywhere, and nothing is wrong.

What holds on every drawn pair: a place in a real frame counts as **shown** by a generated pixel that reads
it when the pixel takes it, *or when the pixel's other sample is the same picture*. A tile of a frame is
unused when less than a quarter of it is shown anywhere. Risky = the tile changed between the two frames
(its own change of brightness taken out, so a fade is not a change), it is unused in one of them, and it is
not as near the picture's edge as the picture's motion reaches (a pan pushes those out, unused and right).

| | Fast model | Quality model |
|---|---|---|
| Correct pictures touched (squares moved 2 to 96 px, fades, a still picture, a square over a texture) | 0 of 9 | 0 of 10 |
| A square left out at 160 and 256 px: how far the old place is from the earlier frame (0.32 = background, 0 = the square) | 0.32 → 0.14 | 0.32 → 0.07 |
| A square moved 200 px over a texture | 0.36 → 0.12 | 0.28 → 0.17 |
| A fine texture the network had already lost (14 to 26 dB): how much is replaced | 4 to 48 % | 0 to 55 % |
| Two unrelated pictures | 33 % | 70 % |

So the rule acts where an object was left out and leaves correct pictures alone, on drawn pictures. What it
does where the network is already lost (hold part of the picture) can look blocky, which is why a frame
with much of it risky is held whole. Whether any of this looks better in a game is not known: a held object
steps at the game's own rate, which the usual error measure scores *worse* than a missing one. It is being
built as an experiment that starts off, for people with real games and cards to judge.

## Next for Guard: written down, not built

The measurement points at a different signal: **the two real frames differ at a place, and the network found
no motion there that explains it.** That is what both a vanished object and a cut look like, and what a
followed motion does not. Showing the real frame nearer in time at such a place would make a too-fast object
step at the game's rate instead of blinking out.

It is not built, because at one pixel the same signal also describes things that are fine: a flash, a new
object, and above all the strip of background a moving object uncovers behind it, where the network is right
to take one frame only. What separates a vanished object from such a strip is not at that pixel: the
object's own pixels are shown somewhere else in the generated frame when the motion was followed, and
nowhere when it was not. So the rule needs a second pass that records which parts of each real frame the
generated frame uses at all, and that pass and its thresholds cannot be tuned on drawn squares. It needs:

- **clips**: short recordings of real games at 60 FPS (fast turns, small fast objects, HUDs, cuts, fades).
  Every second frame is withheld, generated, and compared with the frame that was really there, which is what
  the in-app frame quality test already does for one session;
- **eyes**: the same clips with and without the rule, unlabelled, judged by people. A held region scores
  worse than a blend by the usual error measure even when it looks better, so numbers alone cannot decide.

## What each blocked step waits for

| Waits for | Steps |
|---|---|
| Real 60 FPS game clips, and people to judge them | Guard's next rule; any automatic choice of the motion size (the choice by hand exists since 3.2.7) |
| Frame quality test results from real games at each motion size (the worst place, with the performance report, which names the motion size) | Whether a tier's own motion size should change |
| Card tests and performance reports from real cards (*Settings → Test my graphics card*, *Performance report*) | Fusion (the encoder split must be faster at equal quality on two classes of card, for a ceiling of 7 to 24 %); half precision per card |
| A camera filming screen and controller (240 FPS or more) | Every statement about delay: Predict (its forecast exists as a study), Response, latency modes |
| A PS5 with Remote Play, and testers | Response |
| Real recording, streaming and OBS sessions on several cards | Dual |
| Licence terms for runtimes and for any new model's weights | An NVIDIA-native path; any learned refinement |

## How the guide's first steps map to this repository

| The guide's step | Here |
|---|---|
| Freeze the baseline and list the protected features | `tests/protected-features.json`, checked on every push; the baseline is the 3.2.6 release and its CI run |
| A settings namespace with hidden, default-off flags | Not built: nothing exists yet for a flag to switch (rule 4) |
| Frame identity, clocks and reset rules | Already in the engine (`FramePacket.h`, `FrameBatch.h`, the reset coordinator). Nothing was added; new metadata comes with the part that reads it |
| Leases, queues and teardown | Unchanged. The roadmap's "fixed pool of buffers" step covers it, and it ships after it is measured |
| Instruments and a test corpus | The motion report (models), `PushFlowBench` and the card test (speed), the frame quality test (a live session). The corpus is the clips above |
| Read-only motion data with tested coordinates | `PushFlowMotion.h`, `PushFlowBackend::motionBuffer` (tests only), `veyra_push_flow_motion_tests` |
| Guard's risk map and safe fallback | First rule built, measured, taken out; a whole-frame rule measured and not built; next rule written down (above). The frame quality test's worst frame and worst place are the instrument a user has today |
| Encode once, reuse | Measured: what it could save, and that the encoder's work is per frame (above). Not built |
| Frames from the past only | Measured: possible from the half-way grids for slow motion (above). Not built |
