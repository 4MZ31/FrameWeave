// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// 60 -> 30 selection (engine/CaptureHalfRate.h) for a game that drops below 30 FPS, as a simulation
// of a 60 Hz capture: every slot carries the number of the picture the game showed in it.
//  - below 30, no picture is handed on twice (frame generation would interpolate a pair that does not
//    move), every picture is taken once, at its first copy, with its own time stamp;
//  - a still (a menu) is kept on the grid as before, and the first picture after it comes at once;
//  - 60 FPS content, steady 30 FPS content and every stream without picture numbers are decided
//    exactly as in 3.2.10: compared decision by decision with a copy of that class.
// Pure: builds anywhere (g++ too).
#include "veyra/engine/CaptureHalfRate.h"
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>
namespace before {
// The class exactly as it was in 3.2.10 (the reference the unchanged cases are compared with).
class CaptureHalfRate {
public:
    static constexpr unsigned kSecondCopies=3;
    void reset(){anchored_=false;havePicture_=false;previousNew_=false;shift_=false;secondCopies_=0;lastSequence_=nextSequence_=0;}
    static bool supported(double fps){return std::isfinite(fps)&&fps>=59&&fps<=61;}
    // picture: the sample's picture number, nullptr when unknown.
    bool accept(int64_t pts,int64_t transportInterval,bool boundary,const uint64_t* picture=nullptr,uint64_t sequence=0){
        if(transportInterval<=0){reset();return true;}
        const bool repeat=picture&&havePicture_&&*picture==lastPicture_;
        const bool adjacent=sequence&&lastSequence_?sequence==lastSequence_+1:
            anchored_&&pts>last_&&pts-last_>=transportInterval*3/4&&pts-last_<=transportInterval*5/4;
        const bool secondCopy=repeat&&previousNew_&&adjacent;
        previousNew_=picture&&!repeat;
        havePicture_=picture!=nullptr;if(picture)lastPicture_=*picture;
        const bool sequenceRestart=sequence&&lastSequence_&&sequence<=lastSequence_;
        lastSequence_=sequence;
        if(boundary||!anchored_||sequenceRestart||pts<last_||pts-last_>transportInterval*6){
            anchored_=true;next_=pts+transportInterval*2;nextSequence_=sequence?sequence+2:0;last_=pts;shift_=false;secondCopies_=0;return true;
        }
        last_=pts;
        if(sequence&&!nextSequence_)nextSequence_=sequence;
        const bool beforeDue=sequence?sequence<nextSequence_:pts+transportInterval/4<next_;
        if(beforeDue){
            if(!shift_||!picture||repeat||!adjacent)return false;
            // The first copy of the next picture: move the grid onto it.
            shift_=false;secondCopies_=0;next_=pts+transportInterval*2;nextSequence_=sequence?sequence+2:0;++shifts_;return true;
        }
        const auto period=transportInterval*2;
        next_+=((pts+transportInterval/4-next_)/period+1)*period;
        // Driver PTS can run at the wrong speed. When available the transport
        // sequence selects one callback per pair, preserving the original PTS.
        // A late reader consumes the newest sample once, then starts a new pair
        // there; it must not immediately take an adjacent catch-up sample.
        if(sequence)nextSequence_=sequence+2;
        secondCopies_=secondCopy?secondCopies_+1:0;
        shift_=secondCopies_>=kSecondCopies;
        ++grid_;secondKept_+=secondCopy;
        return true;
    }
    uint64_t shifts()const{return shifts_;}
    // Samples the grid kept, and how many of them were second copies (diagnostics).
    uint64_t gridKept()const{return grid_;}
    uint64_t secondCopiesKept()const{return secondKept_;}
private:
    bool anchored_=false,havePicture_=false,previousNew_=false,shift_=false;int64_t next_=0,last_=0;
    uint64_t lastSequence_=0,nextSequence_=0;
    uint64_t lastPicture_=0,shifts_=0,grid_=0,secondKept_=0;unsigned secondCopies_=0;
};
}

using veyra::engine::CaptureHalfRate;
static int failures = 0;
static void check(bool ok, const std::string& label) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str()); if (!ok) ++failures; }

