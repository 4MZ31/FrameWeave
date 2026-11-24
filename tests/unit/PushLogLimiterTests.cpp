// SPDX-License-Identifier: GPL-3.0-or-later
#include "veyra/media/PushLogLimiter.h"
#include <cstdio>
using veyra::media::PushLogLimiter;
static int failures = 0, checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); } } while (0)
int main() {
    {   // The same line 500 times in a second is one line and one count.
        PushLogLimiter l;
        std::size_t written = 0;
        for (int i = 0; i < 500; ++i) written += l.take(16, "error while decoding MB 3 4\n", i).size();
        CHECK(written == 1);
        auto next = l.take(16, "something else", 600);
        CHECK(next.size() == 2 && next[0].text == "last message repeated 499 times" && next[1].text == "something else");
    }
    {   // Many different lines: at most 20 a second, the rest counted when the second is over.
        PushLogLimiter l(20);
        std::size_t written = 0;
        for (int i = 0; i < 100; ++i) written += l.take(24, "line " + std::to_string(i), 10).size();
        CHECK(written == 20);
        auto later = l.take(24, "after", 1500);
        CHECK(later.size() == 2 && later[0].text == "80 more messages in the last second were not written" && later[1].text == "after");
    }
    {   // Cut at the limit, control characters replaced, trailing newline gone, empty dropped.
        PushLogLimiter l(20, 16);
        auto a = l.take(16, std::string("ab\x01\x1b" "cdefghijklmnopqrstuvwxyz\n"), 0);
        CHECK(a.size() == 1 && a[0].text == "ab??cdefghijklmn");
        CHECK(l.take(16, "\n", 1).empty());
    }
    {   // A count still waiting is written at the end.
        PushLogLimiter l;
        l.take(16, "x", 0); l.take(16, "x", 1); l.take(16, "x", 2);
        auto end = l.finish(3);
        CHECK(end.size() == 1 && end[0].text == "last message repeated 2 times");
        CHECK(l.finish(4).empty());
    }
    {   // The same text at another level is a new line.
        PushLogLimiter l;
        CHECK(l.take(24, "y", 0).size() == 1);
        CHECK(l.take(16, "y", 1).size() == 1);
    }
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
