// SPDX-License-Identifier: GPL-3.0-or-later
#include "veyra/remoteplay/PushRetryWait.h"
#include <cstdio>
using namespace veyra::remoteplay;
static int failures = 0, checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); } } while (0)
int main() {
    // A start that never came up keeps the short waits it always had.
    CHECK(retryWaitSeconds(RetryKind::Start, 1) == 1 && retryWaitSeconds(RetryKind::Start, 2) == 2 && retryWaitSeconds(RetryKind::Start, 3) == 4);
    // The console's own "still in use" keeps 10, 20, 40.
    CHECK(retryWaitSeconds(RetryKind::InUse, 1) == 10 && retryWaitSeconds(RetryKind::InUse, 2) == 20 && retryWaitSeconds(RetryKind::InUse, 3) == 40);
    // A stream that broke off waits the whole release window before its first retry, and longer after.
    CHECK(retryWaitSeconds(RetryKind::AfterLive, 1) == 20 && retryWaitSeconds(RetryKind::AfterLive, 2) == 30 && retryWaitSeconds(RetryKind::AfterLive, 3) == 40);
    // Never inside the window for any retry of a stream that was playing: three retries no longer end in seconds.
    unsigned total = 0;
    for (unsigned r = 1; r <= 3; ++r) { CHECK(retryWaitSeconds(RetryKind::AfterLive, r) >= 20); total += retryWaitSeconds(RetryKind::AfterLive, r); }
    CHECK(total == 90);
    CHECK(retryWaitSeconds(RetryKind::Start, 0) == 1 && retryWaitSeconds(RetryKind::Start, 99) == 4);
    CHECK(retryKind(true, true) == RetryKind::InUse && retryKind(true, false) == RetryKind::InUse);
    CHECK(retryKind(false, true) == RetryKind::AfterLive && retryKind(false, false) == RetryKind::Start);
    static_assert(retryWaitSeconds(RetryKind::AfterLive, 1) == 20);
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
