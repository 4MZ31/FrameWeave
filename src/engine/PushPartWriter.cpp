// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#include "veyra/engine/PushPartWriter.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
}

namespace veyra::engine {
namespace {

std::string utf8Path(const std::wstring& path) {
    const auto text = std::filesystem::path(path).u8string();
    return std::string(text.begin(), text.end());
}
std::wstring avErrorText(int code) {
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof(text));
    std::wstring out;
    for (const char* c = text; *c; ++c) out.push_back(wchar_t(static_cast<unsigned char>(*c)));
    return out;
}

}  // namespace

PushPartWriter::PushPartWriter() = default;
PushPartWriter::~PushPartWriter() { release(); }

void PushPartWriter::freeFile(File& file) {
    if (file.mux) {
        if (file.mux->pb) avio_closep(&file.mux->pb);
        avformat_free_context(file.mux);
    }
    file = File{};
}

void PushPartWriter::release() {
    freeFile(current_);
    freeFile(previous_);
    opened_ = false;
}

bool PushPartWriter::open(Config config, const std::wstring& path) {
    release();
    error_.clear();
    videoWritten_ = 0;
    config_ = std::move(config);
    if (!config_.video) { error_ = L"无法写入录制文件头"; return false; }
    plan_ = recordparts::Plan(config_.limits, config_.fps, config_.audioRate);
    if (!openFile(current_, path, 1, 0)) { freeFile(current_); return false; }
    opened_ = true;
    return true;
}

bool PushPartWriter::openFile(File& file, const std::wstring& path, unsigned number, int64_t originUs) {
    file = File{};
    file.path = path;
    file.number = number;
    file.originUs = originUs;
    if (avformat_alloc_output_context2(&file.mux, nullptr, config_.matroska ? "matroska" : "mp4", nullptr) < 0 || !file.mux) {
        file.mux = nullptr;
        error_ = L"无法创建录制封装器";
        return false;
    }
    AVStream* video = avformat_new_stream(file.mux, nullptr);
    if (!video) { error_ = L"无法创建录制视频流"; return false; }
    video->time_base = AVRational{1, 1000000};
    video->avg_frame_rate = AVRational{int(config_.fps), 1};
    if (avcodec_parameters_copy(video->codecpar, config_.video) < 0) { error_ = L"无法写入录制文件头"; return false; }
    for (const auto& track : config_.sound) {
        AVStream* stream = avformat_new_stream(file.mux, nullptr);
        if (!stream || !track.parameters || avcodec_parameters_copy(stream->codecpar, track.parameters) < 0) {
            error_ = L"无法创建录制音频流";
            return false;
        }
        stream->time_base = AVRational{1, config_.audioRate};
        av_dict_set(&stream->metadata, "handler_name", track.name.c_str(), 0);
    }
    int rc = avio_open(&file.mux->pb, utf8Path(path).c_str(), AVIO_FLAG_WRITE);
    if (rc < 0) { error_ = L"无法创建录制文件：" + avErrorText(rc); return false; }
    // Fragmented MP4: every fragment is playable on its own, so an interrupted recording keeps
    // what was written. MKV is readable up to the last cluster anyway. The first part is written
    // the way the one file of a recording always was. A later part's picture starts at its own
    // time 0 but its sound up to one packet (21 ms) after it: with the index written before any
    // packet (empty_moov) the writer would move that sound to 0 and it would run ahead of the
    // picture, so a later part's index waits for its first fragment (delay_moov), which records
    // where each track starts. The engine release PushFPS is built on does the same.
    AVDictionary* options = nullptr;
    if (!config_.matroska)
        av_dict_set(&options, "movflags", originUs ? "frag_keyframe+delay_moov+default_base_moof" : "frag_keyframe+empty_moov+default_base_moof", 0);
    rc = avformat_write_header(file.mux, &options);
    av_dict_free(&options);
    if (rc < 0) { error_ = L"无法写入录制文件头：" + avErrorText(rc); return false; }
    file.headerWritten = true;
    return true;
}