struct Stream {
    std::vector<int> durations;          // slots each picture lasts
    bool sequence = true;                // transport sequence numbers known
    bool pictures = true;                // picture numbers known
    int jitter = 0;                      // +- 100 ns units on every time stamp
    std::int64_t interval = 166667;      // 60 Hz
    unsigned seed = 1;
    int lostEvery = 0;                   // every n-th transport sample never arrives
};
struct Outcome {
    std::vector<int> kept;               // slots kept
    std::vector<std::int64_t> pts;       // their time stamps as handed in
    int pictureCount = 0, repeated = 0, missed = 0, late = 0, adjacent = 0, closest = 1000;
    std::uint64_t skipped = 0, held = 0, stillEnds = 0, shifts = 0;
    std::vector<bool> decisions;
};
static std::vector<std::uint64_t> slotsOf(const std::vector<int>& durations, std::vector<int>* first = nullptr) {
    std::vector<std::uint64_t> at;
    for (std::size_t p = 0; p < durations.size(); ++p) { if (first) first->push_back(int(at.size())); for (int k = 0; k < durations[p]; ++k) at.push_back(p + 1); }
    return at;
}
template <class Sampler> static Outcome run(const Stream& s) {
    Sampler h; Outcome o; std::mt19937 rng(s.seed); std::uniform_int_distribution<int> j(-s.jitter, s.jitter);
    std::vector<int> first; const auto at = slotsOf(s.durations, &first);
    std::vector<int> keptAt(s.durations.size(), -1);
    int lastSlot = -1000; std::uint64_t lastPicture = 0;
    for (int n = 0; n < int(at.size()); ++n) {
        if (s.lostEvery && n % s.lostEvery == s.lostEvery - 1) continue;
        const std::int64_t pts = std::int64_t(n) * s.interval + (s.jitter ? j(rng) : 0);
        const std::uint64_t picture = at[n];
        const bool keep = h.accept(pts, s.interval, n == 0, s.pictures ? &picture : nullptr, s.sequence ? std::uint64_t(n + 1) : 0);
        o.decisions.push_back(keep);
        if (!keep) continue;
        o.kept.push_back(n); o.pts.push_back(pts);
        if (lastSlot >= 0) { if (n - lastSlot == 1) ++o.adjacent; if (n - lastSlot < o.closest) o.closest = n - lastSlot; }
        if (picture == lastPicture) ++o.repeated;
        else { keptAt[picture - 1] = n; o.late += n - first[picture - 1]; }
        lastSlot = n; lastPicture = picture;
    }
    o.pictureCount = int(s.durations.size());
    for (std::size_t p = 0; p + 1 < s.durations.size(); ++p) if (keptAt[p] < 0) ++o.missed;
    return o;
}
static Outcome now(const Stream& s) {
    Outcome o = run<CaptureHalfRate>(s);
    // The counters, from a second identical run.
    CaptureHalfRate h; std::mt19937 rng(s.seed); std::uniform_int_distribution<int> j(-s.jitter, s.jitter); const auto at = slotsOf(s.durations);
    for (int n = 0; n < int(at.size()); ++n) {
        if (s.lostEvery && n % s.lostEvery == s.lostEvery - 1) continue;
        const std::int64_t pts = std::int64_t(n) * s.interval + (s.jitter ? j(rng) : 0); const std::uint64_t picture = at[n];
        h.accept(pts, s.interval, n == 0, s.pictures ? &picture : nullptr, s.sequence ? std::uint64_t(n + 1) : 0);
    }
    o.skipped = h.repeatsSkipped(); o.held = h.repeatsHeld(); o.stillEnds = h.stillEnds(); o.shifts = h.shifts();
    return o;
}
// A game at a steady rate in a 60 Hz capture; `phase` moves where its pictures start.
static std::vector<int> steady(double fps, int seconds, double phase) {
    std::vector<int> d; const int total = seconds * 60; const double per = 60.0 / fps; int last = -1, count = 0;
    for (int n = 0; n < total; ++n) { const int p = int((n + phase) / per); if (p != last) { if (count) d.push_back(count); count = 0; last = p; } ++count; }
    if (count) d.push_back(count);
    return d;
}
// A picture kept at its first slot is due again two slots later, and at every slot after that until the next one comes.
static int overstays(const std::vector<int>& d) { int n = 0; for (int slots : d) n += slots > 2 ? slots - 2 : 0; return n; }
static std::string name(const char* what, double fps, double phase) { char b[96]; std::snprintf(b, sizeof b, "%s (%.2f FPS, phase %.0f)", what, fps, phase); return b; }

