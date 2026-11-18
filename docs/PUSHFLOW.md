# PushFlow

PushFlow is PushFPS's own frame generation. It runs **RIFE** (Real-Time Intermediate Flow Estimation, an open
neural network by hzwer, MIT licence) through **ONNX Runtime on DirectML**, inside PushFPS's own Direct3D 12
graph, so it works on NVIDIA, AMD and Intel graphics cards.

## How it works

1. **Pack.** A compute shader writes the two real frames and the time of the new frame into the network's
   input buffer on the GPU. A pair is packed once; at 3X and 4X the later frames only change the time plane.
2. **Network.** ONNX Runtime runs the RIFE model on DirectML, on PushFPS's own GPU queue, reading and writing
   buffers PushFPS allocated once (no CPU copies of pictures).
3. **Rebuild.** The models also output RIFE's final **warp grids** and **blend mask**
   (`scripts/pushflow/rife_graph.py` adds them to the vs-mlrt export). In the Fast tier the network runs at half
   size and `shaders/PushFlowReconstruct.hlsl` enlarges the *motion*, not the picture: every output pixel is
   sampled from the **full-resolution real frames** where the motion points. At full size the network's own
   picture is used (the rebuild reproduces it exactly: 99 dB on the CPU check).
4. **Protect.** HUD Lock and the HUD boxes are composed on top, as for every method; scene cuts reset the pair
   so PushFlow never blends across a cut.

| Tier | Model | Motion estimated at | Picture |
|---|---|---|---|
| Fast | RIFE 4.25 lite | up to 960×540 (at most half size) | rebuilt at full size from the full-size frames |
| Balanced | RIFE 4.25 lite | up to 1280×720 | rebuilt at full size when smaller, else network output |
| Quality | RIFE 4.26 | up to 1920×1080 | rebuilt at full size when smaller, else network output |

Since 3.1.6 no tier runs the network above 1080p: at 4K every tier ran RIFE at 4K (or 1080p for Fast) and
was too slow for live play, so the admission control skipped every pair and PushFlow stayed at the game's
frame rate (field report, 17 November 2026). A 4K picture now uses 960×540 / 1280×720 / 1920×1080 motion and
the full-resolution rebuild.

### Precision

Every model ships in full precision (fp32, the reference) and, where it agrees on the CPU, in half precision
(fp16 convolutions; warps, resizes, sigmoid and blend kept in fp32). Until 3.1.6 PushFlow compared both on
every start, inside PushFPS. Since 3.1.7 fp32 is the default: on Windows' software renderer the fp16 model
removed the D3D12 device, and a crash report from an RTX 5090 ended inside ONNX Runtime right after the check.

Since 3.2.0 half precision is tested **in a separate process**: *Professional → PushFlow half precision →
Test* runs `PushFlowBench.exe --fp16-probe` on the graphics card PushFPS renders on (Balanced, 1280×720). It
times fp32, then the fp16 model with PushFlow's own comparison, and passes only when fp16 matches (38 dB or
better) and is at least 10% faster. If the driver or DirectML faults, that process ends and PushFPS keeps
running. A passed test turns the switch on (preference `pushFlowHalf`, result in `pushFlowHalfProbe`); PushFlow
then still compares both at start and stays on fp32 if they do not match. `VEYRA_PUSHFLOW_PRECISION=fp16`
forces the half model for developers.

## Why PushFlow could stay at the game's frame rate

PushFlow never refuses to run: the frame budget (`FgRecoveryBudget`) skips every pair whose generated frames
would arrive too late, so a PushFlow that is too slow, or only looks too slow, shows as "stuck at 30 FPS"
with no error. 3.2.0 removed every cause found in the code:

| Cause | Fix |
|---|---|
| Every inference waited on the CPU: Run synchronised the providers, then `SynchronizeBoundOutputs`. The engine thread stood still for the whole network once per generated frame, and the GPU queue ran dry between the pack, network and resolve lists; those gaps counted as frame generation cost | Queued inference: Run returns once DirectML has recorded the network onto PushFPS's own queue (`disable_synchronize_execution_providers`); the resolve list follows on the same queue. A full wait every 120 inferences lets DirectML release finished work. `VEYRA_PUSHFLOW_SYNC=1` restores the waits |
| Dynamic input shapes: DirectML cannot fuse or pre-plan a graph whose size is unknown | Free dimensions fixed per session (`AddFreeDimensionOverrideByName`, names written into the models by `prepare_runtime.py`). `VEYRA_PUSHFLOW_DYNAMIC=1` keeps them dynamic |
| DirectML prepares the network on its first runs; the first live inference carried that time into the budget | Two warm-up runs at start, while the graph is built |
| A skipped pair marked PushFlow's history lost: the next pair generated nothing, and the evaluation after it was a lone probe on cold clocks that measured slow and started the next skip | PushFlow keeps its previous frame across skips (it reads the graph's own real frames) and its first evaluations after a skip are settling samples, as for NVIDIA VFG (`FgCostSettling`) |
| A tier the card really cannot sustain | The too-slow watch: after eight seconds in which the budget skipped nearly every pair, PushFlow moves one tier down and says why; on Fast it says what else would help |
| The network ran at 4K for 4K pictures | 3.1.6: motion estimated at most at 1080p, picture rebuilt at full size |
| ONNX Runtime's spinning CPU thread pool | One intra-op thread, no spinning |

