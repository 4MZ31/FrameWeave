// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#pragma once
// How long a translated subtitle line stays after its original went away (live translation).
//
// A caption line arrives, is translated and shown. The moment the original line ends, or the next one
// begins, the translation used to go with it: the quicker the translation, the less time was left to read
// it. Here the last translated line, with its original, is kept for a chosen time (0 to 5 s) after it
// went away, or until the next translation is there, whichever comes first.
//
// Pure: no Qt, no clock of its own. The string type is the caller's (QString in the program,
// std::u16string in the test); an empty string is S().
#include <algorithm>
#include <cstdint>

namespace veyra::ui {

inline constexpr int kCaptionHoldMaxMs = 5000;
inline constexpr int kCaptionHoldDefaultMs = 1500;
inline constexpr int kCaptionHoldStepMs = 250;

// The time as it may be kept: 0 (off: exactly as before there was a hold) to 5000, in steps of 250.
inline int cleanCaptionHoldMs(long long ms) {
    const long long c = std::clamp<long long>(ms, 0, kCaptionHoldMaxMs);
    return int((c + kCaptionHoldStepMs / 2) / kCaptionHoldStepMs * kCaptionHoldStepMs);
}

template <class S>
class CaptionHold {
public:
    void setHoldMs(long long ms) {
        holdMs_ = cleanCaptionHoldMs(ms);
        if (holdMs_ == 0) drop();
    }
    int holdMs() const { return holdMs_; }

    // The live pair as it is now. Call whenever caption or translation changed.
    void observe(const S& caption, const S& translated, int64_t nowMs) {
        if (!(translated == S())) {
            heldCaption_ = caption;
            heldTranslated_ = translated;
            live_ = true;
            until_ = 0;
            return;
        }
        if (live_) {   // the translation went away now
            live_ = false;
            until_ = holdMs_ > 0 ? nowMs + holdMs_ : 0;
        }
    }

    // Nothing is kept: a new source, a seek, a pause, captions closed, translation switched off.
    void drop() {
        heldCaption_ = S();
        heldTranslated_ = S();
        live_ = false;
        until_ = 0;
    }

    bool holding(int64_t nowMs) const {
        return !live_ && holdMs_ > 0 && until_ > 0 && nowMs < until_ && !(heldTranslated_ == S());
    }

    // What is shown: the live pair while it has a translation, the kept pair while it is kept,
    // otherwise the live caption alone (as before).
    S shownTranslated(const S& translated, int64_t nowMs) const {
        return translated == S() && holding(nowMs) ? heldTranslated_ : translated;
    }
    S shownCaption(const S& caption, const S& translated, int64_t nowMs) const {
        return translated == S() && holding(nowMs) ? heldCaption_ : caption;
    }

private:
    S heldCaption_{}, heldTranslated_{};
    int holdMs_ = kCaptionHoldDefaultMs;
    bool live_ = false;
    int64_t until_ = 0;
};

}  // namespace veyra::ui
