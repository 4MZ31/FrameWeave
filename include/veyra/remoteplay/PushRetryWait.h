// SPDX-License-Identifier: GPL-3.0-or-later
// PushFPS: how long a PS5 Remote Play reconnect waits before its next try.
//
// The PS5 keeps a session's slot for about twenty seconds after the session ends. A retry that comes sooner is
// refused (quit reason 4, error 2004, or RP_IN_USE), and with the short waits of a failed start all three
// retries of an episode were spent inside that window: the stream "connected and then stopped" for good.
//   Start   a connection that never came up: the console holds nothing yet, so 1, 2, 4 s.
//   InUse   the console said it still holds the previous session: 10, 20, 40 s.
//   AfterLive a stream that was playing and broke off (decode failure, stall, dropped network): the console is
//           still tearing that session down, so the first wait is the full window and later ones longer still,
//           20, 30, 40 s, in case a slow console needs more than the usual window.
#pragma once

#include <algorithm>

namespace veyra::remoteplay {

enum class RetryKind { Start, InUse, AfterLive };

// retry: the episode's retry number, 1 for the first. Anything below 1 counts as 1, above 3 as 3.
constexpr unsigned retryWaitSeconds(RetryKind kind, unsigned retry) noexcept {
    const unsigned n = std::clamp(retry, 1u, 3u);
    switch (kind) {
    case RetryKind::Start: return 1u << (n - 1);
    case RetryKind::InUse: return 10u << (n - 1);
    case RetryKind::AfterLive: return 10u + 10u * n;
    }
    return 1u;
}

// What the retry is: a console that says it holds the session wins over everything; otherwise a stream that
// had been playing waits for the console to let it go.
constexpr RetryKind retryKind(bool consoleHoldsSession, bool wasPlaying) noexcept {
    return consoleHoldsSession ? RetryKind::InUse : wasPlaying ? RetryKind::AfterLive : RetryKind::Start;
}

}  // namespace veyra::remoteplay
