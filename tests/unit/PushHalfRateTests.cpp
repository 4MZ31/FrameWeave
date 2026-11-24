// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// 60 -> 30 selection (engine/CaptureHalfRate.h), as a simulation of a 60 Hz capture: every slot carries the
// number of the game picture shown in it and the picture number the capture callback gave it.
//  - The class decides every sample exactly as the engine release PushFPS is built on does (a copy of that
//    class is kept here as `release::`), so the two programs hand frame generation the same frames.
//  - What it is for, in numbers: a 30 FPS game that hitches (holds a picture for three samples, shows the
//    next for one) or runs below 30. The plain every-second-sample grid of 3.2.10, 4.1.0 and 4.1.1 (kept
//    here as `before::`) hands on the held picture twice and never sees the short one: a pair that does
//    not move, then a pair two game pictures apart. This class loses none.
//  - What it never does: lose a picture the comparison saw as new and the game showed for two samples.
//  - Where nothing repeats a kept picture (no picture numbers, a steady 30 FPS game, 60 FPS content) it
//    decides exactly as the plain grid.
// Pure: builds anywhere (g++ too).
#include "veyra/engine/CaptureHalfRate.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

// 3.2.10's class, as released: the plain grid (4.1.0 and 4.1.1 ran it too).
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

// 4.0's class, as released: what was tried.

// The class of the engine release PushFPS is built on, as released: the reference the class in the
// header is compared with, decision for decision.
namespace release {
class CaptureHalfRate {
public:
    static constexpr unsigned kSecondCopies=3;
    // Same hitch window as ContentDedup at a 30 fps cadence: 2.5 periods = 5 slots.
    static constexpr unsigned kHitchSlots=5;
    void reset(){anchored_=false;havePicture_=false;previousNew_=false;shift_=false;secondCopies_=0;lastSequence_=nextSequence_=0;keptValid_=holding_=waiting_=false;}
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
            anchored_=true;next_=pts+transportInterval*2;nextSequence_=sequence?sequence+2:0;last_=pts;shift_=false;secondCopies_=0;
            holding_=waiting_=false;kept(pts,picture,sequence);return true;
        }
        last_=pts;
        if(sequence&&!nextSequence_)nextSequence_=sequence;
        const bool beforeDue=sequence?sequence<nextSequence_:pts+transportInterval/4<next_;
        if(beforeDue){
            if(!shift_||!picture||repeat||!adjacent)return false;
            // The first copy of the next picture: move the grid onto it.
            shift_=false;secondCopies_=0;next_=pts+transportInterval*2;nextSequence_=sequence?sequence+2:0;++shifts_;
            holding_=false;kept(pts,picture,sequence);return true;
        }
        const bool repeatOfKept=picture&&keptValid_&&*picture==keptPicture_;
        if(repeatOfKept&&!holding_){
            const bool withinHitch=sequence&&keptSequence_?sequence-keptSequence_<kHitchSlots:
                pts-keptPts_+transportInterval/4<transportInterval*int64_t(kHitchSlots);
            // Due, but the game has not drawn a new picture yet: wait for it.
            if(withinHitch){waiting_=true;++repeatsSkipped_;return false;}
            holding_=true; // still picture: hold it on the grid
        }
        const auto period=transportInterval*2;
        // After a skipped repeat the grid restarts on this sample; otherwise it
        // keeps its phase.
        if(waiting_)next_=pts+period;
        else next_+=((pts+transportInterval/4-next_)/period+1)*period;
        // Driver PTS can run at the wrong speed. When available the transport
        // sequence selects one callback per pair, preserving the original PTS.
        // A late reader consumes the newest sample once, then starts a new pair
        // there; it must not immediately take an adjacent catch-up sample.
        if(sequence)nextSequence_=sequence+2;
        // A sample taken after a skipped repeat is the first copy of a new
        // picture (or a held still), so the phase is already where it belongs.
        if(waiting_){shift_=false;secondCopies_=0;}
        else{secondCopies_=secondCopy?secondCopies_+1:0;shift_=secondCopies_>=kSecondCopies;secondKept_+=secondCopy;}
        if(!repeatOfKept)holding_=false;
        held_+=repeatOfKept;
        waiting_=false;
        ++grid_;kept(pts,picture,sequence);
        return true;
    }
    uint64_t shifts()const{return shifts_;}
    // Samples the grid kept, and how many of them were second copies (diagnostics).
    uint64_t gridKept()const{return grid_;}
    uint64_t secondCopiesKept()const{return secondKept_;}
    // Due samples skipped because they repeated the last kept picture, and
    // repeats kept anyway because the picture stayed still past kHitchSlots.
    uint64_t repeatsSkipped()const{return repeatsSkipped_;}
    uint64_t repeatsHeld()const{return held_;}
