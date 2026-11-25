#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS clip markers: a moment marked while recording, kept beside the recording.
//
// A long session is recorded to find three moments in it afterwards. A key (or a button) while
// recording notes the time in the file being written, and the notes are saved as a small text file
// next to the recording: "<recording>-markers.txt", one line a marker, the time first:
//
//     PushFPS markers for 2026-11-20 21-14-05.mp4
//     0:01:23.4  Marker 1
//     0:14:02.0  Marker 2
//
// That is the form video sites take for chapters and every editor's person can read. Nothing is
// written into the recording itself, and no second encoder runs: a marker is a number and a name.
//
// Pure (no Windows, no Qt): the time's text, the label made safe, the file's text, and the book of
// markers for the file being written. A recording written in parts starts each file at 0, and the
// time given here is the time in that file (RecordingStatus::fileSeconds), so each part has its own
// markers file. The bridge (Recording.cpp) writes the file again at every marker, so nothing is
// lost when a recording ends badly.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace veyra::engine::clipmarks {
inline constexpr std::size_t kMaxMarkers = 500;   // in one file
inline constexpr std::size_t kMaxLabel = 80;      // characters of a label (code points, not bytes)

struct Marker {
    double seconds = 0;   // from the start of the file
    std::string label;    // UTF-8, one line
};

// "H:MM:SS.t": hours without padding, tenths of a second. A time that is not a number, or before
// the start, is the start.
inline std::string timeText(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) seconds = 0;
    // Whole tenths, rounded once, so 59.96 s is 0:01:00.0 and never 0:00:60.0.
    const auto tenths = static_cast<std::uint64_t>(std::llround(std::min(seconds, 359999.9) * 10.0));
    const std::uint64_t whole = tenths / 10, h = whole / 3600, m = whole / 60 % 60, s = whole % 60;
    std::string out = std::to_string(h);
    out += ':'; out += static_cast<char>('0' + m / 10); out += static_cast<char>('0' + m % 10);
    out += ':'; out += static_cast<char>('0' + s / 10); out += static_cast<char>('0' + s % 10);
    out += '.'; out += static_cast<char>('0' + tenths % 10);
    return out;
}

// A label as one line of at most kMaxLabel characters: control characters (line breaks, tabs) become
// spaces, runs of spaces become one, the ends are trimmed, and the cut never falls inside a UTF-8
// sequence. Bytes that are not UTF-8 are dropped.
inline std::string cleanLabel(std::string_view text) {
    std::string out;
    std::size_t characters = 0;
    bool space = false;
    for (std::size_t i = 0; i < text.size() && characters < kMaxLabel;) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
        bool valid = length > 0 && i + length <= text.size();
        for (std::size_t k = 1; valid && k < length; ++k)
            valid = (static_cast<unsigned char>(text[i + k]) & 0xC0) == 0x80;
        if (!valid) { ++i; continue; }
        if (length == 1 && (lead < 0x20 || lead == 0x7F || lead == ' ')) {
            space = !out.empty();
            ++i;
            continue;
        }
        if (space) { out += ' '; ++characters; space = false; if (characters >= kMaxLabel) break; }
        out.append(text.substr(i, length));
        ++characters;
        i += length;
    }
    return out;
}

// The markers file's text for a recording's file name: a first line saying what it is, then one
// line a marker in the order of their times (markers at the same time keep the order they were made in).
inline std::string fileText(std::string_view recordingName, std::vector<Marker> markers) {
    std::stable_sort(markers.begin(), markers.end(), [](const Marker& a, const Marker& b) { return a.seconds < b.seconds; });
    std::string out = "FrameWeave markers for ";
    out += cleanLabel(recordingName);
    out += "\r\n";
    for (const auto& marker : markers) {
        out += timeText(marker.seconds);
        const auto label = cleanLabel(marker.label);
        if (!label.empty()) { out += "  "; out += label; }
        out += "\r\n";
    }
    return out;
}

// "clip.mp4" -> "clip-markers.txt" (the extension is the last dot after the last separator; a name
// without one keeps all of it). Only the name: the folder is the recording's own.
inline std::string sidecarName(std::string_view recordingName) {
    const auto slash = recordingName.find_last_of("/\\");
    const auto start = slash == std::string_view::npos ? 0 : slash + 1;
    auto name = recordingName.substr(start);
    const auto dot = name.find_last_of('.');
    if (dot != std::string_view::npos && dot > 0) name = name.substr(0, dot);
    std::string out(name);
    out += "-markers.txt";
    return out;
}

// The markers of the file being written. A marker for another file starts that file's list: the
// earlier file's markers are already on disk.
//
// The book holds what is on disk and nothing else, so a marker is taken in two steps: `stage` says
// what the file's list would be with it and changes nothing, the caller writes that list to disk,
// and `commit` takes it over only once it is there. A marker whose file could not be written is
// then not in the book: it is not counted, the next one takes its number, and it does not turn up
// in a later save after the user was told it was not saved (which is what a single `add` before the
// write did until 4.1.0).
class Book {
public:
    struct Pending {
        std::string file;
        std::vector<Marker> markers;   // the whole list of `file` with the new marker at its end
        std::size_t number = 0;        // the new marker's number in its file (from 1); 0 = refused
        bool valid() const { return number > 0; }
    };
    // Refused (not valid) for: no file, a time that is not a number or is before the file's start,
    // or a file that already has kMaxMarkers. The book is not changed either way.
    Pending stage(std::string_view file, double seconds, std::string_view label) const {
        Pending pending;
        if (file.empty() || !std::isfinite(seconds) || seconds < 0) return pending;
        if (file == file_) pending.markers = markers_;
        if (pending.markers.size() >= kMaxMarkers) { pending.markers.clear(); return pending; }
        pending.file.assign(file);
        pending.markers.push_back({seconds, cleanLabel(label)});
        pending.number = pending.markers.size();
        return pending;
    }
    // Takes a staged list over. Call it only after the list was written.
    void commit(Pending&& pending) {
        if (!pending.valid()) return;
        file_ = std::move(pending.file);
        markers_ = std::move(pending.markers);
    }
    // Both steps at once, for a caller that writes nothing. Returns the marker's number in its file
    // (from 1), or 0 when it is refused.
    std::size_t add(std::string_view file, double seconds, std::string_view label) {
        auto pending = stage(file, seconds, label);
        const std::size_t number = pending.number;
        commit(std::move(pending));
        return number;
    }
    // How many markers `file` has (0 for any file but the one being written).
    std::size_t count(std::string_view file) const { return file == file_ ? markers_.size() : 0; }
    const std::string& file() const { return file_; }
    const std::vector<Marker>& markers() const { return markers_; }
    void clear() { file_.clear(); markers_.clear(); }

private:
    std::string file_;
    std::vector<Marker> markers_;
};
}
