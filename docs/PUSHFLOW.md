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

| Tier | Model | Network size | Picture |
|---|---|---|---|
| Fast | RIFE 4.25 lite | half | rebuilt at full size from the full-size frames |
| Balanced | RIFE 4.25 lite | full | network output |
| Quality | RIFE 4.26 | full | network output |

### Precision

Every model ships in full precision (fp32, the reference) and, where it agrees on the CPU, in half precision
(fp16 convolutions; warps, resizes, sigmoid and blend kept in fp32). At start-up PushFlow runs both on a
deterministic test pattern **on your graphics card** and only keeps fp16 when it matches fp32 (38 dB or better);
otherwise it stays on fp32 and logs why. On Windows' software renderer (CI) fp16 does not match, so the
fallback is exercised on every build. `VEYRA_PUSHFLOW_PRECISION=fp32` (or `fp16`) forces one.

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
at 2X, 3X and 4X. Share it in a GitHub issue to help build the hardware table.

## Roadmap status

The engineering roadmap (17 November 2026) lists 31 items. Where PushFlow stands after 3.1.5:

| Item | Status |
|---|---|
| PF-001 measurement | Done in part: inference p50/p95/max ring in the log and live stats, PushFlowBench with GPU timestamps. Per-frame epoch/deadline tagging and ETW export: open |
| PF-002 model contract | Done in part: models.json (source, hashes, opset, padding, align corners, mask order, precision, CPU checks), pinned archives and packages. Wider test corpus (HDR ramps, odd sizes, cuts): open |
| PF-003 hardware gates | Open: needs RTX 30/40/50, RDNA2/3/4 and Arc results (PushFlowBench) |
| PF-004 split encoder / feature cache | Open: needs a split export of the network |
| PF-005 combined warps | The vs-mlrt export already warps both frames in one batched GridSample (found while exposing the grids) |
| PF-006 memory traffic | Done in part: per-pair packing (time plane only for later frames), preallocated bound buffers |
| PF-007 mixed precision | Done: fp16 convolutions with fp32 warps/blend, on-device self-check, fp32 fallback. Hardware reports: open |
| PF-008 buffer pool | Buffers are preallocated at start-up; multi-slot rings for concurrency: open |
| PF-009/010 TensorRT, CUDA Graphs | Open (optional NVIDIA plugin; not bundled) |
| PF-011 asynchronous inference | Open: inference still waits for its output (kept on purpose until a fence-based path is proven) |
| PF-012 full-resolution Fast tier | Done: grids + mask rebuild at full size |
| PF-013 multiple frames per pair | Done in part: the pair is packed once; the network still runs once per frame |
| PF-014 automatic selection | The optimiser tests PushFlow with the other methods; per-precision/tier autotune: open |
| PF-017 cut handling | Scene cuts and cadence breaks reset the pair: no generated frame across a cut |
| Others (presenter thread, latency policies, GPU-only quality probe, NLE, …) | Open |