The network's time per frame is now its GPU span on the queue (timestamps after the pack and before the
resolve), shown live in the quick setup and in the log every 30 seconds.

## What was measured, and where

On the CPU, when the release is built (`scripts/pushflow/prepare_runtime.py`, numbers from the 2026-11-17 run):

| Check | Fast (4.25 lite) | Quality (4.26) |
|---|---|---|
| Rebuild from grids + mask vs the model's own picture | 99 dB (exact) | 99 dB (exact) |
| Moving square at t = 0.5 (vs 20 dB for a blend) | 38.8 dB | 55.5 dB |
| Fine texture, network at half size: old enlarged picture | 17.2 dB | 17.3 dB |
| Fine texture, network at half size: full-size rebuild | **25.0 dB** | **30.7 dB** |
| fp16 copy vs fp32 (CPU) | 73 dB | 75 dB |

On WARP (Windows' software renderer, `tests/integration/PushGpuTests.cpp`): the whole path through DirectML,
identical frames, the moving square, t = 0.25, fine texture in motion and the Fast tier's rebuild.

One finding from WARP: on per-pixel noise panning 8 pixels at 640×360, the Fast tier's rebuild scores 22.1 dB
against 15.2 dB for a blend, but the Quality model run at full size does not track that pattern (14.6 dB). Real
pictures are not per-pixel noise, and the moving-square check passes at 51.6 dB, but it is on the list: tests
with real game and video footage, and whether the Quality tier should also estimate motion at a lower scale.

**Not measured yet:** speed on real NVIDIA, AMD and Intel cards. PushFlowBench (below) exists so these
numbers come from real hardware, not estimates.

## Measure your card

`PushFlowBench.exe` is next to `PushFPS.exe`. Run it from a command prompt in the PushFPS folder:

```
PushFlowBench.exe --sizes 1920x1080,2560x1440 --tiers 0,1,2 --pairs 40
```

It writes `pushflow-bench.json`: per size and tier, the precision your card passed, GPU time of the pack, the
network and the rebuild (D3D12 timestamps), CPU time of the inference call, and the wall time of a whole pair
at 2X, 3X and 4X. Share it in a GitHub issue to help build the hardware table. `--compare-shapes` adds a run
with dynamic shapes and `--compare-sync` one that waits after every inference (the behaviour before 3.2.0).

## Roadmap status

The engineering roadmap (17 November 2026) lists 31 items. Where PushFlow stands after 3.2.0:

| Item | Status |
|---|---|
| PF-001 measurement | Done in part: inference p50/p95/max ring in the log and live stats, PushFlowBench with GPU timestamps. Per-frame epoch/deadline tagging and ETW export: open |
| PF-002 model contract | Done in part: models.json (source, hashes, opset, padding, align corners, mask order, precision, CPU checks), pinned archives and packages. Wider test corpus (HDR ramps, odd sizes, cuts): open |
| PF-003 hardware gates | Open: needs RTX 30/40/50, RDNA2/3/4 and Arc results (PushFlowBench) |
| PF-004 split encoder / feature cache | Open: needs a split export of the network |
| PF-005 combined warps | The vs-mlrt export already warps both frames in one batched GridSample (found while exposing the grids) |
| PF-006 memory traffic | Done in part: per-pair packing (time plane only for later frames), preallocated bound buffers |
| PF-007 mixed precision | Done: fp16 convolutions with fp32 warps/blend; tested per card in a separate process since 3.2.0, fp32 fallback. Hardware reports: open |
| PF-008 buffer pool | Buffers are preallocated at start-up; multi-slot rings for concurrency: open |
| PF-009/010 TensorRT, CUDA Graphs | Open (optional NVIDIA plugin; not bundled) |
| PF-011 asynchronous inference | Done (3.2.0): queued on the graph's own queue, ordered by the queue, a full wait every 120 inferences |
| PF-012 full-resolution Fast tier | Done: grids + mask rebuild at full size |
| PF-013 multiple frames per pair | Done in part: the pair is packed once; the network still runs once per frame |
| PF-014 automatic selection | The optimiser tests PushFlow with the other methods; the too-slow watch lowers the tier live (3.2.0); half precision per card through the isolated probe (3.2.0) |
| PF-017 cut handling | Scene cuts and cadence breaks reset the pair: no generated frame across a cut |
| Others (presenter thread, latency policies, GPU-only quality probe, NLE, …) | Open |
