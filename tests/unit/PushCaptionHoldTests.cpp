// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// How long a translated subtitle line stays (include/veyra/ui/PushCaptionHold.h).
#include "veyra/ui/PushCaptionHold.h"

#include <cstdio>
#include <string>

using veyra::ui::CaptionHold;
using S = std::u16string;

static int failures = 0;
static void check(bool ok, const char* what) {
    if (!ok) { ++failures; std::printf("FAIL: %s\n", what); }
}

int main() {
    using namespace veyra::ui;
    // The time that may be kept.
    check(cleanCaptionHoldMs(-5) == 0 && cleanCaptionHoldMs(0) == 0, "nothing below 0");
    check(cleanCaptionHoldMs(99999) == 5000, "nothing above 5 s");
    check(cleanCaptionHoldMs(1500) == 1500 && cleanCaptionHoldMs(1600) == 1500 && cleanCaptionHoldMs(1630) == 1750, "steps of 250");
    check(CaptionHold<S>().holdMs() == 1500, "1.5 s unless chosen");

    const S none, a = u"the bridge is closed", ta = u"de brug is dicht", b = u"take the next exit", tb = u"neem de volgende afslag";

    {   // With no hold everything is exactly what the live pair is.
        CaptionHold<S> h; h.setHoldMs(0);
        h.observe(a, none, 0);  check(h.shownCaption(a, none, 0) == a && h.shownTranslated(none, 0) == none, "untranslated line shows its original");
        h.observe(a, ta, 100);  check(h.shownTranslated(ta, 100) == ta, "translated line shown");
        h.observe(none, none, 200);
        check(h.shownTranslated(none, 200) == none && h.shownCaption(none, none, 200) == none, "hold 0: gone with the original");
        check(!h.holding(200), "hold 0 never holds");
    }
    {   // The original ends: the pair stays for the chosen time and then goes.
        CaptionHold<S> h; h.setHoldMs(1500);
        h.observe(a, none, 0); h.observe(a, ta, 100);
        h.observe(none, none, 1000);
        check(h.holding(1000) && h.holding(2499), "held for 1.5 s");
        check(h.shownTranslated(none, 2000) == ta && h.shownCaption(none, none, 2000) == a, "the pair that was shown stays");
        check(!h.holding(2500) && h.shownTranslated(none, 2500) == none && h.shownCaption(none, none, 2500) == none, "gone after the time");
        h.observe(none, none, 2600);
        check(!h.holding(2600), "an empty observation later starts no new hold");
    }
    {   // The next line begins before the time is over: the old pair stays until the new translation is there.
        CaptionHold<S> h; h.setHoldMs(1500);
        h.observe(a, ta, 0);
        h.observe(b, none, 500);   // new original, translation cleared
        check(h.shownCaption(b, none, 600) == a && h.shownTranslated(none, 600) == ta, "old pair shown, not the new original over the old translation");
        h.observe(b, tb, 900);
        check(h.shownCaption(b, tb, 900) == b && h.shownTranslated(tb, 900) == tb && !h.holding(900), "the new pair replaces it at once");
        // and a slow translation: after the time the new original stands alone, as before
        CaptionHold<S> g; g.setHoldMs(1000);
        g.observe(a, ta, 0); g.observe(b, none, 100);
        check(g.shownCaption(b, none, 1099) == a, "still held");
        check(g.shownCaption(b, none, 1100) == b && g.shownTranslated(none, 1100) == none, "then the new original alone");
    }
    {   // A source change, a seek, a pause: nothing stays.
        CaptionHold<S> h; h.setHoldMs(5000);
        h.observe(a, ta, 0);
        h.drop(); h.observe(none, none, 10);
        check(!h.holding(10) && h.shownTranslated(none, 10) == none, "dropped");
        h.observe(a, ta, 20); h.observe(none, none, 30); check(h.holding(40), "holds again afterwards");
        h.setHoldMs(0); check(!h.holding(40) && h.shownTranslated(none, 40) == none, "switching the hold off drops what is held");
    }
    {   // A line that grows keeps its translation live all along.
        CaptionHold<S> h;
        h.observe(a, ta, 0); h.observe(a + u" today", ta, 50);
        check(!h.holding(50) && h.shownTranslated(ta, 50) == ta && h.shownCaption(a + u" today", ta, 50) == a + u" today", "live pair passes through");
    }
    std::printf(failures ? "push caption hold tests: %d FAILED\n" : "push caption hold tests: all passed\n", failures);
    return failures ? 1 : 0;
}
