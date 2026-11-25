// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "Types.h"
#include "Timeline.h"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <array>
#include <memory>
#include <mutex>
#include <stop_token>
namespace veyra::remoteplay {
struct QueueLimits {
    std::size_t maxFrames=4, maxBytes=8u*1024u*1024u, maxConfigBytes=64u*1024u;
    HostTime maxAge100ns=1000000; // 100 ms development safety ceiling, not target latency.
    void validate() const;
};
enum class PushStatus {Accepted,ConfigStored,WaitingForIdr,Malformed,WrongGeneration,Closed};
// PushFPS: why the queue was emptied and a keyframe asked for. One cause is counted for each
// recovery, the one that started it, so the causes add up to `recoveries`.
enum class RecoveryCause : std::uint8_t {
    Age,        // a queued frame waited longer than the limit (the reader or the decoder stalled)
    Depth,      // the frame limit was reached
    Bytes,      // the byte limit was reached
    Oversized,  // one access unit larger than the whole queue
    StaleSample,// a sample already too old when it was handed in
    Gap,        // the stream's own frame numbers skipped
    Lost,       // the stream reported lost frames or a recovered reference
    Malformed,  // a sample that is not what it says it is
    Config,     // the stream's configuration changed
    Mismatch,   // an access unit that does not fit the configuration (or came before one)
    Decoder,    // the decoder asked for a keyframe
    Count
};
constexpr const char* recoveryCauseName(RecoveryCause c) {
    constexpr const char* names[]={"age","depth","bytes","oversized","stale","gap","lost","malformed","config","mismatch","decoder"};
    return std::size_t(c)<std::size_t(RecoveryCause::Count)?names[std::size_t(c)]:"?";
}
struct VideoQueueStats {
    std::uint64_t accessUnits=0,configMessages=0,accepted=0,consumed=0,dropped=0,
        rejected=0,recoveries=0,idrRequests=0;
    std::size_t depth=0,bytes=0,highWaterDepth=0,highWaterBytes=0;
    bool waitingForIdr=true;std::uint64_t epoch=1;
    // PushFPS: accepted == consumed + droppedQueued + depth, at any moment (`dropped` also counts
    // access units refused at the door, which were never accepted).
    std::uint64_t droppedQueued=0;
    std::array<std::uint64_t,std::size_t(RecoveryCause::Count)> causes{};
    std::uint64_t cause(RecoveryCause c) const {return causes[std::size_t(c)];}
    // The longest a frame that was handed out had waited, and how often the reader's clock sample
    // was older than the frame it then took (harmless since 4.0; it used to empty the queue).
    HostTime highWaterAge100ns=0;std::uint64_t readerClockBehind=0;
};
struct QueuedVideo {
    VideoSample sample;
    // Full configuration snapshot must be fed before this AU when non-null.
    // The caller drains EAGAIN then retries; do not drop it as "not a picture".
    std::shared_ptr<const VideoSample> configBefore;
    std::uint64_t epoch=1;
    bool resetDecoder=false;
};
class VideoIngress {
public:
    explicit VideoIngress(QueueLimits limits={});
    void begin(Generation);
    PushStatus push(VideoSample,HostTime now);
    std::optional<QueuedVideo> tryPop(HostTime now);
    bool waitForData(std::chrono::milliseconds timeout);
    // Poll from the protocol OWNER, never request an IDR while holding a C callback's lock.
    bool takeIdrRequest(HostTime now);
    void requireKeyframe(); // Decoder reports fatal error / lost references.
    void close();
    VideoQueueStats stats() const;
private:
    void recoverLocked(RecoveryCause);
    void updateStatsLocked();
    bool expiredLocked(HostTime now) const;
    QueueLimits limits_;mutable std::mutex mutex_;std::condition_variable cv_;
    std::deque<QueuedVideo> queue_;std::shared_ptr<const VideoSample> config_;
    Generation generation_=0;std::size_t payloadBytes_=0;bool closed_=true,waiting_=true,pendingIdr_=false;
    HostTime lastIdr_=-1;Sequence16Extender sequence_;VideoQueueStats stats_{};
};
struct AudioQueueStats {std::uint64_t accepted=0,consumed=0,dropped=0,rejected=0;double bufferedMs=0;};
class AudioIngress {
public:
    explicit AudioIngress(std::uint32_t maxBufferedMs=100);
    void begin(Generation);
    bool push(PcmBlock);
    std::optional<PcmBlock> tryPop();
    bool waitForData(std::stop_token stop);
    void close();
    AudioQueueStats stats() const;
private:
    mutable std::mutex mutex_;
    std::condition_variable_any ready_;
    std::deque<PcmBlock> queue_;
    Generation generation_=0;std::uint32_t rate_=0,channels_=0,maxMs_;std::size_t frames_=0;
    std::uint64_t nextSample_=0;bool closed_=true,gap_=false;AudioQueueStats stats_{};
};
} // namespace veyra::remoteplay