bool PushPartWriter::closeFile(File& file) {
    if (!file.mux) return true;
    Closed closed;
    closed.path = file.path;
    closed.number = file.number;
    closed.frames = file.frames;
    closed.originUs = file.originUs;
    closed.ok = !file.headerWritten || av_write_trailer(file.mux) >= 0;
    closed.bytes = file.mux->pb ? uint64_t(std::max<int64_t>(0, avio_tell(file.mux->pb))) : 0;
    freeFile(file);
    if (!closed.frames) {
        // Nothing was recorded into it: no file is left behind.
        std::error_code code;
        std::filesystem::remove(std::filesystem::path(closed.path), code);
        closed.removed = true;
    }
    if (config_.closed) config_.closed(closed);
    return closed.ok;
}

bool PushPartWriter::next(int64_t ptsUs) {
    if (!opened_) return false;
    // The part two cuts back: whatever sound was on its way has arrived long since.
    const bool olderOk = closeFile(previous_);
    plan_.closePrevious();
    if (!olderOk) { error_ = L"录制文件收尾失败"; return false; }
    previous_ = std::exchange(current_, File{});
    plan_.startNext(ptsUs);
    const std::wstring path = config_.nextPath ? config_.nextPath(plan_.parts()) : std::wstring{};
    if (path.empty()) { error_ = L"无法创建录制文件：" + avErrorText(AVERROR(EINVAL)); return false; }
    if (!openFile(current_, path, plan_.parts(), ptsUs)) { freeFile(current_); return false; }
    return true;
}

bool PushPartWriter::video(const uint8_t* data, size_t size, int64_t ptsUs, int64_t durationUs, bool key) {
    if (!opened_ || !current_.mux) return false;
    AVPacket* out = av_packet_alloc();
    if (!out || av_new_packet(out, int(size)) < 0) { av_packet_free(&out); return false; }
    std::memcpy(out->data, data, size);
    const AVStream* stream = current_.mux->streams[0];
    out->stream_index = stream->index;
    out->pts = out->dts = ptsUs - current_.originUs;
    out->duration = std::max<int64_t>(1, durationUs);
    if (key) out->flags |= AV_PKT_FLAG_KEY;
    av_packet_rescale_ts(out, AVRational{1, 1000000}, stream->time_base);
    if (out->dts <= current_.lastDts) out->pts = out->dts = current_.lastDts + 1;
    current_.lastDts = out->dts;
    const int rc = av_interleaved_write_frame(current_.mux, out);
    av_packet_free(&out);
    if (rc < 0) return false;
    ++current_.frames;
    ++videoWritten_;
    plan_.wroteVideo(size, key);
    if (current_.mux->pb) plan_.fileSize(uint64_t(std::max<int64_t>(0, avio_tell(current_.mux->pb))));
    return true;
}

bool PushPartWriter::sound(unsigned track, AVPacket* packet) {
    if (!opened_ || !packet || track >= config_.sound.size()) return false;
    const auto where = packet->pts == AV_NOPTS_VALUE ? recordparts::SoundGoes::Current : plan_.soundGoes(packet->pts);
    const auto bytes = uint64_t(std::max(packet->size, 0));
    plan_.wroteSound(bytes, where);
    // For a part whose file is closed: counted (lostSound), never written into another part.
    if (where == recordparts::SoundGoes::Lost) return true;
    File& file = where == recordparts::SoundGoes::Previous ? previous_ : current_;
    if (!file.mux) return false;
    const int64_t origin = plan_.originSamples(where);
    const AVStream* stream = file.mux->streams[track + 1];
    packet->stream_index = stream->index;
    if (packet->pts != AV_NOPTS_VALUE) packet->pts -= origin;
    if (packet->dts != AV_NOPTS_VALUE) packet->dts -= origin;
    av_packet_rescale_ts(packet, AVRational{1, config_.audioRate}, stream->time_base);
    return av_interleaved_write_frame(file.mux, packet) >= 0;
}

bool PushPartWriter::finish() {
    if (!opened_) return true;
    const bool olderOk = closeFile(previous_);
    plan_.closePrevious();
    const bool currentOk = closeFile(current_);
    opened_ = false;
    return olderOk && currentOk;
}

}  // namespace veyra::engine
