// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// A recording in parts, cut without a gap (engine/PushRecordParts.h): the decision alone, with a
// model of the writer that calls it in the order PushPartWriter does. No file, no codec: builds
// with g++ too. What the files themselves hold is checked on Windows (PushPartWriterTests.cpp).
#include "veyra/engine/PushRecordParts.h"

#include <cstdio>
#include <deque>
#include <vector>

using namespace veyra::engine::recordparts;

static int failures = 0, checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

namespace {
constexpr int64_t kRate = 48000;

struct Picture { int64_t ptsUs = 0; bool key = false; uint64_t bytes = 0; };
struct Sound { int64_t pts = 0; uint64_t bytes = 0; int64_t readyUs = 0; };   // pts in samples; when the writer can have it

struct WrittenPart {
    int64_t originUs = 0;
    std::vector<Picture> pictures;
    std::vector<int64_t> sound;   // each packet's start, in samples of the recording's clock
    uint64_t bytes = 0;
    bool closed = false;
};

// The writer, without files: pictures are held one behind (a picture's length is the distance to
// the next), sound is written up to the picture, and a part's file is closed one cut later.
struct Model {
    Plan plan;
    std::vector<WrittenPart> parts;
    std::deque<Sound> waiting;
    uint64_t lost = 0;
    int encodeLagUs = 20000;
    explicit Model(Limits limits, uint32_t fps = 60) : plan(limits, fps, kRate) { parts.emplace_back(); }

    void soundUpTo(int64_t videoUs, int64_t nowUs) {
        const int64_t limit = samplesAt(videoUs, kRate);
        while (!waiting.empty() && waiting.front().readyUs <= nowUs && waiting.front().pts + 1024 <= limit) {
            const Sound packet = waiting.front();
            waiting.pop_front();
            const auto where = plan.soundGoes(packet.pts);
            plan.wroteSound(packet.bytes, where);
            if (where == SoundGoes::Lost) { ++lost; continue; }
            auto& part = where == SoundGoes::Previous ? parts[parts.size() - 2] : parts.back();
            CHECK(!part.closed);
            part.sound.push_back(packet.pts);
            part.bytes += packet.bytes;
        }
    }
    void run(const std::vector<Picture>& pictures, std::vector<Sound> sound) {
        for (const auto& s : sound) waiting.push_back(s);
        const Picture* held = nullptr;
        for (const auto& next : pictures) {
            const int64_t now = next.ptsUs + encodeLagUs;
            if (held) {
                plan.wroteVideo(held->bytes, held->key);
                parts.back().pictures.push_back(*held);
                parts.back().bytes += held->bytes;
                if (plan.endsBefore(next.key, next.ptsUs)) {
                    soundUpTo(next.ptsUs, now);
                    if (parts.size() >= 2) parts[parts.size() - 2].closed = true;
                    plan.closePrevious();
                    plan.startNext(next.ptsUs);
                    parts.emplace_back();
                    parts.back().originUs = next.ptsUs;
                }
            }
            held = &next;
            soundUpTo(held->ptsUs, now);
        }
        if (held) {
            plan.wroteVideo(held->bytes, held->key);
            parts.back().pictures.push_back(*held);
            parts.back().bytes += held->bytes;
            // The end: everything that is left, whenever it arrives.
            const int64_t end = held->ptsUs + 1000000;
            while (!waiting.empty()) {
                const Sound packet = waiting.front();
                waiting.pop_front();
                if (packet.pts + 1024 > samplesAt(end, kRate)) break;
                const auto where = plan.soundGoes(packet.pts);
                plan.wroteSound(packet.bytes, where);
                if (where == SoundGoes::Lost) { ++lost; continue; }
                auto& part = where == SoundGoes::Previous ? parts[parts.size() - 2] : parts.back();
                part.sound.push_back(packet.pts);
                part.bytes += packet.bytes;
            }
        }
    }
};

uint32_t seed = 12345;
uint32_t roll() { seed = seed * 1664525u + 1013904223u; return seed >> 8; }

// A game recorded at `fps` with a keyframe every `group` pictures; sizes wander a little.
std::vector<Picture> pictures(int count, int fps, int group, uint64_t keyBytes, uint64_t otherBytes, int jitterUs = 0) {
    std::vector<Picture> out;
    for (int i = 0; i < count; ++i) {
        Picture p;
        p.ptsUs = int64_t(i) * 1000000 / fps + (jitterUs ? int64_t(roll() % (2 * jitterUs + 1)) - jitterUs : 0);
        if (i == 0) p.ptsUs = 0;
        if (!out.empty() && p.ptsUs <= out.back().ptsUs) p.ptsUs = out.back().ptsUs + 1;
        p.key = i % group == 0;
        const uint64_t base = p.key ? keyBytes : otherBytes;
        p.bytes = base - base / 10 + roll() % (base / 5 + 1);
        out.push_back(p);
    }
    return out;
}
// Sound for `seconds`: 1024 samples a packet, the first one the encoder's lead-in before 0.
std::vector<Sound> sound(double seconds, int lagUs) {
    std::vector<Sound> out;
    for (int64_t pts = -1024; pts < int64_t(seconds * kRate); pts += 1024) {
        Sound s;
        s.pts = pts; s.bytes = 480 + roll() % 60;
        s.readyUs = (pts + 1024) * 1000000 / kRate + lagUs;
        out.push_back(s);
    }
    return out;
}

void whole(const Model& m, const std::vector<Picture>& in) {
    // Every picture in exactly one part, in order; every part starts with a keyframe at its own time 0.
    size_t at = 0;
    bool ordered = true, keyed = true, origin = true, none = true;
    for (const auto& part : m.parts) {
        if (part.pictures.empty()) { none = false; continue; }
        if (!part.pictures.front().key) keyed = false;
        if (part.pictures.front().ptsUs != part.originUs) origin = false;
        for (const auto& p : part.pictures) {
            if (at >= in.size() || in[at].ptsUs != p.ptsUs) ordered = false;
            ++at;
        }
    }
    CHECK(ordered);
    CHECK(at == in.size());
    CHECK(keyed);
    CHECK(origin);
    CHECK(none);
    CHECK(m.plan.parts() == m.parts.size());
}

void soundWhole(const Model& m, size_t sent) {
    // Every packet in the part that holds its start; the one across a cut belongs to the older part.
    size_t written = 0;
    bool placed = true, rising = true;
    for (size_t k = 0; k < m.parts.size(); ++k) {
        const int64_t from = k ? samplesAt(m.parts[k].originUs, kRate) : INT64_MIN;
        const int64_t to = k + 1 < m.parts.size() ? samplesAt(m.parts[k + 1].originUs, kRate) : INT64_MAX;
        int64_t last = INT64_MIN;
        for (const int64_t pts : m.parts[k].sound) {
            if (pts < from || pts >= to) placed = false;
            if (pts <= last) rising = false;
            last = pts;
            ++written;
        }
    }
    CHECK(placed);
    CHECK(rising);
    CHECK(written + m.lost == sent);
}
}  // namespace

