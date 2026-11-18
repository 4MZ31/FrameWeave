# PushFPS Frame Input API (version 1)

Send video from your own application straight into PushFPS. PushFPS treats it like a capture card:
frame generation (PushFlow, NVIDIA, AMD, Intel, ANR), AI enhancement, HUD protection, recording, instant
replay and going live all work on it. Typical senders: emulators, game engines, streaming clients,
video players, test tools.

- **Windows 10/11 only.** The transport is plain named shared memory plus an event, so any language
  that can open a file mapping can send (C, C++, C#, Rust, Python...).
- **C/C++:** include [`include/pushfps/FrameInput.h`](../include/pushfps/FrameInput.h). It has the
  structures and a complete sender class, `pushfps::FrameInputSender`.
- **Python:** [`tools/frame-input/send_test_pattern.py`](../tools/frame-input/send_test_pattern.py) is a
  working sender with no extra packages.
- **Licence:** the header and the samples are MIT, so closed-source applications can use them too.
  PushFPS itself stays GPL-3.0.

## In PushFPS

Click **Other app** on the home screen, in streaming mode, or in video playback mode. The dialog lists
every application sending on the usual channels (`default`, `1`-`8`); click **Use**. To use another
channel name, type it under **Other channel**. Opened this way, the picture behaves like any other
source: Professional mode, the quick setup, recording and streaming all apply.

Since 3.2.2 PushFPS can also be started on a channel: `PushFPS.exe frameinput:default` (or
`frameinput:<channel>`) opens it as soon as the window is up, so a sending program can launch PushFPS
on its own picture.

## Quick start (C++)

```cpp
#include "pushfps/FrameInput.h"

pushfps::FrameInputSender sender;
sender.open(L"default", 1920, 1080, 60, 1, "My emulator");   // largest frame, nominal rate, name
// for every frame you render:
sender.send(pixels, width, height, rowPitchBytes, PUSHFPS_FORMAT_BGRA8);
```

`send()` copies the frame, so your buffer is free again when it returns. Send at your real frame rate
(30 or 60 FPS for a game, for example); PushFPS generates the in-between frames.

## The protocol

The **sender** creates both objects, and PushFPS only reads:

| Object | Name |
|---|---|
| File mapping | `Local\PushFPS.FrameInput.<channel>`, `pushfps_frame_input_mapping_size(maxWidth, maxHeight)` bytes |
| Auto-reset event (optional) | `Local\PushFPS.FrameInput.<channel>.Ready` |

Channel names are 1-32 characters from `A-Z a-z 0-9 - _`. The mapping holds a 4096-byte header,
then three pixel slots of `slotBytes` each.

### Header (little-endian, byte offsets)

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | `magic` = `0x4E494650` ("PFIN"). Write it **last** when creating the channel and set it to 0 when closing |
| 4 | u32 | `version` = 1 |
| 8 | u32 | `headerBytes` = 4096 |
| 12 | u32 | `slotCount` = 3 |
| 16 | u32 | `maxWidth` (at most 8192) |
| 20 | u32 | `maxHeight` (at most 8192) |
| 24 | u64 | `slotBytes`, at least maxWidth × maxHeight × 4 |
| 32 | u32 | `fpsNum` (0 = variable) |
| 36 | u32 | `fpsDen` |
| 40 | u32 | `senderPid`: PushFPS notices when this process exits |
| 44 | u32 | `colorSpace`: 0 = sRGB (SDR, BT.709 primaries) |
| 48 | u32 | `latestSlot`: index of the newest complete slot |
| 56 | u64 | `frameCounter`: increments by one for every frame sent |
| 64 | char[64] | `senderName`, UTF-8, NUL-terminated |
| 3904 | slot[3] | slot descriptors, 64 bytes each |

### Slot descriptor (64 bytes)

| Offset | Type | Field |
|---|---|---|
| 0 | u32 | `seq`: odd while writing, even when complete |
| 4 | u32 | `width` |
| 8 | u32 | `height` |
| 12 | u32 | `stride`: bytes per row in the slot (≥ width × 4) |
| 16 | u32 | `format`: 1 = BGRA8, 2 = RGBA8 (alpha ignored) |
| 20 | u32 | `flags`: bit 0 = discontinuity (a cut, seek or restart before this frame) |
| 24 | i64 | `timestamp100ns`: when the picture was made, `QueryPerformanceCounter` in 100 ns units (0 = use the arrival time) |
| 32 | u64 | `frameIndex`: your own counter (shown in diagnostics) |

Slot *i*'s pixels start at `4096 + i × slotBytes`, top row first.

### Sending a frame

1. Take `i = (latestSlot + 1) % 3`.
2. Increment slot *i*'s `seq` (it becomes odd).
3. Write the pixels and the slot's fields.
4. Increment `seq` again (even).
5. Store `latestSlot = i`, increment `frameCounter` and signal the event.

Use interlocked writes or memory barriers between steps (the C++ sender does). PushFPS copies the
newest slot and checks `seq` before and after copying; a frame rewritten mid-copy is read again, so a
half-written picture is never shown. Only the newest frame counts: if you send faster than PushFPS
reads, the older frames are skipped, as with a capture card.

## Limits and notes

- 8 bits per channel, SDR (sRGB). HDR and 10-bit formats are reserved for a later version.
- Every frame is copied once into PushFPS. At 4K60 that's about 2 GB/s of memory bandwidth, which is
  fine on current PCs. A zero-copy GPU texture path may come in a later version.
- The size can change from frame to frame, up to the maximum you opened with; PushFPS rebuilds its
  pipeline when it does.
- Timestamps drive frame pacing. Use `QueryPerformanceCounter` (the C++ sender does when you pass 0).
- When the sending process exits, PushFPS shows "the sending app was closed" and stops the source.

## Version history

- **1** (PushFPS 3.1.3): shared-memory BGRA8/RGBA8 frames.
