#pragma once
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS: recordings are written as fragmented MP4 so a crash keeps what was
// written, but several editors (Sony/MAGIX Vegas Pro among them) refuse
// fragmented files or the 1 MHz track clock they use. When a recording
// finishes normally it is rewritten, without re-encoding, as a standard MP4:
// one index at the front (faststart) and a 90 kHz video clock. If anything
// fails the fragmented original is kept as it is, so nothing is ever lost.
#include <filesystem>
#include <string>

namespace veyra::engine {

struct Mp4FinalizeResult {
    bool ok = false;
    std::string error;          // why the original was kept (empty when ok)
    long long videoPackets = 0; // packets copied, for the log
    long long audioPackets = 0;
    bool noSpace = false;       // the disk has no room for the standard copy (the original was kept)
};

// The standard copy is written beside the recording before it replaces it, so the disk needs room for
// the recording once more, and a little (the index, the file system). Asked before the copy starts:
// a recording has no length limit, and without this the copy of a long one ran the disk full
// before it failed.
inline bool mp4FinalizeFits(unsigned long long fileBytes, unsigned long long freeBytes) {
    return freeBytes >= fileBytes + fileBytes / 50 + (64ull << 20);
}

Mp4FinalizeResult finalizeMp4(const std::filesystem::path& path);

} // namespace veyra::engine
