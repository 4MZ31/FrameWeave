#pragma once
#include <cmath>
#include <cstdint>
namespace veyra::engine {
// Explicit 60/59.94 transport -> 30/29.97 sampling before any GPU work.
// Keeps original PTS. It does not claim to detect unique pictures or repair
// irregular game cadence. Quarter-slot tolerance absorbs timestamp jitter.
//
// Phase (2026-10-27): a 30 fps game in 60 Hz shows every picture twice, and
// every second sample may sit on the second copies, so each picture reaches the
// GPU 16.7 ms after it arrived. With picture numbers (PictureCompare.h) a kept
// sample that shows the picture of the sample just before it is such a second
// copy; after kSecondCopies in a row the next sample, a first copy, is taken as
// well and the grid continues from it. 60 fps content never repeats and
// irregular content does not repeat on every kept sample, so both keep the
// plain grid; without picture numbers nothing changes.
//
// Below 30 (PushFPS 4.0). A game that cannot hold 30 shows some pictures three or
// four times. The plain grid then kept one picture twice, frame generation was
// handed a pair that does not move (a visible stall: 2 a second at 29 fps, 6 at
// 24), and the phase move above fired over and over, each time adding a sample
// right behind the last (31 to 32 kept a second). So a due sample that shows the
// last kept picture again is passed over while that keep is less than
// kHitchSlots samples old: the next new picture is taken in its place and the
// grid starts again from it. Kept samples stay two slots apart and keep their
// own PTS, so a 50 ms step is interpolated as 50 ms; nothing is made up to
// reach 30, the kept rate is the game's own.
// A picture that stands longer than the window is a still (a menu, a pause): it
// is kept on the grid as before, so enhancement and frame generation keep steady
// work. The grid's phase through a still is arbitrary, so the first new picture
// after one is taken at once, even one slot after the still's last keep, and the
// grid starts again from it. Waiting for the grid's own next slot would show
// that picture and the three after it one sample late (16.7 ms), until the
// phase move above caught up.
// Measured with tools kept in tests/unit/PushHalfRateTests.cpp (20 s of 60 Hz):
// 29 / 26 / 24 fps games had 40 / 80 / 120 repeated pairs before and have none;
// 60 fps and steady 30 fps content, and every stream without picture numbers,
// are decided exactly as before (compared decision by decision with a copy of
// the old class).
class CaptureHalfRate {
public:
    static constexpr unsigned kSecondCopies=3;
    // How long a repeated picture counts as a hitch: 5 samples, 83 ms at 60 Hz
    // (2.5 frames of a 30 fps game, the same window ContentDedup uses there).
    static constexpr unsigned kHitchSlots=5;
    void reset(){anchored_=false;havePicture_=false;previousNew_=false;shift_=false;secondCopies_=0;lastSequence_=nextSequence_=0;
        keptValid_=still_=waiting_=false;}
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
            still_=waiting_=false;keep(pts,picture,sequence);return true;
        }
        last_=pts;
        if(sequence&&!nextSequence_)nextSequence_=sequence;
        const bool beforeDue=sequence?sequence<nextSequence_:pts+transportInterval/4<next_;
        const bool keptAgain=picture&&keptValid_&&*picture==keptPicture_;
        if(beforeDue){
            // The first new picture after a still: taken at once (see the note above).
            if(still_&&picture&&keptValid_&&!keptAgain){
                still_=waiting_=shift_=false;secondCopies_=0;next_=pts+transportInterval*2;nextSequence_=sequence?sequence+2:0;
                ++stillEnds_;keep(pts,picture,sequence);return true;
            }
            if(!shift_||!picture||repeat||!adjacent)return false;
            // The first copy of the next picture: move the grid onto it.
            shift_=false;secondCopies_=0;next_=pts+transportInterval*2;nextSequence_=sequence?sequence+2:0;++shifts_;
            still_=false;keep(pts,picture,sequence);return true;
        }
        if(keptAgain&&!still_){
            const bool hitch=sequence&&keptSequence_?sequence-keptSequence_<kHitchSlots:
                pts-keptPts_+transportInterval/4<transportInterval*int64_t(kHitchSlots);
            // Due, but the game has not drawn its next picture yet: wait for it.
            if(hitch){waiting_=true;++repeatsSkipped_;return false;}
            still_=true;   // it stands still: from here it is kept on the grid
        }
        const auto period=transportInterval*2;
        // After a passed-over repeat the grid starts again from this sample; otherwise it keeps its phase.
        if(waiting_)next_=pts+period;
        else next_+=((pts+transportInterval/4-next_)/period+1)*period;
        // Driver PTS can run at the wrong speed. When available the transport
        // sequence selects one callback per pair, preserving the original PTS.
        // A late reader consumes the newest sample once, then starts a new pair
        // there; it must not immediately take an adjacent catch-up sample.
        if(sequence)nextSequence_=sequence+2;
        // A sample taken after a passed-over repeat is the first copy of a new picture (or the
        // still's own refresh), so the phase is where it belongs and nothing is to be moved.
        if(waiting_){shift_=false;secondCopies_=0;}
        else{secondCopies_=secondCopy?secondCopies_+1:0;shift_=secondCopies_>=kSecondCopies;secondKept_+=secondCopy;}
        if(!keptAgain)still_=false;
        repeatsHeld_+=keptAgain;
        waiting_=false;
        ++grid_;keep(pts,picture,sequence);
        return true;
    }
    uint64_t shifts()const{return shifts_;}
    // Samples the grid kept, and how many of them were second copies (diagnostics).
    uint64_t gridKept()const{return grid_;}
    uint64_t secondCopiesKept()const{return secondKept_;}
    // Due samples passed over because they showed the last kept picture again; such samples kept
    // all the same because the picture stood still past kHitchSlots; and new pictures taken at once
    // after a still.
    uint64_t repeatsSkipped()const{return repeatsSkipped_;}
    uint64_t repeatsHeld()const{return repeatsHeld_;}
    uint64_t stillEnds()const{return stillEnds_;}
private:
    void keep(int64_t pts,const uint64_t* picture,uint64_t sequence){
        keptValid_=picture!=nullptr;if(picture)keptPicture_=*picture;keptPts_=pts;keptSequence_=sequence;
    }
    bool keptValid_=false,still_=false,waiting_=false;
    uint64_t keptPicture_=0,keptSequence_=0,repeatsSkipped_=0,repeatsHeld_=0,stillEnds_=0;int64_t keptPts_=0;
    bool anchored_=false,havePicture_=false,previousNew_=false,shift_=false;int64_t next_=0,last_=0;
    uint64_t lastSequence_=0,nextSequence_=0;
    uint64_t lastPicture_=0,shifts_=0,grid_=0,secondKept_=0;unsigned secondCopies_=0;
};
}