private:
    void kept(int64_t pts,const uint64_t* picture,uint64_t sequence){
        keptValid_=picture!=nullptr;if(picture)keptPicture_=*picture;keptPts_=pts;keptSequence_=sequence;
    }
    bool anchored_=false,havePicture_=false,previousNew_=false,shift_=false;int64_t next_=0,last_=0;
    uint64_t lastSequence_=0,nextSequence_=0;
    uint64_t lastPicture_=0,shifts_=0,grid_=0,secondKept_=0;unsigned secondCopies_=0;
    bool keptValid_=false,holding_=false,waiting_=false;
    uint64_t keptPicture_=0,keptSequence_=0,repeatsSkipped_=0,held_=0;int64_t keptPts_=0;
};
}

using veyra::engine::CaptureHalfRate;
// The "original" 60 -> 30 mode: the same class with repeat skipping off.
struct OriginalHalfRate : CaptureHalfRate { OriginalHalfRate() { setSkipRepeats(false); } };
static int failures = 0;
static void check(bool ok, const std::string& label) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str()); if (!ok) ++failures; }

struct Stream {
    std::vector<int> durations;          // slots each game picture lasts
    std::vector<bool> seen;              // did the comparison see this picture as new (empty: every one)
    bool sequence = true;                // transport sequence numbers known
    bool pictures = true;                // picture numbers known
    int jitter = 0;                      // +- 100 ns units on every time stamp
    std::int64_t interval = 166667;      // 60 Hz
    unsigned seed = 1;
    int lostEvery = 0;                   // every n-th transport sample never arrives
};
struct Slots { std::vector<int> picture; std::vector<std::uint64_t> number; };
// The game picture in every slot, and the picture number the comparison gives it: a picture it does not
// see as new keeps the number of the one before.
static Slots slotsOf(const Stream& s) {
    Slots o; std::uint64_t number = 0;
    for (std::size_t p = 0; p < s.durations.size(); ++p) {
        if (p == 0 || s.seen.empty() || s.seen[p]) ++number;
        for (int k = 0; k < s.durations[p]; ++k) { o.picture.push_back(int(p)); o.number.push_back(number); }
    }
    return o;
}
template <class Sampler> static std::vector<bool> decisions(const Stream& s, Sampler* used = nullptr) {
    Sampler h; std::vector<bool> out; std::mt19937 rng(s.seed); std::uniform_int_distribution<int> j(-s.jitter, s.jitter);
    const Slots at = slotsOf(s);
    for (int n = 0; n < int(at.picture.size()); ++n) {
        if (s.lostEvery && n % s.lostEvery == s.lostEvery - 1) { out.push_back(false); continue; }
        const std::int64_t pts = std::int64_t(n) * s.interval + (s.jitter ? j(rng) : 0);
        const std::uint64_t picture = at.number[std::size_t(n)];
        out.push_back(h.accept(pts, s.interval, n == 0, s.pictures ? &picture : nullptr, s.sequence ? std::uint64_t(n + 1) : 0));
    }
    if (used) *used = h;
    return out;
}
// What a selection hands on: how many samples, how many game pictures never, how many pairs that show
// the same picture twice, how many pairs two or more game pictures apart, and the closest two keeps.
struct Handed { int kept = 0, lost = 0, lostSeen = 0, still = 0, doubles = 0, closest = 1000; double perSecond = 0; };
template <class Sampler> static Handed handed(const Stream& s) {
    const Slots at = slotsOf(s); const auto keep = decisions<Sampler>(s);
    std::vector<bool> got(s.durations.size(), false); Handed o; int last = -1, lastSlot = -1000;
    for (int n = 0; n < int(keep.size()); ++n) {
        if (!keep[std::size_t(n)]) continue;
        const int p = at.picture[std::size_t(n)];
        ++o.kept; got[std::size_t(p)] = true;
        if (last >= 0) { if (p == last) ++o.still; else if (p - last >= 2) ++o.doubles; }
        if (lastSlot > -1000) o.closest = std::min(o.closest, n - lastSlot);
        last = p; lastSlot = n;
    }
    for (std::size_t p = 0; p < got.size(); ++p) if (!got[p]) {
        ++o.lost;
        // A picture of two samples or more that the comparison saw as new.
        if (s.durations[p] >= 2 && (s.seen.empty() || s.seen[p])) ++o.lostSeen;
    }
    o.perSecond = keep.empty() ? 0 : o.kept * 60.0 / double(keep.size());
    return o;
}
// A game at a steady rate in a 60 Hz capture; `phase` moves where its pictures start.
static std::vector<int> steady(double fps, int seconds, double phase) {
    std::vector<int> d; const int total = seconds * 60; const double per = 60.0 / fps; int last = -1, count = 0;
    for (int n = 0; n < total; ++n) { const int p = int((n + phase) / per); if (p != last) { if (count) d.push_back(count); count = 0; last = p; } ++count; }
    if (count) d.push_back(count);
    return d;
}
// A 30 FPS game in which every `every`-th picture is replaced by `hitch` (slots per picture).
static std::vector<int> hitching(const std::vector<int>& hitch, int every, int seconds) {
    std::vector<int> d; int slots = 0, n = 0;
    while (slots < seconds * 60) { if (++n % every == 0) for (int v : hitch) { d.push_back(v); slots += v; } else { d.push_back(2); slots += 2; } }
    return d;
}