int main() {
    const int seconds = 20;
    // --- below 30: what the fix is for -------------------------------------------------------------
    for (double fps : {29.0, 28.0, 26.0, 24.0, 20.0, 15.0, 12.0}) for (double phase : {0.0, 1.0}) {
        Stream s; s.durations = steady(fps, seconds, phase);
        const Outcome before = run<before::CaptureHalfRate>(s), o = now(s);
        check(before.repeated > 0, name("the old selection handed a picture on twice", fps, phase));
        check(o.repeated == 0, name("no picture is handed on twice", fps, phase));
        check(o.missed == 0 && int(o.kept.size()) == o.pictureCount, name("every picture is taken exactly once", fps, phase));
        check(o.late == 0, name("each at its first copy", fps, phase));
        check(o.adjacent == 0 && o.closest >= 2, name("never two samples in a row", fps, phase));
        check(int(o.skipped) == overstays(s.durations) && o.held == 0, name("one passed-over repeat for every due slot a picture overstays", fps, phase));
        bool own = true; for (std::size_t i = 0; i < o.kept.size(); ++i) own = own && o.pts[i] == std::int64_t(o.kept[i]) * s.interval;
        check(own, name("time stamps are the samples' own", fps, phase));
    }
    {   // The figures the fault was reported with: repeated pairs in 20 s at 29 / 26 / 24 FPS.
        Stream a; a.durations = steady(29, seconds, 0); Stream b; b.durations = steady(26, seconds, 0); Stream c; c.durations = steady(24, seconds, 0);
        const int was29 = run<before::CaptureHalfRate>(a).repeated, was26 = run<before::CaptureHalfRate>(b).repeated, was24 = run<before::CaptureHalfRate>(c).repeated;
        std::printf("INFO repeated pairs in 20 s before: 29 FPS %d, 26 FPS %d, 24 FPS %d\n", was29, was26, was24);
        check(was29 == 40 && was26 == 80 && was24 == 120, "the old selection repeated 40 / 80 / 120 pairs at 29 / 26 / 24 FPS");
        const int kept29 = int(run<before::CaptureHalfRate>(a).kept.size());
        check(kept29 == 31 * seconds, "and kept 31 samples a second from a 29 FPS game");
        check(int(now(a).kept.size()) == 29 * seconds, "now 29: the game's own rate");
    }
    {   // An exact case, slot by slot: pictures lasting 2 2 3 2 4 2 2 5 2 slots.
        Stream s; s.durations = {2, 2, 3, 2, 4, 2, 2, 5, 2, 2};
        const Outcome o = now(s);
        const std::vector<int> expected = {0, 2, 4, 7, 9, 13, 15, 17, 22, 24};
        check(o.kept == expected, "slots 0 2 4 7 9 13 15 17 22 24 are kept");
        check(o.skipped == 1 + 2 + 3 && o.held == 0 && o.stillEnds == 0, "six due repeats passed over, nothing held");
    }
    {   // Without transport sequence numbers, on jittered time stamps, and at 59.94 Hz.
        for (double fps : {28.0, 24.0}) {
            Stream s; s.durations = steady(fps, seconds, 1); s.sequence = false; s.jitter = 15000;   // +-1.5 ms
            const Outcome o = now(s);
            check(o.repeated == 0 && o.missed == 0 && o.late == 0 && o.adjacent == 0, name("time stamps alone, +-1.5 ms", fps, 1));
            s.interval = 166833; s.jitter = 8000;
            const Outcome p = now(s);
            check(p.repeated == 0 && p.missed == 0 && p.late == 0 && p.adjacent == 0, name("59.94 Hz, time stamps alone", fps, 1));
        }
    }
    {   // A 30 FPS game with hitches of three and four slots, at random.
        Stream s; std::mt19937 rng(11); std::uniform_int_distribution<int> u(0, 9); int total = 0;
        while (total < seconds * 60) { const int x = u(rng); const int n = x == 0 ? 4 : x < 3 ? 3 : 2; s.durations.push_back(n); total += n; }
        const Outcome before = run<before::CaptureHalfRate>(s), o = now(s);
        check(before.repeated > 20 && before.adjacent > 5, "a 30 FPS game with hitches: the old selection repeated pictures and doubled up samples");
        check(o.repeated == 0 && o.missed == 0 && o.late == 0 && o.adjacent == 0, "now every picture once, at its first copy, never two in a row");
    }
    {   // A transport sample that never arrives does not bring the fault back.
        Stream s; s.durations = steady(26, seconds, 0); s.lostEvery = 37;
        const Outcome o = now(s);
        check(o.repeated == 0 && o.adjacent == 0, "26 FPS with lost transport samples: no picture twice");
        check(o.late <= o.pictureCount / 10, "and a picture is late only where its first copy was the lost sample");
    }
    // --- a still: kept on the grid, and left at once -----------------------------------------------
    for (int still : {40, 41, 90, 91}) {
        Stream s; s.durations = {2, 2, still, 2, 2, 2, 2, 2, 2};
        const Outcome o = now(s);
        std::vector<int> first; slotsOf(s.durations, &first);
        const int begin = first[2], end = first[3];
        int holds = 0, lastHold = begin; bool even = true;
        for (int k : o.kept) if (k > begin && k < end) { ++holds; even = even && (k - lastHold == 2 || lastHold == begin); lastHold = k; }
        char label[96];
        std::snprintf(label, sizeof label, "a still of %d slots is kept on the grid, every second slot", still);
        check(holds >= (still - int(CaptureHalfRate::kHitchSlots)) / 2 && even && int(o.held) == holds, label);
        std::snprintf(label, sizeof label, "its first refresh comes %u slots after the picture arrived", CaptureHalfRate::kHitchSlots);
        bool firstRefresh = false; for (int k : o.kept) if (k == begin + int(CaptureHalfRate::kHitchSlots)) firstRefresh = true;
        check(firstRefresh, label);
        std::snprintf(label, sizeof label, "after a still of %d slots every picture comes at its first copy", still);
        check(o.late == 0 && o.missed == 0 && o.shifts == 0, label);
        const Outcome before = run<before::CaptureHalfRate>(s);
        if (still % 2) check(before.late > 0, "the old selection showed the pictures after an odd still one sample late");
        check(o.stillEnds <= 1, "at most one sample is taken out of step for it");
    }
    {   // Exactly at the window: five slots is a hitch, six is a still.
        Stream five; five.durations = {2, 5, 2, 2}; Stream six; six.durations = {2, 6, 2, 2};
        const Outcome a = now(five), b = now(six);
        check(a.kept == std::vector<int>({0, 2, 7, 9}) && a.held == 0, "a picture of five slots is passed over until the next one");
        check(b.kept == std::vector<int>({0, 2, 7, 8, 10}) && b.held == 1 && b.stillEnds == 1, "one of six slots is refreshed once, and the next picture still comes at once");
    }
    // --- what must not change ----------------------------------------------------------------------
    {
        struct Case { const char* what; Stream s; };
        std::vector<Case> cases;
        for (double fps : {60.0, 30.0, 40.0, 50.0}) for (double phase : {0.0, 1.0}) {
            Stream s; s.durations = steady(fps, seconds, phase); cases.push_back({"picture numbers and sequence", s});
            s.sequence = false; s.jitter = 15000; cases.push_back({"picture numbers, jittered time stamps", s});
            s.interval = 166833; cases.push_back({"59.94 Hz", s});
            s.sequence = true; s.jitter = 0; s.interval = 166667; s.lostEvery = 37; cases.push_back({"lost transport samples", s});
        }
        for (double fps : {60.0, 30.0, 29.0, 26.0, 24.0, 20.0, 10.0}) for (bool sequence : {true, false}) {
            Stream s; s.durations = steady(fps, seconds, 1); s.pictures = false; s.sequence = sequence; s.jitter = sequence ? 0 : 12000;
            cases.push_back({"no picture numbers", s});
        }
        int same = 0;
        for (const auto& c : cases) {
            const bool equal = run<before::CaptureHalfRate>(c.s).decisions == run<CaptureHalfRate>(c.s).decisions;
            if (!equal) check(false, std::string("decided exactly as before: ") + c.what);
            same += equal;
        }
        std::printf("INFO %d of %zu unchanged streams decided exactly as in 3.2.10\n", same, cases.size());
        check(same == int(cases.size()), "60 FPS content, steady 30 FPS content and streams without picture numbers are decided as in 3.2.10");
    }
    {   // A boundary (a new source, a format change) forgets the picture kept before it.
        CaptureHalfRate h; const std::uint64_t a = 7;
        check(h.accept(0, 166667, true, &a, 1), "the first sample is kept");
        check(!h.accept(166667, 166667, false, &a, 2), "its repeat one slot later is not due");
        check(h.accept(333334, 166667, true, &a, 3), "the first sample after a boundary is kept even with the same picture number");
        h.reset();
        check(h.accept(500000, 166667, false, &a, 4) && h.repeatsSkipped() == 0, "and after a reset");
        check(h.accept(1, 0, false, &a, 5), "an unknown transport interval keeps everything");
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASSED", failures);
    return failures ? 1 : 0;
}
