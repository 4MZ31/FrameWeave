# PushFlow 2: the plan, and where it stands

On 19 November 2026 a reviewer sent an engineering guide for the next generation of PushFlow: six parts, each
with its own tests and gates, and one rule above all of them: nothing that exists today may break. This file
is how PushFPS takes that guide: what each part is, what has been done, what was measured, and what waits
for something that code alone cannot give. It follows the [roadmap](ROADMAP.md)'s rules and adds none that
contradict them. No dates.

## The six parts

| Part | What it is | Where it stands |
|---|---|---|
| Guard | Find the places in a generated frame that are probably wrong and show something safe there instead | A first rule was built, measured on the shipped models and taken out again (below). The next rule is written down; it needs real clips before it is built |
| Fusion | Encode every real frame once and work a pair's motion out once, then make every in-between time from it | Not started. It is the roadmap's "encode every real frame once"; it ships only when it is faster at equal quality on two classes of card |
| Predict | Make frames from what has already arrived, without waiting for the next real frame | Not started, and never the normal mode. It needs Guard's checking of predictions against the frames that arrive later, and a camera for any claim about delay |
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
3. **The reach is in the network's pixels.** Here it lies between 96 and 160 of them. A tier that works the
   motion out at a larger size sees finer motion but, measured in screen widths, a shorter distance. Whether
   that holds on real camera turns, and whether fast pairs should be worked out at a smaller size, is the
   next thing to measure: with the in-app frame quality test on real games, not with drawn squares.

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

## Next for Guard: written down, not built

The measurement points at a different signal: **the two real frames differ at a place, and the network found
no motion there that explains it.** That is what both a vanished object and a cut look like, and what a
followed motion does not. Showing the real frame nearer in time at such a place would make a too-fast object
step at the game's rate instead of blinking out.

It is not built, because the same signal also describes things that are fine (a flash, a new object, the
strip a moving object uncovers), and telling those apart cannot be tuned on drawn squares. It needs:

- **clips**: short recordings of real games at 60 FPS (fast turns, small fast objects, HUDs, cuts, fades).
  Every second frame is withheld, generated, and compared with the frame that was really there, which is what
  the in-app frame quality test already does for one session;
- **eyes**: the same clips with and without the rule, unlabelled, judged by people. A held region scores
  worse than a blend by the usual error measure even when it looks better, so numbers alone cannot decide.

## What each blocked step waits for

| Waits for | Steps |
|---|---|
| Real 60 FPS game clips, and people to judge them | Guard's next rule; any change to how the tiers choose the motion size |
| Card tests and performance reports from real cards (*Settings → Test my graphics card*, *Performance report*) | Fusion (the split model must be faster at equal quality on two classes of card); half precision per card |
| A camera filming screen and controller (240 FPS or more) | Every statement about delay: Predict, Response, latency modes |
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
| Guard's risk map and safe fallback | First rule built, measured, taken out; next rule written down (above) |
