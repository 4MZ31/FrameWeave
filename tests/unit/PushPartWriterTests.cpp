// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// A recording in parts, cut without a gap: the files themselves (engine/PushPartWriter.h).
// Pictures and sound from FFmpeg's own MPEG-4 and AAC encoders go through the writer in the order
// the recorder's writer thread uses, and every part is read back:
//   - not cut: the file is byte for byte what the recorder wrote before parts existed;
//   - cut by time and by size: every picture and every sound packet is in exactly one part, in
//     order and unchanged; every part starts with a keyframe at its own time 0; no part passes
//     the size; sound that arrives late still lands in the part it belongs to;
//   - every part becomes a standard MP4 (Mp4Finalize.h) with the same packets.
// What decides where a cut falls is tested without files in PushRecordPartsTests.cpp.
#include "veyra/engine/Mp4Finalize.h"
#include "veyra/engine/PushPartWriter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

namespace fs = std::filesystem;
using veyra::engine::PushPartWriter;
namespace recordparts = veyra::engine::recordparts;

static int failures = 0, checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

namespace {
constexpr int kRate = 48000;
constexpr int kFps = 60;

struct Picture { std::vector<uint8_t> data; int64_t ptsUs = 0; bool key = false; };
struct Sound { std::vector<uint8_t> data; int64_t pts = 0; int64_t duration = 0; };

struct Material {
    std::vector<Picture> pictures;
    std::vector<Sound> sound;
    AVCodecParameters* video = nullptr;
    AVCodecParameters* audio = nullptr;
    double seconds = 0;
    ~Material() { avcodec_parameters_free(&video); avcodec_parameters_free(&audio); }
};

// `seconds` of pictures (a keyframe every `group` of them, uneven times like presented frames) and of sound.
bool encode(Material& out, int seconds, int group) {
    const int width = 320, height = 180, frames = seconds * kFps;
    const AVCodec* vcodec = avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    const AVCodec* acodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!vcodec || !acodec) { std::fprintf(stderr, "encoders missing\n"); return false; }
    AVCodecContext* v = avcodec_alloc_context3(vcodec);
    v->width = width; v->height = height; v->pix_fmt = AV_PIX_FMT_YUV420P;
    v->time_base = AVRational{1, 50000}; v->framerate = AVRational{kFps, 1};
    v->gop_size = group; v->max_b_frames = 0; v->bit_rate = 600000;
    v->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(v, vcodec, nullptr) < 0) return false;
    AVCodecContext* a = avcodec_alloc_context3(acodec);
    a->sample_rate = kRate; a->sample_fmt = AV_SAMPLE_FMT_FLTP; a->bit_rate = 128000;
    av_channel_layout_default(&a->ch_layout, 2);
    a->time_base = AVRational{1, kRate};
    a->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(a, acodec, nullptr) < 0) return false;
    out.video = avcodec_parameters_alloc();
    out.audio = avcodec_parameters_alloc();
    if (!out.video || !out.audio || avcodec_parameters_from_context(out.video, v) < 0 || avcodec_parameters_from_context(out.audio, a) < 0) return false;

    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    frame->format = v->pix_fmt; frame->width = width; frame->height = height;
    av_frame_get_buffer(frame, 0);
    auto drainVideo = [&] {
        while (avcodec_receive_packet(v, pkt) == 0) {
            Picture p;
            p.data.assign(pkt->data, pkt->data + pkt->size);
            p.ptsUs = pkt->pts * 20;                       // the encoder's clock is 1/50000
            p.key = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
            out.pictures.push_back(std::move(p));
            av_packet_unref(pkt);
        }
    };
    int64_t us = 0;
    for (int i = 0; i < frames; ++i) {
        av_frame_make_writable(frame);
        for (int y = 0; y < height; ++y) std::memset(frame->data[0] + y * frame->linesize[0], (i * 3 + y) & 255, size_t(width));
        for (int y = 0; y < height / 2; ++y) {
            std::memset(frame->data[1] + y * frame->linesize[1], (i / 2) & 255, size_t(width / 2));
            std::memset(frame->data[2] + y * frame->linesize[2], 128, size_t(width / 2));
        }
        frame->pts = us / 20;
        us += (i % 3 == 0) ? 16000 : 17000;                // uneven, like presented frames
        if (avcodec_send_frame(v, frame) < 0) return false;
        drainVideo();
    }
    avcodec_send_frame(v, nullptr);
    drainVideo();
    out.seconds = double(us) / 1e6;

    AVFrame* block = av_frame_alloc();
    block->format = a->sample_fmt; block->nb_samples = a->frame_size; block->sample_rate = kRate;
    av_channel_layout_copy(&block->ch_layout, &a->ch_layout);
    av_frame_get_buffer(block, 0);
    auto drainSound = [&] {
        while (avcodec_receive_packet(a, pkt) == 0) {
            Sound s;
            s.data.assign(pkt->data, pkt->data + pkt->size);
            s.pts = pkt->pts; s.duration = pkt->duration > 0 ? pkt->duration : a->frame_size;
            out.sound.push_back(std::move(s));
            av_packet_unref(pkt);
        }
    };
    for (int64_t at = 0; at < int64_t(out.seconds * kRate); at += a->frame_size) {
        av_frame_make_writable(block);
        for (int ch = 0; ch < 2; ++ch) {
            auto* d = reinterpret_cast<float*>(block->data[ch]);
            for (int n = 0; n < a->frame_size; ++n) d[n] = 0.2f * std::sin(float(at + n) * 0.05f);
        }
        block->pts = at;
        if (avcodec_send_frame(a, block) < 0) return false;
        drainSound();
    }
    avcodec_send_frame(a, nullptr);
    drainSound();
    av_frame_free(&frame); av_frame_free(&block); av_packet_free(&pkt);
    avcodec_free_context(&v); avcodec_free_context(&a);
    return !out.pictures.empty() && !out.sound.empty() && out.pictures.front().key;
}

