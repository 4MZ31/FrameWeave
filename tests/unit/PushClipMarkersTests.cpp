// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// Clip markers (engine/PushClipMarkers.h). Pure: also builds with g++.
#include "veyra/engine/PushClipMarkers.h"
#include <cstdio>
#include <limits>
using namespace veyra::engine::clipmarks;
static int failures = 0;
#define CHECK(x) do { if (!(x)) { ++failures; std::printf("FAILED line %d: %s\n", __LINE__, #x); } } while (0)
int main() {
    // The time as people write it.
    CHECK(timeText(0) == "0:00:00.0");
    CHECK(timeText(83.44) == "0:01:23.4");
    CHECK(timeText(83.46) == "0:01:23.5");
    CHECK(timeText(59.96) == "0:01:00.0");          // rounded once: never 0:00:60.0
    CHECK(timeText(3599.96) == "1:00:00.0");
    CHECK(timeText(36000 + 62.5) == "10:01:02.5");  // hours are not padded and not cut
    CHECK(timeText(-5) == "0:00:00.0");
    CHECK(timeText(std::numeric_limits<double>::quiet_NaN()) == "0:00:00.0");
    CHECK(timeText(std::numeric_limits<double>::infinity()) == "0:00:00.0");
    CHECK(timeText(1e12) == "99:59:59.9");          // a nonsense time is the longest one, not garbage

    // A label is one safe line.
    CHECK(cleanLabel("Boss fight") == "Boss fight");
    CHECK(cleanLabel("  two\r\nlines\there  ") == "two lines here");
    CHECK(cleanLabel("\n\n") == "");
    CHECK(cleanLabel("a   b") == "a b");
    CHECK(cleanLabel("\xE6\xA0\x87\xE8\xAE\xB0 1") == "\xE6\xA0\x87\xE8\xAE\xB0 1");   // "标记 1" in UTF-8 is kept
    CHECK(cleanLabel("ok\xFF\xFE!") == "ok!");                                       // bytes that are not UTF-8 are dropped
    CHECK(cleanLabel("cut\xE6\xA0") == "cut");                                        // a sequence cut short is dropped
    {
        std::string longText;
        for (int i = 0; i < 100; ++i) longText += "\xE6\xA0\x87";   // 100 characters of 3 bytes
        const auto cut = cleanLabel(longText);
        CHECK(cut.size() == kMaxLabel * 3);                          // 80 whole characters, never half of one
        CHECK(cleanLabel(std::string(200, 'x')).size() == kMaxLabel);
    }

    // The file's text: what it is, then the markers by time.
    {
        const std::vector<Marker> marks{{125.0, "second"}, {12.34, "first"}, {125.0, "also second"}, {4000, ""}};
        const auto text = fileText("2026-11-20 21-14-05.mp4", marks);
        CHECK(text == "PushFPS markers for 2026-11-20 21-14-05.mp4\r\n"
                      "0:00:12.3  first\r\n"
                      "0:02:05.0  second\r\n"
                      "0:02:05.0  also second\r\n"
                      "1:06:40.0\r\n");
        CHECK(fileText("clip.mp4", {}) == "PushFPS markers for clip.mp4\r\n");
        // A file name can never add a line to the file.
        CHECK(fileText("evil\r\n0:00:00.0  fake.mp4", {}) == "PushFPS markers for evil 0:00:00.0 fake.mp4\r\n");
    }

    // The markers file's name: beside the recording, never anywhere else.
    CHECK(sidecarName("clip.mp4") == "clip-markers.txt");
    CHECK(sidecarName("C:\\Videos\\PushFPS\\2026-11-20 21-14-05.mkv") == "2026-11-20 21-14-05-markers.txt");
    CHECK(sidecarName("/home/a/b.c/clip") == "clip-markers.txt");
    CHECK(sidecarName("a/b/clip.part.mp4") == "clip.part-markers.txt");
    CHECK(sidecarName(".hidden") == ".hidden-markers.txt");
    CHECK(sidecarName("..\\..\\clip.mp4") == "clip-markers.txt");

    // The book: the markers of the file being written.
    {
        Book book;
        CHECK(book.add("", 3, "x") == 0);                                  // no file yet
        CHECK(book.add("a.mp4", std::numeric_limits<double>::quiet_NaN(), "x") == 0);
        CHECK(book.add("a.mp4", -1, "x") == 0);
        CHECK(book.count("a.mp4") == 0);
        CHECK(book.add("a.mp4", 10, "one") == 1);
        CHECK(book.add("a.mp4", 20, "two\nlines") == 2);
        CHECK(book.count("a.mp4") == 2 && book.count("b.mp4") == 0);
        CHECK(book.markers()[1].label == "two lines");
        // A recording in parts: the next file starts its own list at its own zero.
        CHECK(book.add("a-2.mp4", 1.5, "three") == 1);
        CHECK(book.file() == "a-2.mp4" && book.count("a.mp4") == 0 && book.markers().size() == 1);
        book.clear();
        CHECK(book.file().empty() && book.markers().empty());
        // Never without an end.
        for (std::size_t i = 0; i < kMaxMarkers; ++i) CHECK(book.add("long.mp4", double(i), "m") == i + 1);
        CHECK(book.add("long.mp4", 9999, "one too many") == 0);
        CHECK(book.count("long.mp4") == kMaxMarkers);
    }
    // A marker is in the book only once its file was written: staged first, taken over after.
    {
        Book book;
        CHECK(book.add("a.mp4", 10, "one") == 1);
        // Staging changes nothing, whatever becomes of the write.
        auto lost = book.stage("a.mp4", 20, "two");
        CHECK(lost.valid() && lost.number == 2 && lost.file == "a.mp4" && lost.markers.size() == 2);
        CHECK(lost.markers[0].label == "one" && lost.markers[1].label == "two" && lost.markers[1].seconds == 20);
        CHECK(book.count("a.mp4") == 1 && book.markers().size() == 1);          // the write failed: nothing was taken
        // The next marker takes the number the lost one had, and the lost one is not in its list.
        auto kept = book.stage("a.mp4", 30, "two again");
        CHECK(kept.valid() && kept.number == 2 && kept.markers.size() == 2 && kept.markers[1].seconds == 30);
        book.commit(std::move(kept));
        CHECK(book.count("a.mp4") == 2 && book.markers()[1].label == "two again" && book.markers()[1].seconds == 30);
        // A new part whose first marker could not be written: the book still holds the earlier part, whole.
        auto part = book.stage("a-2.mp4", 1, "first of part two");
        CHECK(part.valid() && part.number == 1 && part.markers.size() == 1);
        CHECK(book.file() == "a.mp4" && book.count("a.mp4") == 2 && book.count("a-2.mp4") == 0);
        book.commit(std::move(part));
        CHECK(book.file() == "a-2.mp4" && book.count("a-2.mp4") == 1 && book.count("a.mp4") == 0);
        // Refused markers are not valid, and taking one over does nothing.
        CHECK(!book.stage("", 3, "x").valid());
        CHECK(!book.stage("a-2.mp4", std::numeric_limits<double>::quiet_NaN(), "x").valid());
        CHECK(!book.stage("a-2.mp4", -1, "x").valid());
        book.commit(book.stage("a-2.mp4", -1, "x"));
        CHECK(book.file() == "a-2.mp4" && book.count("a-2.mp4") == 1);
        // The limit holds for a staged marker too, and a full file's list is not handed out.
        Book full;
        for (std::size_t i = 0; i < kMaxMarkers; ++i) CHECK(full.add("long.mp4", double(i), "m") == i + 1);
        const auto over = full.stage("long.mp4", 9999, "one too many");
        CHECK(!over.valid() && over.markers.empty() && full.count("long.mp4") == kMaxMarkers);
        // A label is cleaned when staged, as when added.
        CHECK(Book().stage("u.mp4", 1, "two\nlines").markers[0].label == "two lines");
    }
    if (failures) { std::printf("%d failed\n", failures); return 1; }
    std::printf("PushClipMarkersTests: all passed\n");
    return 0;
}
