// SPDX-License-Identifier: GPL-3.0-or-later
#include "veyra/media/PushFfmpegLog.h"
#include "veyra/media/PushLogLimiter.h"
#include "veyra/Log.h"

#include <chrono>
#include <cstdarg>
#include <mutex>

extern "C" {
#include <libavutil/log.h>
}

namespace veyra::media {
namespace {

std::mutex& gate() { static std::mutex m; return m; }
PushLogLimiter& limiter() { static PushLogLimiter* l = new PushLogLimiter(20, 1024); return *l; }  // never destroyed: decoder threads may log at exit

void write(int level, const std::string& text) {
    if (level <= AV_LOG_ERROR) log::error("ffmpeg", text);
    else log::warn("ffmpeg", text);
}

void callback(void* context, int level, const char* format, va_list args) {
    if (level > AV_LOG_WARNING) return;  // info and debug stay out of the log
    char line[1024]{};
    int prefix = 1;
    va_list copy;
    va_copy(copy, args);
    av_log_format_line2(context, level, format, copy, line, sizeof(line), &prefix);
    va_end(copy);
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    std::vector<PushLogLimiter::Line> out;
    {
        std::lock_guard lock(gate());
        out = limiter().take(level, line, now);
    }
    for (const auto& l : out) write(l.level, l.text);
}

}  // namespace

void installPushFfmpegLog() {
    static std::once_flag once;
    std::call_once(once, [] { av_log_set_callback(callback); });
}

}  // namespace veyra::media