AVPacket* packetOf(const std::vector<uint8_t>& data) {
    AVPacket* pkt = av_packet_alloc();
    if (!pkt || av_new_packet(pkt, int(data.size())) < 0) { av_packet_free(&pkt); return nullptr; }
    std::memcpy(pkt->data, data.data(), data.size());
    return pkt;
}

// The recorder as it wrote one file before parts existed: the same calls, in the same order.
struct Before {
    AVFormatContext* mux = nullptr;
    AVStream* videoStream = nullptr;
    AVStream* soundStream = nullptr;
    int64_t lastVideoDts = INT64_MIN;
    bool open(const Material& m, const fs::path& path) {
        if (avformat_alloc_output_context2(&mux, nullptr, "mp4", nullptr) < 0 || !mux) return false;
        videoStream = avformat_new_stream(mux, nullptr);
        videoStream->time_base = AVRational{1, 1000000};
        videoStream->avg_frame_rate = AVRational{kFps, 1};
        if (avcodec_parameters_copy(videoStream->codecpar, m.video) < 0) return false;
        soundStream = avformat_new_stream(mux, nullptr);
        if (avcodec_parameters_copy(soundStream->codecpar, m.audio) < 0) return false;
        soundStream->time_base = AVRational{1, kRate};
        av_dict_set(&soundStream->metadata, "handler_name", "Veyra sound", 0);
        if (avio_open(&mux->pb, path.string().c_str(), AVIO_FLAG_WRITE) < 0) return false;
        AVDictionary* options = nullptr;
        av_dict_set(&options, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
        const int rc = avformat_write_header(mux, &options);
        av_dict_free(&options);
        return rc >= 0;
    }
    bool video(const Picture& p, int64_t durationUs) {
        AVPacket* out = packetOf(p.data);
        if (!out) return false;
        out->stream_index = videoStream->index; out->pts = out->dts = p.ptsUs; out->duration = std::max<int64_t>(1, durationUs);
        if (p.key) out->flags |= AV_PKT_FLAG_KEY;
        av_packet_rescale_ts(out, AVRational{1, 1000000}, videoStream->time_base);
        if (out->dts <= lastVideoDts) out->pts = out->dts = lastVideoDts + 1;
        lastVideoDts = out->dts;
        const int rc = av_interleaved_write_frame(mux, out);
        av_packet_free(&out);
        return rc >= 0;
    }
    bool sound(const Sound& s) {
        AVPacket* packet = packetOf(s.data);
        if (!packet) return false;
        packet->pts = packet->dts = s.pts; packet->duration = s.duration;
        packet->stream_index = soundStream->index;
        av_packet_rescale_ts(packet, AVRational{1, kRate}, soundStream->time_base);
        const int rc = av_interleaved_write_frame(mux, packet);
        av_packet_free(&packet);
        return rc >= 0;
    }
    bool finish() {
        const bool ok = av_write_trailer(mux) >= 0;
        avio_closep(&mux->pb);
        avformat_free_context(mux);
        mux = nullptr;
        return ok;
    }
};

struct Run {
    std::vector<PushPartWriter::Closed> closed;
    uint64_t lost = 0;
    unsigned parts = 0;
    bool ok = false;
};

// The writer thread's order: a picture is written when the next one says how long it lasts; sound
// goes in up to the picture, and up to the cut before a file changes. lagUs: how long after its
// own time a sound packet reaches the writer (a microphone is late).
template <class VideoFn, class SoundFn, class DueFn, class NextFn>
bool feed(const Material& m, int64_t lagUs, bool limitToVideo, VideoFn&& video, SoundFn&& sound, DueFn&& due, NextFn&& next) {
    size_t at = 0;
    const auto soundUpTo = [&](int64_t videoUs, int64_t nowUs) {
        const int64_t limit = recordparts::samplesAt(videoUs, kRate);
        while (at < m.sound.size()) {
            const Sound& s = m.sound[at];
            const int64_t ready = (s.pts + s.duration) * 1000000 / kRate + lagUs;
            if (ready > nowUs || (limitToVideo && s.pts + s.duration > limit)) break;
            if (!sound(s)) return false;
            ++at;
        }
        return true;
    };
    const Picture* held = nullptr;
    for (const auto& p : m.pictures) {
        const int64_t now = p.ptsUs + 20000;
        if (held) {
            if (!video(*held, p.ptsUs - held->ptsUs)) return false;
            if (due(p.ptsUs, p.key)) {
                if (!soundUpTo(p.ptsUs, now)) return false;
                if (!next(p.ptsUs)) return false;
            }
        }
        held = &p;
        if (!soundUpTo(held->ptsUs, now)) return false;
    }
    if (held && !video(*held, 1000000 / kFps)) return false;
    for (; at < m.sound.size(); ++at) if (!sound(m.sound[at])) return false;
    return true;
}

Run record(const Material& m, const fs::path& dir, const std::string& name, recordparts::Limits limits, int64_t lagUs) {
    Run run;
    PushPartWriter writer;
    PushPartWriter::Config config;
    config.video = m.video; config.fps = kFps; config.audioRate = kRate;
    config.sound.push_back({m.audio, "Veyra sound"});
    config.limits = limits;
    config.nextPath = [&](unsigned number) { return (dir / (name + "-" + std::to_string(number) + ".mp4")).wstring(); };
    config.closed = [&](const PushPartWriter::Closed& c) { run.closed.push_back(c); };
    if (!writer.open(std::move(config), (dir / (name + ".mp4")).wstring())) { std::fprintf(stderr, "open failed\n"); return run; }
    const bool fed = feed(m, lagUs, limits.any(),
        [&](const Picture& p, int64_t d) { return writer.video(p.data.data(), p.data.size(), p.ptsUs, d, p.key); },
        [&](const Sound& s) {
            AVPacket* packet = packetOf(s.data);
            if (!packet) return false;
            packet->pts = packet->dts = s.pts; packet->duration = s.duration;
            const bool ok = writer.sound(0, packet);
            av_packet_free(&packet);
            return ok;
        },
        [&](int64_t pts, bool key) { return writer.due(pts, key); },
        [&](int64_t pts) { return writer.next(pts); });
    run.parts = writer.parts();
    run.lost = writer.lostSound();
    run.ok = writer.finish() && fed;
    return run;
}

struct ReadBack {
    std::vector<std::vector<uint8_t>> video, sound;
    std::vector<double> videoSeconds, soundSeconds;   // each packet's time in the file
    std::vector<bool> videoKey;
    bool ok = false;
};
ReadBack read(const fs::path& path) {
    ReadBack out;
    AVFormatContext* in = nullptr;
    if (avformat_open_input(&in, path.string().c_str(), nullptr, nullptr) < 0) return out;
    if (avformat_find_stream_info(in, nullptr) < 0) { avformat_close_input(&in); return out; }
    AVPacket* pkt = av_packet_alloc();
    while (av_read_frame(in, pkt) >= 0) {
        const AVStream* s = in->streams[pkt->stream_index];
        const double at = double(pkt->pts) * av_q2d(s->time_base);
        if (s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            out.video.emplace_back(pkt->data, pkt->data + pkt->size);
            out.videoSeconds.push_back(at);
            out.videoKey.push_back((pkt->flags & AV_PKT_FLAG_KEY) != 0);
        } else if (s->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            out.sound.emplace_back(pkt->data, pkt->data + pkt->size);
            out.soundSeconds.push_back(at);
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&in);
    out.ok = true;
    return out;
}

std::vector<uint8_t> bytesOf(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}
bool hasBox(const fs::path& path, const char* name) {
    std::ifstream file(path, std::ios::binary);
    for (;;) {
        unsigned char header[16];
        if (!file.read(reinterpret_cast<char*>(header), 8)) return false;
        uint64_t size = (uint64_t(header[0]) << 24) | (uint64_t(header[1]) << 16) | (uint64_t(header[2]) << 8) | header[3];
        if (std::memcmp(header + 4, name, 4) == 0) return true;
        uint64_t headerSize = 8;
        if (size == 1) {
            if (!file.read(reinterpret_cast<char*>(header + 8), 8)) return false;
            size = 0; for (int i = 8; i < 16; ++i) size = (size << 8) | header[i];
            headerSize = 16;
        }
        if (size < headerSize) return false;
        file.seekg(std::streamoff(size - headerSize), std::ios::cur);
    }
}

// Every picture and every sound packet of `m` is in exactly one part, in order and unchanged;
// every part starts with a keyframe at its own time 0; sound lies in the part that holds its start.
void whole(const Material& m, const Run& run, const char* what) {
    std::printf("%s: parts=%u closed=%zu lost=%llu ok=%d\n", what, run.parts, run.closed.size(), (unsigned long long)run.lost, run.ok);
    CHECK(run.ok);
    CHECK(run.closed.size() == run.parts);
    CHECK(run.lost == 0);
    size_t v = 0, a = 0;
    bool pictures = true, sounds = true, starts = true, placed = true, numbered = true;
    for (size_t k = 0; k < run.closed.size(); ++k) {
        const auto& part = run.closed[k];
        if (part.number != k + 1 || !part.ok || part.removed || !part.frames) numbered = false;
        const ReadBack back = read(fs::path(part.path));
        if (!back.ok || back.video.empty()) { pictures = false; continue; }
        if (back.video.size() != part.frames) pictures = false;
        if (!back.videoKey.front() || std::fabs(back.videoSeconds.front()) > 0.0002) starts = false;
        const double origin = double(part.originUs) / 1e6;
        const double nextOrigin = k + 1 < run.closed.size() ? double(run.closed[k + 1].originUs) / 1e6 : 1e18;
        for (size_t i = 0; i < back.video.size(); ++i, ++v) {
            if (v >= m.pictures.size() || back.video[i] != m.pictures[v].data) { pictures = false; continue; }
            // Its time in the file is its own. The first part is written as one file always was: the
            // sound's lead-in (one packet, 21.3 ms, before 0) moves everything after the first picture
            // by that much, sound and picture alike. A later part starts clean.
            if (std::fabs(back.videoSeconds[i] + origin - double(m.pictures[v].ptsUs) / 1e6) > (k ? 0.0002 : 0.022)) pictures = false;
        }
        for (size_t i = 0; i < back.sound.size(); ++i, ++a) {
            if (a >= m.sound.size() || back.sound[i] != m.sound[a].data) { sounds = false; continue; }
            const double start = double(m.sound[a].pts) / kRate;
            // Its start lies in this part (the lead-in before 0 belongs to the first).
            if ((k && start < origin - 1e-6) || start >= nextOrigin + 1e-6) placed = false;
            // And its time in the file is its own: in the first part moved by the lead-in like the
            // picture (above), in a later part where the file says its sound starts, which it
            // records to the millisecond.
            if (std::fabs(back.soundSeconds[i] + origin - start) > (k ? 0.0015 : 0.022)) placed = false;
            // Sound and picture keep their distance in every part: what one is moved by, the other is.
            if (!k && i == 5 && back.video.size() > 5 &&
                std::fabs((back.soundSeconds[i] - start) - (back.videoSeconds[5] - double(m.pictures[v - back.video.size() + 5].ptsUs) / 1e6)) > 0.0002) placed = false;
        }
    }
    CHECK(pictures);
    CHECK(v == m.pictures.size());
    CHECK(sounds);
    CHECK(a == m.sound.size());
    CHECK(starts);
    CHECK(placed);
    CHECK(numbered);
}
}  // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "pushfps-part-writer-test";
    std::error_code code;
    fs::remove_all(dir, code);
    fs::create_directories(dir);

