// SPDX-License-Identifier: GPL-3.0-or-later
// PushFPS: FFmpeg's warnings and errors in PushFPS's own log. Without a callback FFmpeg prints to stderr,
// which a windowed program does not have, so a decoder failing on a PS5 stream left nothing in the log.
#pragma once

namespace veyra::media {

// Installs the callback once for this process (later calls do nothing). Only warnings and errors are written,
// through PushLogLimiter (repeats counted, at most 20 lines a second). A process that has set its own callback
// (the export worker) must not call this.
void installPushFfmpegLog();

}  // namespace veyra::media