int main() {
    // --- one file: no limit, nothing ever ends ------------------------------------------------
    {
        Model m(Limits{});
        const auto in = pictures(60 * 600, 60, 120, 600000, 300000);
        const auto snd = sound(600, 5000);
        m.run(in, snd);
        CHECK(m.parts.size() == 1);
        CHECK(m.lost == 0);
        whole(m, in);
        soundWhole(m, snd.size());
        CHECK(!Limits{}.any());
        CHECK((Limits{1, 0}.any()) && (Limits{0, 1}.any()));
    }
    // --- by time: every part the chosen length, to within one keyframe interval ----------------
    for (const int group : {60, 120, 240, 420}) {
        Limits limits; limits.afterUs = 60 * 1000000LL;
        Model m(limits);
        const auto in = pictures(60 * 600, 60, group, 600000, 300000, 2000);
        const auto snd = sound(600, 5000);
        m.run(in, snd);
        whole(m, in);
        soundWhole(m, snd.size());
        CHECK(m.lost == 0);
        const int64_t groupUs = int64_t(group) * 1000000 / 60;
        bool lengths = true;
        for (size_t k = 0; k + 1 < m.parts.size(); ++k) {
            const int64_t length = m.parts[k + 1].originUs - m.parts[k].originUs;
            if (length < limits.afterUs - 500000 / 60 - 2000 || length >= limits.afterUs + groupUs + 4000) lengths = false;
        }
        CHECK(lengths);
        CHECK(m.parts.size() >= 600 / (60 + size_t(groupUs / 1000000) + 1) && m.parts.size() <= 10);
    }
    // A keyframe that comes half a frame early on the clock is the cut: not a whole interval later.
    {
        Limits limits; limits.afterUs = 10 * 1000000LL;
        Plan plan(limits, 60, kRate);
        plan.wroteVideo(1000, true);
        CHECK(!plan.endsBefore(true, 10 * 1000000LL - 9000));     // more than half a frame early: not yet
        CHECK(plan.endsBefore(true, 10 * 1000000LL - 8000));      // within half a frame
        CHECK(plan.endsBefore(true, 10 * 1000000LL));
        CHECK(!plan.endsBefore(false, 11 * 1000000LL));           // never at a picture that is no keyframe
    }
    // Never an empty part.
    {
        Limits limits; limits.afterUs = 1; limits.afterBytes = 1;
        Plan plan(limits, 60, kRate);
        CHECK(!plan.endsBefore(true, 5 * 1000000LL));
        plan.wroteVideo(10, true);
        CHECK(plan.endsBefore(true, 5 * 1000000LL));
        plan.startNext(5 * 1000000LL);
        CHECK(!plan.endsBefore(true, 9 * 1000000LL));
    }
    // --- by size: a part ends before the size, never after -------------------------------------
    for (const uint64_t megabytes : {256u, 1024u, 4000u}) {
        Limits limits; limits.afterBytes = uint64_t(megabytes) << 20;
        Model m(limits);
        // About 150 Mbit/s: 18.6 MB a second, 37 MB a group of two seconds.
        const int seconds = megabytes >= 4000 ? 1500 : 400;
        const auto in = pictures(60 * seconds, 60, 120, 900000, 305000);
        const auto snd = sound(seconds, 5000);
        m.run(in, snd);
        whole(m, in);
        soundWhole(m, snd.size());
        CHECK(m.lost == 0);
        CHECK(m.parts.size() >= 3);
        bool under = true, full = true;
        uint64_t largest = 0;
        for (const auto& part : m.parts) {
            uint64_t group = 0;
            for (const auto& p : part.pictures) { if (p.key) { largest = std::max(largest, group); group = 0; } group += p.bytes; }
            largest = std::max(largest, group);
        }
        for (size_t k = 0; k < m.parts.size(); ++k) {
            if (m.parts[k].bytes > limits.afterBytes) under = false;
            // And not far under it: what is left is less than the reserve and one more group.
            if (k + 1 < m.parts.size() && m.parts[k].bytes + reserveFor(largest) + largest + (1 << 20) < limits.afterBytes) full = false;
        }
        CHECK(under);
        CHECK(full);
    }
    // The reserve is the largest group so far and a quarter.
    CHECK(reserveFor(0) == 0);
    CHECK(reserveFor(1000) == 1250);
    CHECK(reserveFor(40u << 20) == (50u << 20));
    // A group no larger than that after the cut never passes the size; the file's own size counts when it is further.
    {
        Limits limits; limits.afterBytes = 1000;
        Plan plan(limits, 60, kRate);
        plan.wroteVideo(100, true);
        for (int i = 0; i < 3; ++i) plan.wroteVideo(50, false);       // a group of 250
        CHECK(plan.largestGroup() == 250);
        CHECK(!plan.endsBefore(true, 1000000));                       // 250 + 312 <= 1000
        plan.wroteVideo(100, true);
        for (int i = 0; i < 3; ++i) plan.wroteVideo(50, false);       // 500 in the part
        CHECK(!plan.endsBefore(true, 2000000));                       // 500 + 312 <= 1000
        plan.wroteVideo(100, true);
        for (int i = 0; i < 3; ++i) plan.wroteVideo(50, false);       // 750
        CHECK(plan.endsBefore(true, 3000000));                        // 750 + 312 > 1000: the next group might not fit
        CHECK(plan.bytesNow() == 750);
        plan.fileSize(900);
        CHECK(plan.bytesNow() == 900);
        plan.startNext(3000000);
        CHECK(plan.bytesNow() == 0 && plan.current().originUs == 3000000 && plan.previous().bytes == 750);
        CHECK(plan.largestGroup() == 250);                            // what was learned is kept for the next part
    }
    // Sound counts towards the size and towards a group.
    {
        Limits limits; limits.afterBytes = 1000;
        Plan plan(limits, 60, kRate);
        plan.wroteVideo(100, true);
        plan.wroteSound(300, SoundGoes::Current);
        CHECK(plan.current().bytes == 400 && plan.largestGroup() == 400);
        CHECK(!plan.endsBefore(true, 1000000));                       // 400 + 500 <= 1000
        plan.wroteSound(120, SoundGoes::Current);
        CHECK(plan.endsBefore(true, 1000000));                        // 520 + 650 > 1000
    }
    // --- both limits: whichever comes first ----------------------------------------------------
    {
        Limits limits; limits.afterUs = 30 * 1000000LL; limits.afterBytes = uint64_t(2000) << 20;
        Model m(limits);
        const auto in = pictures(60 * 200, 60, 120, 900000, 305000);
        m.run(in, sound(200, 5000));
        whole(m, in);
        bool byTime = true;
        for (size_t k = 0; k + 1 < m.parts.size(); ++k)
            if (m.parts[k + 1].originUs - m.parts[k].originUs > 32 * 1000000LL + 4000 || m.parts[k].bytes > limits.afterBytes) byTime = false;
        CHECK(byTime);
        CHECK(m.parts.size() >= 6);
    }
    // --- sound across a cut --------------------------------------------------------------------
    // A microphone's sound reaches the writer after the picture of the same moment. The older part
    // stays open for it: nothing is lost and nothing lands in the wrong part.
    for (const int lagUs : {0, 5000, 30000, 80000, 250000}) {
        Limits limits; limits.afterUs = 20 * 1000000LL;
        Model m(limits);
        const auto in = pictures(60 * 130, 60, 120, 600000, 300000, 1500);
        const auto snd = sound(130, lagUs);
        m.run(in, snd);
        whole(m, in);
        soundWhole(m, snd.size());
        CHECK(m.lost == 0);
        CHECK(m.plan.lostSound() == 0);
        // A part's sound starts within one packet of its picture, and the part before it ends within one packet of the cut.
        bool tight = true;
        for (size_t k = 1; k < m.parts.size(); ++k) {
            const int64_t cut = samplesAt(m.parts[k].originUs, kRate);
            if (m.parts[k].sound.empty() || m.parts[k].sound.front() - cut >= 1024) tight = false;
            if (m.parts[k - 1].sound.empty() || cut - m.parts[k - 1].sound.back() > 1024) tight = false;
        }
        CHECK(tight);
    }
    // Where a packet goes, case by case.
    {
        Limits limits; limits.afterUs = 1000000;
        Plan plan(limits, 60, kRate);
        CHECK(plan.soundGoes(-1024) == SoundGoes::Current);          // the encoder's lead-in belongs to the first part
        CHECK(plan.soundGoes(0) == SoundGoes::Current);
        plan.wroteVideo(1000, true);
        plan.startNext(2000000);                                      // the second part starts at 2 s = sample 96000
        CHECK(plan.soundGoes(95999) == SoundGoes::Previous);
        CHECK(plan.soundGoes(95000) == SoundGoes::Previous);          // across the cut: the older part
        CHECK(plan.soundGoes(96000) == SoundGoes::Current);
        CHECK(plan.soundGoes(-1024) == SoundGoes::Previous);
        CHECK(plan.originSamples(SoundGoes::Previous) == 0 && plan.originSamples(SoundGoes::Current) == 96000);
        plan.wroteVideo(1000, true);
        plan.closePrevious();
        plan.startNext(4000000);                                      // the third part at 4 s = 192000
        CHECK(plan.soundGoes(191999) == SoundGoes::Previous);
        CHECK(plan.soundGoes(192000) == SoundGoes::Current);
        // For the first part, whose file is closed by now: counted, never written into the second.
        CHECK(plan.soundGoes(95000) == SoundGoes::Lost);
        plan.wroteSound(500, SoundGoes::Lost);
        CHECK(plan.lostSound() == 1 && plan.previous().bytes == 1000 && plan.current().bytes == 0);
        plan.closePrevious();
        CHECK(plan.soundGoes(191999) == SoundGoes::Lost);             // nothing older is open any more
        CHECK(plan.soundGoes(192000) == SoundGoes::Current);
    }
    // Sound later than a whole part is counted as lost, and the count is what the plan says.
    {
        Limits limits; limits.afterUs = 2 * 1000000LL;
        Model m(limits);
        const auto in = pictures(60 * 20, 60, 60, 60000, 30000);
        const auto snd = sound(20, 5 * 1000000);                      // five seconds late: two parts behind
        m.run(in, snd);
        whole(m, in);
        soundWhole(m, snd.size());
        CHECK(m.lost > 0);
        CHECK(m.plan.lostSound() == m.lost);
    }
    // Times in samples, rounded to the nearest.
    CHECK(samplesAt(0, 48000) == 0);
    CHECK(samplesAt(1000000, 48000) == 48000);
    CHECK(samplesAt(16667, 48000) == 800);
    CHECK(samplesAt(10, 48000) == 0 && samplesAt(11, 48000) == 1);
    CHECK(samplesAt(-1000000, 48000) == -48000);
    // A frame rate of 0 is taken as 1, a sound rate of 0 as 48000: no division by zero.
    {
        Limits limits; limits.afterUs = 1000000;
        Plan plan(limits, 0, 0);
        plan.wroteVideo(1, true);
        CHECK(plan.endsBefore(true, 600000));
        CHECK(plan.originSamples(SoundGoes::Current) == 0);
    }

    std::printf("push_record_parts_tests: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
