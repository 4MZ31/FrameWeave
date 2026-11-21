// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// The files of one recording: one part, or several that follow each other without a gap.
//
// The recorder's writer thread gives it the encoder's pictures and the sound packets in the
// recording's own time; it puts each into the part it belongs to and changes files at a keyframe
// when engine/PushRecordParts.h says so. One encoder runs through all of it, which is what makes
// the cut seamless. Only FFmpeg is used here (no Direct3D, no encoder), so the files it writes are
// checked by a test of their own (tests/unit/PushPartWriterTests.cpp).
//
// A recording that is not cut is written exactly as before: one file, the same streams, the same
// times, the same calls in the same order.
#pragma once

#include "veyra/engine/PushRecordParts.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct AVCodecParameters;
struct AVFormatContext;
struct AVPacket;

namespace veyra::engine {

class PushPartWriter {
public:
    struct SoundTrack {
        const AVCodecParameters* parameters = nullptr;
        std::string name;   // the track's name in the file
    };
    struct Closed {
        std::wstring path;
        unsigned number = 1;   // 1: the recording's first file
        uint64_t frames = 0;
        uint64_t bytes = 0;    // the file's size as it was closed
        int64_t originUs = 0;  // the recording's time its first picture has
        bool ok = false;       // its end was written
        bool removed = false;  // it held no picture and was deleted
    };
    struct Config {
        const AVCodecParameters* video = nullptr;   // copied into every part; must stay valid while writing
        uint32_t fps = 60;
        int audioRate = 48000;
        std::vector<SoundTrack> sound;
        bool matroska = false;
        recordparts::Limits limits;
        std::function<std::wstring(unsigned number)> nextPath;   // the file of part 2, 3, ...
        std::function<void(const Closed&)> closed;               // writing thread: a part's file is closed
    };

    PushPartWriter();
    ~PushPartWriter();
    PushPartWriter(const PushPartWriter&) = delete;
    PushPartWriter& operator=(const PushPartWriter&) = delete;

    bool open(Config config, const std::wstring& path);
    // The picture that follows what was written: does the part end before it?
    bool due(int64_t ptsUs, bool key) const { return opened_ && plan_.endsBefore(key, ptsUs); }
    // Close the part for pictures and start the next with the picture at ptsUs. The older part
    // stays open for sound that is still on its way, until the cut after this one or finish().
    bool next(int64_t ptsUs);
    bool video(const uint8_t* data, size_t size, int64_t ptsUs, int64_t durationUs, bool key);
    // packet: its times in samples of the recording's clock; they are changed to the part's own.
    bool sound(unsigned track, AVPacket* packet);
    // Every part that is still open. False when a file's end could not be written.
    bool finish();
    // Frees everything without writing a file's end (after an error).
    void release();

    bool opened() const { return opened_; }
    const std::wstring& error() const { return error_; }
    const std::wstring& currentPath() const { return current_.path; }
    unsigned parts() const { return plan_.parts(); }
    int64_t currentOriginUs() const { return plan_.current().originUs; }
    uint64_t lostSound() const { return plan_.lostSound(); }
    uint64_t videoWritten() const { return videoWritten_; }

private:
    struct File {
        AVFormatContext* mux = nullptr;
        std::wstring path;
        unsigned number = 0;
        int64_t originUs = 0, lastDts = INT64_MIN;
        uint64_t frames = 0;
        bool headerWritten = false;
    };
    bool openFile(File& file, const std::wstring& path, unsigned number, int64_t originUs);
    bool closeFile(File& file);
    static void freeFile(File& file);

    Config config_;
    recordparts::Plan plan_;
    File current_, previous_;
    std::wstring error_;
    uint64_t videoWritten_ = 0;
    bool opened_ = false;
};

}  // namespace veyra::engine