    Material m;
    CHECK(encode(m, 12, 30));
    std::printf("material: pictures=%zu sound=%zu seconds=%.3f\n", m.pictures.size(), m.sound.size(), m.seconds);
    if (failures) return 1;

    // --- not cut: byte for byte the file the recorder wrote before parts existed ----------------
    {
        Before before;
        CHECK(before.open(m, dir / "before.mp4"));
        const bool fed = feed(m, 5000, false,
            [&](const Picture& p, int64_t d) { return before.video(p, d); },
            [&](const Sound& s) { return before.sound(s); },
            [](int64_t, bool) { return false; },
            [](int64_t) { return true; });
        CHECK(fed);
        CHECK(before.finish());
        const Run run = record(m, dir, "one", recordparts::Limits{}, 5000);
        whole(m, run, "one file");
        CHECK(run.parts == 1);
        const auto a = bytesOf(dir / "before.mp4"), b = bytesOf(dir / "one.mp4");
        std::printf("one file: %zu bytes, before parts existed: %zu bytes\n", b.size(), a.size());
        CHECK(!a.empty() && a == b);
    }
    // --- cut by time ------------------------------------------------------------------------------
    for (const int64_t lagUs : {int64_t(0), int64_t(5000), int64_t(60000), int64_t(250000)}) {
        recordparts::Limits limits; limits.afterUs = 3 * 1000000LL;
        const std::string name = "time-" + std::to_string(lagUs);
        const Run run = record(m, dir, name, limits, lagUs);
        whole(m, run, name.c_str());
        // Keyframes every half second: parts of three seconds, the last one shorter.
        CHECK(run.parts == 4 || run.parts == 5);
        bool lengths = true;
        for (size_t k = 0; k + 1 < run.closed.size(); ++k) {
            const int64_t length = run.closed[k + 1].originUs - run.closed[k].originUs;
            if (length < limits.afterUs - 8334 || length > limits.afterUs + 600000) lengths = false;
        }
        CHECK(lengths);
        // Each part becomes a standard MP4 with the same packets.
        for (const auto& part : run.closed) {
            const auto finished = veyra::engine::finalizeMp4(fs::path(part.path));
            CHECK(finished.ok);
            CHECK(uint64_t(finished.videoPackets) == part.frames);
            CHECK(!hasBox(fs::path(part.path), "moof") && hasBox(fs::path(part.path), "moov"));
        }
        whole(m, run, (name + " as standard MP4").c_str());
    }
    // --- cut by size: no part passes it -----------------------------------------------------------
    {
        // Sizes follow from what the encoders produced: the limits are shares of it, and how large a
        // group of pictures (with its sound) is decides what may be asked of the rule.
        uint64_t total = 0, group = 0, largestGroup = 0, runningLargest = 0;
        bool tame = true;   // no group more than a quarter larger than every one before it
        size_t soundAt = 0;
        for (size_t i = 0; i < m.pictures.size(); ++i) {
            if (m.pictures[i].key && i) {
                if (runningLargest && group > runningLargest + runningLargest / 4) tame = false;
                runningLargest = std::max(runningLargest, group);
                largestGroup = std::max(largestGroup, group);
                group = 0;
            }
            group += m.pictures[i].data.size();
            total += m.pictures[i].data.size();
            const int64_t until = i + 1 < m.pictures.size() ? recordparts::samplesAt(m.pictures[i + 1].ptsUs, kRate) : INT64_MAX;
            for (; soundAt < m.sound.size() && m.sound[soundAt].pts + m.sound[soundAt].duration <= until; ++soundAt) {
                group += m.sound[soundAt].data.size();
                total += m.sound[soundAt].data.size();
            }
        }
        largestGroup = std::max(largestGroup, group);
        std::printf("sizes: total=%llu largestGroup=%llu tame=%d\n", (unsigned long long)total, (unsigned long long)largestGroup, tame);
        for (const uint64_t share : {uint64_t(3), uint64_t(5)}) {
            recordparts::Limits limits; limits.afterBytes = total / share;
            const std::string name = "size-" + std::to_string(share);
            const Run run = record(m, dir, name, limits, 5000);
            whole(m, run, name.c_str());
            CHECK(run.parts >= 2);
            bool sizeKnown = true, bounded = true, under = true;
            for (const auto& part : run.closed) {
                const uint64_t onDisk = fs::file_size(fs::path(part.path));
                std::printf("  part %u: %llu bytes, %llu pictures (limit %llu)\n", part.number, (unsigned long long)part.bytes, (unsigned long long)part.frames, (unsigned long long)limits.afterBytes);
                if (part.bytes != onDisk) sizeKnown = false;                                  // what the writer reports is the file
                if (onDisk > limits.afterBytes + largestGroup + (16u << 10)) bounded = false;  // whatever the pictures do
                if (onDisk > limits.afterBytes) under = false;
            }
            CHECK(sizeKnown);
            CHECK(bounded);
            if (tame) CHECK(under);
            else std::printf("  (a group grew by more than a quarter: the size is a bound of one group here, not a limit)\n");
            for (const auto& part : run.closed) {
                CHECK(veyra::engine::finalizeMp4(fs::path(part.path)).ok);
                std::printf("  part %u as standard MP4: %llu bytes\n", part.number, (unsigned long long)fs::file_size(fs::path(part.path)));
            }
            whole(m, run, (name + " as standard MP4").c_str());
        }
    }
    // --- sound later than a whole part is counted, never written into another part ---------------
    {
        recordparts::Limits limits; limits.afterUs = 1000000;
        // 1.6 s late with parts of one second: the first 0.4 s of each part's sound still finds its
        // file open (the part before the one being written), the rest comes after it was closed.
        const Run run = record(m, dir, "late", limits, 1600000);
        std::printf("late sound: parts=%u lost=%llu\n", run.parts, (unsigned long long)run.lost);
        CHECK(run.ok);
        CHECK(run.lost > 0);
        size_t pictures = 0, sound = 0;
        bool placed = true;
        for (size_t k = 0; k < run.closed.size(); ++k) {
            const ReadBack back = read(fs::path(run.closed[k].path));
            pictures += back.video.size();
            sound += back.sound.size();
            for (const double at : back.soundSeconds) if (k && at < -0.0015) placed = false;   // nothing from before the part's start
        }
        CHECK(pictures == m.pictures.size());       // the picture is whole whatever the sound does
        CHECK(sound + run.lost == m.sound.size());
        CHECK(placed);
    }
    // --- a writer that was never opened, and one opened without a picture -------------------------
    {
        PushPartWriter writer;
        CHECK(!writer.opened());
        CHECK(!writer.due(0, true));
        CHECK(!writer.next(0));
        CHECK(writer.finish());
        PushPartWriter::Config config;
        CHECK(!writer.open(std::move(config), (dir / "nothing.mp4").wstring()));
        CHECK(!writer.error().empty());
        CHECK(!fs::exists(dir / "nothing.mp4"));
    }
    // A recording that ends before any picture leaves no file.
    {
        std::vector<PushPartWriter::Closed> closed;
        PushPartWriter writer;
        PushPartWriter::Config config;
        config.video = m.video; config.fps = kFps; config.audioRate = kRate;
        config.sound.push_back({m.audio, "Veyra sound"});
        config.closed = [&](const PushPartWriter::Closed& c) { closed.push_back(c); };
        CHECK(writer.open(std::move(config), (dir / "empty.mp4").wstring()));
        CHECK(writer.finish());
        CHECK(closed.size() == 1 && closed[0].removed && closed[0].frames == 0);
        CHECK(!fs::exists(dir / "empty.mp4"));
    }

    if (!std::getenv("PUSHFPS_KEEP_TEST_FILES")) fs::remove_all(dir, code);
    std::printf("push_part_writer_tests: %d checks, %d failed\n", checks, failures);
    return failures ? 1 : 0;
}