int main() {
    const int seconds = 20;
    // --- every sample is decided exactly as the engine release decides it ---------------------------------
    std::vector<Stream> all;
    {
        for (double fps : {60.0, 30.0, 40.0, 50.0, 29.0, 26.0, 24.0, 20.0, 10.0}) for (double phase : {0.0, 1.0}) {
            Stream s; s.durations = steady(fps, seconds, phase); all.push_back(s);
            s.sequence = false; s.jitter = 15000; all.push_back(s);
            s.interval = 166833; all.push_back(s);
            s.sequence = true; s.jitter = 0; s.interval = 166667; s.lostEvery = 37; all.push_back(s);
        }
        for (double fps : {60.0, 30.0, 29.0, 26.0, 24.0, 20.0, 10.0}) for (bool sequence : {true, false}) {
            Stream s; s.durations = steady(fps, seconds, 1); s.pictures = false; s.sequence = sequence; s.jitter = sequence ? 0 : 12000;
            all.push_back(s);
        }
        for (unsigned seed : {5u, 6u, 7u, 8u}) for (bool sequence : {true, false}) {
            // A 30 FPS game with hitches, stills and single-slot pictures, some of them unseen by the comparison.
            Stream s; s.seed = seed; s.sequence = sequence; s.jitter = sequence ? 0 : 9000;
            std::mt19937 rng(seed); std::uniform_int_distribution<int> pick(0, 99);
            for (int n = 0; n < 900; ++n) { const int r = pick(rng); s.durations.push_back(r < 70 ? 2 : r < 82 ? 3 : r < 90 ? 1 : r < 97 ? 4 : 30); s.seen.push_back(seed < 7 || pick(rng) < 93); }
            all.push_back(s);
        }
        for (const auto& hitch : {std::vector<int>{3, 1}, std::vector<int>{3}, std::vector<int>{4, 1, 1}, std::vector<int>{1, 3}}) for (int every : {4, 6, 30}) {
            Stream s; s.durations = hitching(hitch, every, seconds); all.push_back(s);
        }
        int same = 0;
        for (const auto& s : all) same += decisions<release::CaptureHalfRate>(s) == decisions<CaptureHalfRate>(s);
        std::printf("INFO %d of %zu streams decided exactly as the engine release decides them\n", same, all.size());
        check(same == int(all.size()), "every stream is decided exactly as the engine release decides it");
        // The original mode is the plain grid of 3.2.10 to 4.1.1, decision for decision, on every stream.
        int plainSame = 0;
        for (const auto& s : all) plainSame += decisions<before::CaptureHalfRate>(s) == decisions<OriginalHalfRate>(s);
        std::printf("INFO %d of %zu streams decided by the original mode exactly as by the plain grid\n", plainSame, all.size());
        check(plainSame == int(all.size()), "the original 60 -> 30 mode decides every stream exactly as the plain grid of 3.2.10 did");
        // Switching modes resets the sampler; switching to the mode it has changes nothing.
        CaptureHalfRate h; check(!h.setSkipRepeats(true) && h.skipRepeats() && h.setSkipRepeats(false) && !h.skipRepeats(), "switching between the modes");
    }
    // --- where nothing repeats a kept picture, the plain grid's decisions --------------------------------
    {
        std::vector<Stream> plain;
        for (double phase : {0.0, 1.0}) { Stream s; s.durations = steady(30, seconds, phase); plain.push_back(s); s.sequence = false; s.jitter = 15000; plain.push_back(s); }
        { Stream s; s.durations = steady(60, seconds, 0); plain.push_back(s); s.sequence = false; s.jitter = 15000; plain.push_back(s); }
        { Stream s; s.durations = steady(45, seconds, 0); plain.push_back(s); }
        for (double fps : {60.0, 30.0, 29.0, 26.0, 24.0, 20.0, 10.0}) for (bool sequence : {true, false}) {
            Stream s; s.durations = steady(fps, seconds, 1); s.pictures = false; s.sequence = sequence; s.jitter = sequence ? 0 : 12000; plain.push_back(s);
        }
        int same = 0;
        for (const auto& s : plain) same += decisions<before::CaptureHalfRate>(s) == decisions<CaptureHalfRate>(s);
        check(same == int(plain.size()), "a steady 30 FPS game, 60 FPS content and a card without picture numbers are decided exactly as by the plain grid");
    }
    // --- a hitch loses no picture and hands on no pair that does not move ---------------------------------
    for (int every : {30, 6, 4}) {
        Stream s; s.durations = hitching({3, 1}, every, 60);
        const Handed now = handed<CaptureHalfRate>(s), grid = handed<before::CaptureHalfRate>(s);
        const int hitches = int(s.durations.size()) / (every + 1);
        char label[200];
        std::snprintf(label, sizeof label, "a game that holds a picture three samples and shows the next for one, every %d pictures: no picture lost, no still pair, no double step", every);
        check(now.lost == 0 && now.still == 0 && now.doubles == 0 && now.closest >= 1, label);
        std::printf("INFO hitch every %d pictures, one minute: now %d lost, %d still pairs, %d double steps (%.2f a second); the plain grid %d lost, %d still pairs, %d double steps\n",
            every, now.lost, now.still, now.doubles, now.perSecond, grid.lost, grid.still, grid.doubles);
        check(grid.lost >= hitches - 2 && grid.doubles >= hitches - 2 && grid.still >= hitches - 2, "what the plain grid did there: at every hitch a picture lost, a still pair and a double step");
    }
    // --- a picture held a sample too long is not handed on twice -------------------------------------------
    for (int every : {30, 8}) {
        Stream s; s.durations = hitching({3}, every, 60);
        const Handed now = handed<CaptureHalfRate>(s), grid = handed<before::CaptureHalfRate>(s);
        check(now.lost == 0 && now.still == 0 && now.doubles == 0 && now.perSecond <= 30.01, "a picture held for three samples is handed on once, and never more than 30 a second");
        check(grid.still > 0 && grid.perSecond > 30.2, "what the plain grid did there: the held picture twice, and extra samples from moving its phase");
    }
    // --- below 30: each picture once, kept samples never adjacent ------------------------------------------
    for (double fps : {29.0, 28.0, 26.0, 24.0, 20.0}) for (bool sequence : {true, false}) {
        Stream s; s.durations = steady(fps, 60, 0); s.sequence = sequence; s.jitter = sequence ? 0 : 1000;
        CaptureHalfRate used; const auto keep = decisions<CaptureHalfRate>(s, &used);
        const Handed now = handed<CaptureHalfRate>(s), grid = handed<before::CaptureHalfRate>(s);
        char label[200];
        std::snprintf(label, sizeof label, "a game at %.0f FPS: each picture handed on once, never a repeat or an adjacent sample%s", fps, sequence ? "" : " (time stamps only)");
        check(now.lost <= 1 && now.still == 0 && now.doubles == 0 && now.closest >= 2 && used.shifts() == 0, label);
        if (sequence) std::printf("INFO %.0f FPS, one minute: now %d kept, %d still pairs; the plain grid %d kept, %d still pairs\n", fps, now.kept, now.still, grid.kept, grid.still);
        (void)keep;
    }
    // --- never a picture the comparison saw and the game showed for two samples -----------------------------
    {
        int lostSeen = 0, streams = 0, unseen = 0, lost = 0;
        for (unsigned seed = 1; seed <= 40; ++seed) for (int share : {100, 99, 95, 90, 70, 50}) {
            Stream s; s.seed = seed; std::mt19937 rng(seed * 977u + unsigned(share)); std::uniform_int_distribution<int> pick(0, 99);
            // Mostly two samples a picture, with hitches, short pictures and stills; `share` of them seen.
            for (int n = 0; n < 600; ++n) {
                const int r = pick(rng);
                s.durations.push_back(r < 72 ? 2 : r < 84 ? 3 : r < 90 ? 1 : r < 97 ? 4 : 12);
                const bool sees = pick(rng) < share; s.seen.push_back(sees); unseen += sees ? 0 : 1;
            }
            const Handed now = handed<CaptureHalfRate>(s);
            lostSeen += now.lostSeen; lost += now.lost; ++streams;
        }
        std::printf("INFO %d streams with hitches and with pictures the comparison did not see as new (%d of them): %d pictures not handed on, %d of those seen and two samples long\n", streams, unseen, lost, lostSeen);
        check(lostSeen == 0, "a picture the comparison saw as new and the game showed for two samples is never lost");
    }
    // --- a steady game with every picture seen is handed on whole ------------------------------------------
    {
        Stream s; s.durations = steady(30, seconds, 0);
        const Handed now = handed<CaptureHalfRate>(s);
        check(now.kept == 30 * seconds && now.lost == 0 && now.still == 0 && now.doubles == 0, "a steady 30 FPS game is handed on whole: 30 a second, every game picture");
    }
    {   // A boundary (a new source, a format change) starts the grid again.
        CaptureHalfRate h; const std::uint64_t a = 7;
        check(h.accept(0, 166667, true, &a, 1), "the first sample is kept");
        check(!h.accept(166667, 166667, false, &a, 2), "its repeat one slot later is not due");
        check(h.accept(333334, 166667, true, &a, 3), "the first sample after a boundary is kept even with the same picture number");
        h.reset();
        check(h.accept(500000, 166667, false, &a, 4), "and after a reset");
        check(h.accept(1, 0, false, &a, 5), "an unknown transport interval keeps everything");
        // A picture that stands still (a menu, a pause): passed over inside the hitch window, then held on
        // the grid, so the enhancement and frame generation keep a steady load.
        CaptureHalfRate g; const std::uint64_t same = 3; std::vector<int> keptAt;
        for (int n = 0; n < 60; ++n) if (g.accept(std::int64_t(n) * 166667, 166667, n == 0, &same, std::uint64_t(n + 1))) keptAt.push_back(n);
        bool onGrid = keptAt.size() >= 3 && keptAt[0] == 0 && keptAt[1] == int(CaptureHalfRate::kHitchSlots);
        for (std::size_t n = 2; n < keptAt.size(); ++n) onGrid = onGrid && keptAt[n] - keptAt[n - 1] == 2;
        check(onGrid && keptAt.size() == 29 && g.repeatsSkipped() == CaptureHalfRate::kHitchSlots - 2, "a picture that stands still is held on the grid after the hitch window (every second sample)");
        // Without picture numbers nothing is known about repeats: every second sample.
        CaptureHalfRate blind; int kept = 0;
        for (int n = 0; n < 60; ++n) kept += blind.accept(std::int64_t(n) * 166667, 166667, n == 0, nullptr, std::uint64_t(n + 1)) ? 1 : 0;
        check(kept == 30, "without picture numbers every second sample is kept");
    }
    {   // Two fixed cases with their exact answers (the engine release's own).
        const std::int64_t dt = 166667;
        // A freeze past the hitch window: picture 20 on samples 40 to 47. It is passed over three times,
        // then held on the grid twice (samples 45 and 47), and the game goes on.
        CaptureHalfRate freeze; std::vector<int> repeats; std::uint64_t lastKept = ~0ull;
        for (int i = 0; i < 120; ++i) {
            const std::uint64_t id = i < 40 ? std::uint64_t(i / 2) : i < 48 ? 20 : std::uint64_t(21 + (i - 48) / 2);
            if (freeze.accept(i * dt, dt, false, &id, std::uint64_t(i + 1))) { if (id == lastKept) repeats.push_back(i); lastKept = id; }
        }
        check(repeats == std::vector<int>{45, 47} && freeze.repeatsSkipped() == 3 && freeze.repeatsHeld() == 2, "a freeze longer than the hitch window is held on the grid");
        // A still screen is no sign that the grid sits on second copies: 120 moving, 89 still, 150 at 60 FPS.
        CaptureHalfRate still; unsigned stillKept = 0;
        for (int i = 0; i < 720; ++i) {
            const std::uint64_t id = i < 240 ? std::uint64_t(i / 2) : i < 421 ? 1000 : std::uint64_t(1000 + i - 420);
            stillKept += still.accept(i * dt, dt, false, &id) ? 1u : 0u;
        }
        check(still.shifts() == 0 && stillKept == 359 && still.repeatsSkipped() == CaptureHalfRate::kHitchSlots - 2, "a still screen followed by 60 FPS content adds no sample and moves no phase");
    }
    std::printf(failures ? "%d FAILED\n" : "ALL PASSED (0 failures)\n", failures);
    return failures ? 1 : 0;
}
