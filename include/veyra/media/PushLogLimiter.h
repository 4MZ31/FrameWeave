// SPDX-License-Identifier: GPL-3.0-or-later
// PushFPS: keeps a noisy library's messages useful in the log. A decoder in trouble can print the same line
// hundreds of times a second; the log should say what it was, how often, and still show anything new.
//   - the same line again is counted, not written; the count is written with the next different line or when
//     the second is over ("last message repeated N times");
//   - at most `perSecond` lines are written in any second; the rest are counted and the count is written once
//     the next second starts;
//   - a line is cut at `maxBytes`, and control characters (a decoder can print raw bytes) become '?'.
// Pure and clock-free: the caller passes the time in milliseconds.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace veyra::media {

class PushLogLimiter {
public:
    struct Line { int level; std::string text; };

    explicit PushLogLimiter(int perSecond = 20, std::size_t maxBytes = 1024) noexcept
        : perSecond_(perSecond < 1 ? 1 : perSecond), maxBytes_(maxBytes < 16 ? 16 : maxBytes) {}

    // Returns what to write now, in order (zero, one or a few lines).
    std::vector<Line> take(int level, std::string_view raw, std::int64_t nowMs) {
        std::vector<Line> out;
        roll(nowMs, out);
        std::string text = clean(raw);
        if (text.empty()) return out;
        if (text == last_ && level == lastLevel_) { ++repeats_; return out; }
        flushRepeats(out);
        last_ = text; lastLevel_ = level;
        if (written_ >= perSecond_) { ++dropped_; return out; }
        ++written_;
        out.push_back({level, std::move(text)});
        return out;
    }

    // Whatever is still counted, for the end of a session.
    std::vector<Line> finish(std::int64_t nowMs) {
        std::vector<Line> out;
        roll(nowMs + 1000000, out);
        flushRepeats(out);
        return out;
    }

private:
    void roll(std::int64_t nowMs, std::vector<Line>& out) {
        if (started_ && nowMs - windowStart_ < 1000) return;
        if (started_) {
            flushRepeats(out);
            if (dropped_) out.push_back({warnLevel_, std::to_string(dropped_) + " more messages in the last second were not written"});
        }
        started_ = true; windowStart_ = nowMs; written_ = 0; dropped_ = 0;
    }
    void flushRepeats(std::vector<Line>& out) {
        if (repeats_) out.push_back({lastLevel_, "last message repeated " + std::to_string(repeats_) + " times"});
        repeats_ = 0;
    }
    std::string clean(std::string_view raw) const {
        while (!raw.empty() && (raw.back() == '\n' || raw.back() == '\r' || raw.back() == ' ')) raw.remove_suffix(1);
        std::string s;
        s.reserve(raw.size() < maxBytes_ ? raw.size() : maxBytes_);
        for (const char c : raw) {
            if (s.size() >= maxBytes_) break;
            const auto u = static_cast<unsigned char>(c);
            s.push_back(u < 0x20 || u == 0x7f ? '?' : c);
        }
        return s;
    }

    int perSecond_;
    std::size_t maxBytes_;
    bool started_ = false;
    std::int64_t windowStart_ = 0;
    int written_ = 0;
    unsigned dropped_ = 0;
    unsigned repeats_ = 0;
    std::string last_;
    int lastLevel_ = 0;
    static constexpr int warnLevel_ = 24;  // FFmpeg's AV_LOG_WARNING; the caller maps levels to its own log
};

}  // namespace veyra::media
