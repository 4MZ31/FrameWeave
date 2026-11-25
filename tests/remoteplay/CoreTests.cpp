// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/remoteplay/Types.h"
#include "veyra/remoteplay/Timeline.h"
#include "veyra/remoteplay/VideoIngress.h"
#include "veyra/remoteplay/ControllerQueue.h"
#include "veyra/remoteplay/LatestMailbox.h"
#include "veyra/remoteplay/PacketPump.h"
#include "veyra/remoteplay/InputGate.h"
#include "veyra/remoteplay/SessionInbox.h"
#include "veyra/remoteplay/StreamRecovery.h"
#include "veyra/engine/PresentationScheduler.h"
#include "veyra/engine/FgRecoveryBudget.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#include <utility>
using namespace veyra::remoteplay;
namespace {
using Test=std::pair<const char*,void(*)()>;
std::vector<Test>& tests(){static std::vector<Test> value;return value;}
struct AddTest{AddTest(const char* n,void(*f)()){tests().emplace_back(n,f);}};
#define TEST(name) void name(); AddTest reg_##name(#name,name); void name()
#define CHECK(expr) do { if(!(expr))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #expr); } while(false)
#define THROWS(expr) do{bool caught=false;try{(void)(expr);}catch(const std::exception&){caught=true;}CHECK(caught);}while(false)
std::vector<std::uint8_t> configBytes(){return {0,0,0,1,0x67,0x42,0,0x1f,0,0,1,0x68,0xee,0x3c};}
VideoSample config(Generation g=1,HostTime t=0){
    VideoSample s;s.generation=g;s.kind=SampleKind::CodecConfig;s.width=1920;s.height=1080;
    s.payload=PaddedBytes::copy(configBytes());s.arrival100ns=t;return s;
}
VideoSample frame(std::uint16_t index,bool idr,HostTime t=0,Generation g=1){
    VideoSample s;s.generation=g;s.wireFrameIndex=index;s.width=1920;s.height=1080;s.arrival100ns=t;
    const std::array<std::uint8_t,6> bytes{0,0,1,static_cast<std::uint8_t>(idr?0x65:0x41),0x88,0x99};
    s.payload=PaddedBytes::copy(bytes);return s;
}
PcmBlock audio(std::uint64_t index=0,std::size_t count=480,Generation g=1){
    PcmBlock b;b.generation=g;b.firstSample=index;b.channels=2;b.rate=48000;b.samples.resize(count*2,42);return b;
}
void start(VideoIngress& q){q.begin(1);CHECK(q.push(config(),0)==PushStatus::ConfigStored);CHECK(q.push(frame(0,true),0)==PushStatus::Accepted);}

TEST(account_base64_exact){AccountId id{1,0,0,0,0,0,0,0};CHECK(accountIdToBase64(id)=="AQAAAAAAAAA=");CHECK(accountIdFromBase64("AQAAAAAAAAA=")==id);}
TEST(account_decimal_endian){auto id=accountIdFromDecimal("72623859790382856");CHECK(id);CHECK((*id==AccountId{8,7,6,5,4,3,2,1}));}
TEST(account_uint64_boundary){auto id=accountIdFromDecimal("18446744073709551615");CHECK(id);for(auto b:*id)CHECK(b==255);CHECK(!accountIdFromDecimal("18446744073709551616"));}
TEST(account_decimal_strict){for(auto s:{"","-1","+1"," 1","1 ","1a","1.0","184467440737095516150"})CHECK(!accountIdFromDecimal(s));CHECK(accountIdFromDecimal("0"));}
TEST(account_base64_strict){for(auto s:{"","AQAAAAAAAAA","AQAAAAAAAAA==","AQAAAAAAAAA=\n","AQAAAAAAAAB=","AQAAAAAAAA-=","AQAAAA=AAAA="})CHECK(!accountIdFromBase64(s));}
TEST(account_roundtrip_property){std::mt19937 rng(2731);for(int i=0;i<10000;++i){AccountId a{};for(auto& b:a)b=static_cast<std::uint8_t>(rng());CHECK(accountIdFromBase64(accountIdToBase64(a))==a);}}
TEST(pairing_pin_leading_zero){CHECK(parsePairingPin("00123456")==123456u);CHECK(parsePairingPin("00000000")==0u);}
TEST(pairing_pin_reject){for(auto s:{"1234567","123456789","12 45678","-1234567","1234567a"})CHECK(!parsePairingPin(s));}
TEST(host_validation){CHECK(validHost("192.168.1.2"));CHECK(validHost("ps5.local"));CHECK(validHost("console-1"));for(auto s:{"","256.1.1.1","192.168.001.2","https://ps5","user@host","host:9295","foo/bar","-foo","foo-","a..b","a.","127.1","a\nb"})CHECK(!validHost(s));}
TEST(profile_id_validation){CHECK(validProfileId("0123456789abcdef0123456789abcdef"));CHECK(!validProfileId("../../credentials"));CHECK(!validProfileId("0123456789ABCDEF0123456789ABCDEF"));}
TEST(video_profile_validation){VideoProfile p;CHECK(!p.validate());p.width=3840;CHECK(p.validate());p={};p.fps=120;CHECK(p.validate());p={};p.bitrateKbps=999;CHECK(p.validate());p={};p.codec=static_cast<Codec>(99);CHECK(p.validate());}
TEST(padding_ownership){std::vector<std::uint8_t> b{1,2,3};auto p=PaddedBytes::copy(b);b[0]=99;CHECK(p.bytes()[0]==1);CHECK(p.size()==3);CHECK(p.paddingIsZero());}
TEST(padding_reject_oversize){std::vector<std::uint8_t> b(8u*1024u*1024u+1);THROWS(PaddedBytes::copy(b));}
TEST(nal_h264_classification){const auto c=inspectAnnexB(configBytes(),Codec::H264);CHECK(c.valid&&c.hasConfig&&!c.hasPicture&&!c.idr);auto f=frame(1,true);const auto i=inspectAnnexB(f.payload.bytes(),Codec::H264);CHECK(i.valid&&i.hasPicture&&i.idr);}
TEST(nal_h265_idr_not_cra){for(auto type:{19u,20u,21u}){std::array<std::uint8_t,6>b{0,0,1,static_cast<std::uint8_t>(type<<1),1,99};auto n=inspectAnnexB(b,Codec::H265);CHECK(n.valid&&n.hasPicture);CHECK(n.idr==(type!=21));}}
TEST(nal_reject_malformed){for(const auto& b:std::vector<std::vector<std::uint8_t>>{{1,2,3},{0,0,1},{0,0,1,0xff},{0,0,1,0}})CHECK(!inspectAnnexB(b,Codec::H264).valid);std::array<std::uint8_t,5>b{0,0,1,38,0};CHECK(!inspectAnnexB(b,Codec::H265).valid);}
TEST(nal_random_input_bounds){std::mt19937 rng(11);for(int j=0;j<20000;++j){std::vector<std::uint8_t>b(rng()%128);for(auto& v:b)v=static_cast<std::uint8_t>(rng());(void)inspectAnnexB(b,Codec::H264);(void)inspectAnnexB(b,Codec::H265);}}
TEST(sequence_wrap){Sequence16Extender x;CHECK(x.observe(65534).first);CHECK(x.observe(65535).value==65535);CHECK(x.observe(0).value==65536);CHECK(x.observe(1).value==65537);}
TEST(sequence_duplicates_and_old){Sequence16Extender x;CHECK(x.observe(1).accepted);CHECK(!x.observe(1).accepted);CHECK(!x.observe(0).accepted);CHECK(!x.observe(32769).accepted);CHECK(x.observe(4).gap==2);}
TEST(sequence_multiple_wraps){Sequence16Extender x;for(std::uint64_t i=0;i<200000;++i){auto r=x.observe(static_cast<std::uint16_t>(i));CHECK(r.accepted&&r.value==i);}}
TEST(clock_common_origin){RemotePlayClock c;c.reset(1000000,60);auto v=c.video(123,1300000);CHECK(v&&v->pts100ns==300000);CHECK(c.audioPts(480,48000,1100000)==200000);CHECK(v->provenance==TimestampProvenance::LocalEstimated);}
TEST(clock_cadence_no_cumulative_rounding){RemotePlayClock c;c.reset(0,60);for(std::uint64_t i=0;i<60000;++i){const auto time=static_cast<std::int64_t>(i*10000000/60);auto s=c.video(i,time);CHECK(s&&s->pts100ns==time);}}
TEST(clock_gap_reanchors){RemotePlayClock c;c.reset(0,60);CHECK(c.video(0,0));auto s=c.video(60,10000000);CHECK(s&&s->discontinuity&&s->pts100ns==10000000);}
TEST(clock_long_session_drift_and_fg){
    for(unsigned fps:{30u,60u})for(int ppm:{-1400,1400}){
        RemotePlayClock c;c.reset(0,fps);
        veyra::engine::PresentationScheduler schedule;
        veyra::engine::FgRecoveryBudget budget;
        const int64_t interval=10000000/fps;
        schedule.reset(1,0,0,interval);
        int64_t previous=-1;unsigned admitted=0,measured=0;
        for(uint64_t i=0;i<fps*600u;++i){
            const auto base=int64_t(i*uint64_t(1000000+ppm)*10/fps);
            const auto jitter=i%97==11?20000:int64_t(i%7)*1000;
            const auto arrival=base+jitter;
            const auto stamp=c.video(i,arrival);
            CHECK(stamp&&!stamp->discontinuity&&stamp->pts100ns>previous);previous=stamp->pts100ns;
            budget.fgCost(2,arrival);budget.complete(12,true,false,arrival);
            const bool admittedNow=budget.admit(arrival+20000,schedule.deadline(stamp->pts100ns-interval/2),2,.5);
            if(i>fps*10u){CHECK(std::abs(stamp->pts100ns-base)<150000);++measured;admitted+=admittedNow;}
        }
        CHECK(admitted>measured*98/100);
    }
}
// PushFPS 4.5.3: a PS5 whose frame index runs slower than its profile (a log: about 29.85 a second on 30) for
// minutes; here 1 % slow (29.7). At the old 0.5 % slew the estimate fell 1.5 s behind arrival in five minutes; now it stays within
// a frame and a half, never steps back, and a steady stream still moves at most 0.5 % a frame.
TEST(clock_follows_an_index_slower_than_the_profile){
    RemotePlayClock c;c.reset(0,30);
    const int64_t interval=10000000/30;
    int64_t previous=-1,worst=0;
    for(uint64_t i=0;i<30u*300u;++i){
        const auto arrival=int64_t(double(i)*10000000.0/29.7)+int64_t(i%5)*2000;
        const auto stamp=c.video(i,arrival);
        CHECK(stamp&&!stamp->discontinuity&&stamp->pts100ns>previous);
        if(stamp&&previous>=0)CHECK(stamp->pts100ns-previous<=interval+interval/20+1);
        if(stamp)previous=stamp->pts100ns;
        if(stamp&&i>30u*10u)worst=std::max<int64_t>(worst,std::llabs(arrival-stamp->pts100ns));
    }
    CHECK(worst<interval*3/2);
}
TEST(clock_reject_invalid){RemotePlayClock c;CHECK(!c.video(0,0));THROWS(c.reset(0,120));THROWS(c.reset(-1,60));c.reset(10,60);CHECK(!c.video(0,9));CHECK(c.video(1,20));CHECK(!c.video(1,20));CHECK(!c.video(2,19));CHECK(!c.audioPts(0,0,10));CHECK(!c.audioPts(std::numeric_limits<std::uint64_t>::max(),8000,10));}
TEST(queue_limits_validation){QueueLimits l;l.maxFrames=0;THROWS(VideoIngress{l});l={};l.maxConfigBytes=l.maxBytes+1;THROWS(VideoIngress{l});l={};l.maxAge100ns=0;THROWS(VideoIngress{l});}
TEST(config_not_counted_as_picture){VideoIngress q;q.begin(1);CHECK(q.push(config(),0)==PushStatus::ConfigStored);CHECK(q.stats().accessUnits==0);CHECK(q.stats().depth==0);CHECK(!q.tryPop(0));}
TEST(queue_starts_at_keyframe){VideoIngress q;q.begin(1);q.push(config(),0);CHECK(q.push(frame(1,false),0)==PushStatus::WaitingForIdr);CHECK(q.push(frame(2,true),0)==PushStatus::Accepted);auto v=q.tryPop(0);CHECK(v&&v->configBefore&&v->resetDecoder);CHECK(q.stats().accepted==1);}
TEST(queue_fifo_not_latest_compressed){VideoIngress q;start(q);q.push(frame(1,false),0);q.push(frame(2,false),0);for(std::uint16_t i=0;i<3;++i){auto s=q.tryPop(0);CHECK(s&&s->sample.wireFrameIndex==i);CHECK(static_cast<bool>(s->configBefore)==(i==0));}}
TEST(queue_overflow_requires_idr){QueueLimits l;l.maxFrames=2;VideoIngress q(l);start(q);q.push(frame(1,false),0);CHECK(q.push(frame(2,false),0)==PushStatus::WaitingForIdr);CHECK(q.stats().depth==0);CHECK(q.stats().waitingForIdr);CHECK(q.push(frame(3,false),0)==PushStatus::WaitingForIdr);CHECK(q.push(frame(4,true),0)==PushStatus::Accepted);auto s=q.tryPop(0);CHECK(s&&s->resetDecoder&&s->configBefore);CHECK(s->epoch==2);}
TEST(queue_overflow_incoming_idr){QueueLimits l;l.maxFrames=1;VideoIngress q(l);start(q);CHECK(q.push(frame(1,true),0)==PushStatus::Accepted);auto s=q.tryPop(0);CHECK(s&&s->sample.wireFrameIndex==1&&s->resetDecoder);}
TEST(queue_age_recovery){VideoIngress q;start(q);CHECK(!q.tryPop(1000001));CHECK(q.stats().waitingForIdr&&q.stats().depth==0);}
// PushFPS 4.0. The reader samples its clock, and only then waits for the lock; the stream's callback
// can stamp and queue a frame in between. That frame's stamp is later than the reader's clock: it is
// new. Until 4.0 the queue called it expired, threw it away and waited for the next keyframe.
TEST(queue_frame_stamped_after_the_readers_clock_is_new){
    VideoIngress q;q.begin(1);CHECK(q.push(config(1,5000),5000)==PushStatus::ConfigStored);
    CHECK(q.push(frame(0,true,5000),5000)==PushStatus::Accepted);
    auto s=q.tryPop(4999);CHECK(s&&s->sample.wireFrameIndex==0&&s->resetDecoder);
    auto st=q.stats();CHECK(st.recoveries==0&&!st.waitingForIdr&&st.epoch==1&&st.dropped==0&&st.readerClockBehind==1);
    CHECK(!q.takeIdrRequest(6000));
    // The same with frames behind it: all of them come out, in order, with one stale clock sample.
    for(std::uint16_t i=1;i<=3;++i)CHECK(q.push(frame(i,false,6000+i),6000+i)==PushStatus::Accepted);
    for(std::uint16_t i=1;i<=3;++i){auto f=q.tryPop(5990);CHECK(f&&f->sample.wireFrameIndex==i&&!f->resetDecoder);}
    st=q.stats();CHECK(st.recoveries==0&&st.consumed==4&&st.dropped==0&&st.depth==0&&!st.waitingForIdr);
    // An equal stamp is age zero too.
    CHECK(q.push(frame(4,false,7000),7000)==PushStatus::Accepted);CHECK(q.tryPop(7000));CHECK(q.stats().recoveries==0);
}
TEST(queue_a_frame_that_really_waited_too_long_still_expires){
    QueueLimits l;VideoIngress q(l);q.begin(1);q.push(config(1,100),100);
    // Exactly the limit is still in time; one tick over is not.
    CHECK(q.push(frame(0,true,100),100)==PushStatus::Accepted);
    auto s=q.tryPop(100+l.maxAge100ns);CHECK(s&&s->sample.wireFrameIndex==0);CHECK(q.stats().highWaterAge100ns==l.maxAge100ns);
    CHECK(q.push(frame(1,false,200),200)==PushStatus::Accepted);
    CHECK(!q.tryPop(200+l.maxAge100ns+1));
    auto st=q.stats();CHECK(st.recoveries==1&&st.waitingForIdr&&st.depth==0&&st.cause(RecoveryCause::Age)==1&&st.droppedQueued==1);
    // The next frame's arrival finds a stale one in front: the same cause, from the other side.
    VideoIngress p(l);p.begin(1);p.push(config(1,100),100);CHECK(p.push(frame(0,true,100),100)==PushStatus::Accepted);
    const HostTime late=100+l.maxAge100ns+1;
    CHECK(p.push(frame(1,false,late),late)==PushStatus::WaitingForIdr);
    st=p.stats();CHECK(st.recoveries==1&&st.cause(RecoveryCause::Age)==1&&st.depth==0&&st.waitingForIdr);
    // And a stale reader clock never hides a frame that is old by the clock of the one who queues.
    VideoIngress r(l);r.begin(1);r.push(config(1,100),100);CHECK(r.push(frame(0,true,100),100)==PushStatus::Accepted);
    CHECK(r.tryPop(50));CHECK(r.stats().recoveries==0);   // new for this reader: handed out, not kept
    CHECK(r.stats().depth==0);
}
TEST(queue_a_sample_stamped_after_its_own_clock_is_refused){
    // What makes the rule above safe: nothing gets in with a stamp from the future.
    VideoIngress q;start(q);CHECK(q.push(frame(1,false,1000),999)==PushStatus::Malformed);
    auto st=q.stats();CHECK(st.rejected==1&&st.recoveries==1&&st.cause(RecoveryCause::Malformed)==1&&st.waitingForIdr);
}
TEST(queue_recoveries_are_counted_by_what_started_them){
    const auto sum=[](const VideoQueueStats& s){std::uint64_t n=0;for(auto c:s.causes)n+=c;return n;};
    const auto whole=[&](const VideoQueueStats& s){return s.accepted==s.consumed+s.droppedQueued+s.depth&&sum(s)==s.recoveries;};
    {QueueLimits l;l.maxFrames=2;VideoIngress q(l);start(q);q.push(frame(1,false),0);CHECK(q.push(frame(2,false),0)==PushStatus::WaitingForIdr);
     auto s=q.stats();CHECK(s.cause(RecoveryCause::Depth)==1&&s.recoveries==1&&s.droppedQueued==2&&whole(s));
     // While it waits for a keyframe nothing more is counted: one recovery, one cause.
     CHECK(q.push(frame(3,false),0)==PushStatus::WaitingForIdr);q.requireKeyframe();s=q.stats();CHECK(s.recoveries==1&&sum(s)==1&&whole(s));
     CHECK(q.push(frame(4,true),0)==PushStatus::Accepted);q.requireKeyframe();s=q.stats();
     CHECK(s.recoveries==2&&s.cause(RecoveryCause::Decoder)==1&&s.cause(RecoveryCause::Depth)==1&&whole(s));}
    {VideoIngress q;start(q);CHECK(q.push(frame(5,false),0)==PushStatus::WaitingForIdr);auto s=q.stats();CHECK(s.cause(RecoveryCause::Gap)==1&&whole(s));}
    {VideoIngress q;start(q);auto f=frame(1,false);f.framesLost=2;q.push(std::move(f),0);auto s=q.stats();CHECK(s.cause(RecoveryCause::Lost)==1&&whole(s));}
    {VideoIngress q;start(q);auto c=config();c.width=1280;c.height=720;q.push(std::move(c),0);auto s=q.stats();CHECK(s.cause(RecoveryCause::Config)==1&&whole(s));}
    {VideoIngress q;start(q);auto f=frame(1,false);f.width=1280;f.height=720;CHECK(q.push(std::move(f),0)==PushStatus::WaitingForIdr);auto s=q.stats();CHECK(s.cause(RecoveryCause::Mismatch)==1&&whole(s));}
    {VideoIngress q;start(q);CHECK(q.push(frame(1,false,0),2000000)==PushStatus::WaitingForIdr);auto s=q.stats();CHECK(s.cause(RecoveryCause::StaleSample)==1&&whole(s));}
    {QueueLimits l;l.maxBytes=1024;l.maxConfigBytes=64;VideoIngress q(l);start(q);std::vector<std::uint8_t>b(600,0x88);b[0]=0;b[1]=0;b[2]=1;b[3]=0x41;
     auto f=frame(1,false);f.payload=PaddedBytes::copy(b);CHECK(q.push(std::move(f),0)==PushStatus::Accepted);
     auto g=frame(2,false);g.payload=PaddedBytes::copy(b);CHECK(q.push(std::move(g),0)==PushStatus::WaitingForIdr);
     auto s=q.stats();CHECK(s.cause(RecoveryCause::Bytes)==1&&whole(s));
     std::vector<std::uint8_t>big(2000,0x88);big[0]=0;big[1]=0;big[2]=1;big[3]=0x65;auto h=frame(3,true);h.payload=PaddedBytes::copy(big);
     CHECK(q.push(std::move(h),0)==PushStatus::WaitingForIdr);CHECK(whole(q.stats()));}
    // A new session starts from nothing.
    {VideoIngress q;start(q);q.requireKeyframe();q.begin(2);auto s=q.stats();CHECK(s.recoveries==0&&sum(s)==0&&s.droppedQueued==0&&s.readerClockBehind==0&&s.highWaterAge100ns==0);}
    for(std::size_t i=0;i<std::size_t(RecoveryCause::Count);++i)CHECK(std::string(recoveryCauseName(RecoveryCause(i)))!="?");
}
TEST(queue_idr_coalescing){VideoIngress q;q.begin(1);q.push(config(),0);q.push(frame(0,false),0);CHECK(q.takeIdrRequest(0));q.push(frame(1,false),0);CHECK(!q.takeIdrRequest(100000));CHECK(q.takeIdrRequest(5000000));CHECK(!q.takeIdrRequest(10000000));}
TEST(queue_config_change_clears_history){VideoIngress q;start(q);auto c=config();c.width=1280;c.height=720;q.push(std::move(c),0);CHECK(q.stats().depth==0&&q.stats().epoch==2);auto f=frame(1,true);f.width=1280;f.height=720;CHECK(q.push(std::move(f),0)==PushStatus::Accepted);CHECK(q.tryPop(0)->configBefore->width==1280);}
TEST(queue_identical_config_no_reset){VideoIngress q;start(q);q.push(config(),0);CHECK(q.stats().depth==1&&q.stats().epoch==1);}
TEST(queue_wrong_generation){VideoIngress q;start(q);CHECK(q.push(frame(1,false,0,2),0)==PushStatus::WrongGeneration);CHECK(q.stats().depth==1);}
TEST(queue_reference_recovery_not_keyframe){VideoIngress q;start(q);q.tryPop(0);auto f=frame(1,false);f.referenceRecovered=true;CHECK(q.push(std::move(f),0)==PushStatus::WaitingForIdr);CHECK(q.stats().waitingForIdr);}
TEST(queue_frame_gap_resets){VideoIngress q;start(q);q.tryPop(0);CHECK(q.push(frame(3,false),0)==PushStatus::WaitingForIdr);CHECK(q.stats().epoch==2);}
TEST(queue_reject_injected_config_picture){VideoIngress q;q.begin(1);auto f=frame(0,true);f.kind=SampleKind::CodecConfig;CHECK(q.push(std::move(f),0)==PushStatus::Malformed);}
TEST(queue_close_unblocks){VideoIngress q;q.begin(1);std::atomic<bool> result{true};std::thread t([&]{result=q.waitForData(std::chrono::seconds(2));});q.close();t.join();CHECK(!result);CHECK(q.push(config(),0)==PushStatus::Closed);}
TEST(queue_bytes_bound){QueueLimits l;l.maxBytes=1024;l.maxConfigBytes=64;VideoIngress q(l);start(q);std::vector<std::uint8_t>b(1000,0x88);b[0]=0;b[1]=0;b[2]=1;b[3]=0x41;auto f=frame(1,false);f.payload=PaddedBytes::copy(b);q.push(std::move(f),0);CHECK(q.stats().bytes<=1024);CHECK(q.stats().highWaterBytes<=1024);}
TEST(queue_malformed_time){VideoIngress q;start(q);CHECK(q.push(frame(1,false,10),9)==PushStatus::Malformed);CHECK(q.stats().waitingForIdr);}
TEST(queue_concurrent_bound){VideoIngress q;q.begin(1);q.push(config(),0);std::atomic<bool> done=false,ok=true;std::thread producer([&]{for(std::uint16_t i=0;i<20000;++i){q.push(frame(i,i%10==0),0);auto s=q.stats();if(s.depth>4||s.bytes>8u*1024u*1024u)ok=false;}done=true;});std::thread consumer([&]{while(!done){(void)q.tryPop(0);std::this_thread::yield();}});producer.join();consumer.join();CHECK(ok);}
TEST(audio_sample_count_per_channel){auto b=audio();CHECK(b.valid());CHECK(b.frames()==480);CHECK(b.samples.size()*sizeof(std::int16_t)==1920);}
TEST(audio_overflow_discontinuous){AudioIngress q(20);q.begin(1);q.push(audio(0));q.push(audio(480));q.push(audio(960));CHECK(q.stats().dropped==1);CHECK(q.stats().bufferedMs==20);auto b=q.tryPop();CHECK(b&&b->firstSample==480&&b->discontinuity);}
TEST(audio_out_of_order){AudioIngress q;q.begin(1);CHECK(q.push(audio(480)));CHECK(!q.push(audio(0)));CHECK(q.stats().rejected==1);}
TEST(audio_reject_odd_channels){AudioIngress q;q.begin(1);auto b=audio();b.samples.pop_back();CHECK(!q.push(std::move(b)));CHECK(!q.push(audio(0,10000)));}
TEST(audio_format_change){AudioIngress q;q.begin(1);q.push(audio());auto b=audio();b.rate=24000;b.channels=1;b.samples.resize(240);CHECK(q.push(std::move(b)));auto a=q.tryPop();CHECK(a&&a->rate==24000&&a->channels==1&&a->discontinuity);}
TEST(audio_close_and_generation){AudioIngress q;q.begin(1);CHECK(!q.push(audio(0,480,2)));q.close();CHECK(!q.push(audio()));CHECK(!q.tryPop());}
TEST(mailbox_latest_owned){LatestMailbox<std::shared_ptr<int>> m;m.begin(1);auto a=std::make_shared<int>(1);std::weak_ptr<int>w=a;m.publish({1,1,a});a.reset();m.publish({1,2,std::make_shared<int>(2)});CHECK(w.expired());auto s=m.take();CHECK(s&&*s->value==2);CHECK(m.overwritten()==1);CHECK(!m.take());}
TEST(mailbox_generation_and_order){LatestMailbox<int> m;m.begin(1);CHECK(!m.publish({2,1,1}));CHECK(m.publish({1,2,2}));CHECK(!m.publish({1,1,1}));CHECK(!m.publish({1,2,22}));m.close();CHECK(!m.publish({1,3,3}));CHECK(!m.take());}
struct FakeCodec {std::vector<SendStatus> sends;std::vector<ReceiveStatus> receives;std::vector<int> seen;std::size_t si=0,ri=0;SendStatus send(const int& n){seen.push_back(n);CHECK(si<sends.size());return sends[si++];}ReceiveStatus receive(){CHECK(ri<receives.size());return receives[ri++];}};
TEST(pump_retries_same_packet){PacketPump<int> p;FakeCodec c{{SendStatus::NeedDrain,SendStatus::Accepted},{ReceiveStatus::Frame,ReceiveStatus::NeedInput,ReceiveStatus::Frame,ReceiveStatus::NeedInput},{},0,0};CHECK(p.submit(42));CHECK(p.step(c)==PumpStatus::Idle);CHECK((c.seen==std::vector<int>{42,42}));CHECK(p.stats().accepted==1&&p.stats().frames==2&&p.stats().eagain==1);}
TEST(pump_double_eagain_rejected){PacketPump<int> p;FakeCodec c{{SendStatus::NeedDrain},{ReceiveStatus::NeedInput},{},0,0};p.submit(7);CHECK(p.step(c)==PumpStatus::ProtocolViolation);CHECK(p.stats().accepted==0&&p.hasPending());}
TEST(pump_budget_retains_pending){PacketPump<int> p;FakeCodec c{{SendStatus::NeedDrain,SendStatus::Accepted},{ReceiveStatus::Frame,ReceiveStatus::NeedInput,ReceiveStatus::NeedInput},{},0,0};p.submit(9);CHECK(p.step(c,1)==PumpStatus::Yield&&p.hasPending());CHECK(!p.submit(10));CHECK(p.step(c)==PumpStatus::Idle);CHECK(c.seen.back()==9);}
TEST(pump_error_not_accepted){PacketPump<int> p;FakeCodec c{{SendStatus::Error},{},{},0,0};p.submit(1);CHECK(p.step(c)==PumpStatus::Error);CHECK(p.stats().accepted==0);CHECK(!p.submit(2));p.reset();CHECK(p.submit(2));}
TEST(pump_drain_all_frames){PacketPump<int> p;FakeCodec c{{SendStatus::Accepted},{ReceiveStatus::Frame,ReceiveStatus::Frame,ReceiveStatus::Frame,ReceiveStatus::NeedInput},{},0,0};p.submit(1);CHECK(p.step(c)==PumpStatus::Idle);CHECK(p.stats().frames==3&&p.drained());}
TEST(pump_end_not_success){PacketPump<int> p;FakeCodec c{{SendStatus::End},{},{},0,0};p.submit(1);CHECK(p.step(c)==PumpStatus::End&&p.stats().accepted==0);CHECK(!p.submit(2));}
TEST(input_focus_release){InputGate g;g.begin(1);ControllerState s;s.buttons=ControllerState::Cross;CHECK(!g.update(1,s,10));g.setFocused(true);CHECK(g.update(1,s,10));CHECK(g.sample(1,11)==s);g.setFocused(false);CHECK(g.sample(1,12)==ControllerState{});}
TEST(input_stale_and_new_session){InputGate g;g.begin(1);g.setFocused(true);ControllerState s;s.l2=255;g.update(1,s,10);CHECK(g.sample(1,1000011)==ControllerState{});CHECK(g.sample(2,11)==ControllerState{});g.begin(2);g.setFocused(true);CHECK(!g.update(1,s,20));CHECK(g.sample(2,20)==ControllerState{});}
TEST(inbox_connected_is_not_streaming){SessionInbox q;auto t=q.begin(monotonic100ns());CHECK(q.snapshot().state==SessionState::Connecting);t.connected();CHECK(q.snapshot().state==SessionState::WaitingFirstFrame);CHECK(q.decodedFrameReady(t.generation()));CHECK(q.snapshot().state==SessionState::Streaming);}
TEST(inbox_reject_stale_callbacks){SessionInbox q;auto a=q.begin(monotonic100ns());q.invalidate();q.finishStop();auto b=q.begin(monotonic100ns());a.connected();CHECK(q.snapshot().state==SessionState::Connecting);CHECK(!a.video(config(a.generation(),monotonic100ns())));b.connected();CHECK(q.snapshot().state==SessionState::WaitingFirstFrame);CHECK(q.snapshot().staleCallbacks>=2);}
TEST(inbox_weak_token_after_destruction){std::optional<SessionInbox::Token> t;{SessionInbox q;t=q.begin(monotonic100ns());}CHECK(!t->video(config(t->generation(),monotonic100ns())));t->connected();t->failed(99);}
TEST(inbox_failure_closes_media){SessionInbox q;auto t=q.begin(monotonic100ns());CHECK(t.video(config(t.generation(),monotonic100ns())));t.failed(32);CHECK(q.snapshot().state==SessionState::Failed);CHECK(!q.isCurrent(t.generation()));CHECK(!q.tryVideo(monotonic100ns()));CHECK(q.snapshot().errorCode==32);}
TEST(inbox_begin_and_stop_guards){SessionInbox q;THROWS(q.begin(-1));auto t=q.begin(monotonic100ns());THROWS(q.begin(monotonic100ns()));THROWS(q.finishStop());q.invalidate();THROWS(q.begin(monotonic100ns()));q.finishStop();CHECK(q.begin(monotonic100ns()).generation()>t.generation());}
TEST(inbox_callbacks_race_invalidate){SessionInbox q;auto t=q.begin(monotonic100ns());std::atomic<bool> finish=false;std::thread worker([&]{while(!finish){t.connected();(void)t.audio(audio(0,480,t.generation()));}});q.invalidate();finish=true;worker.join();CHECK(!q.isCurrent(t.generation()));CHECK(!q.tryAudio());q.finishStop();CHECK(q.snapshot().state==SessionState::Idle);}
TEST(inbox_failed_requires_backend_join){SessionInbox q;auto t=q.begin(monotonic100ns());t.failed(42);THROWS(q.begin(monotonic100ns()));q.invalidate();q.finishStop();CHECK(q.begin(monotonic100ns()).generation()>t.generation());}
TEST(inbox_frame_stamped_after_the_readers_clock_is_new){
    // Through the session's boundary with the real clock, in the order the product has it: the
    // reader's clock sample, then the callback stamps and queues, then the reader gets the lock.
    SessionInbox q;auto t=q.begin(monotonic100ns());
    CHECK(t.video(config(t.generation(),monotonic100ns())));
    for(std::uint16_t i=0;i<200;++i){
        const auto readerNow=monotonic100ns()-1;
        CHECK(t.video(frame(i,i==0,monotonic100ns(),t.generation())));
        auto v=q.tryVideo(readerNow);CHECK(v&&v->sample.wireFrameIndex==i);
    }
    const auto s=q.snapshot().video;CHECK(s.recoveries==0&&!s.waitingForIdr&&s.consumed==200&&s.dropped==0&&s.epoch==1);
}
// PushFPS 4.0: controller states on their way to the console (remoteplay/ControllerQueue.h).
namespace pad {
using S=ControllerState;
constexpr HostTime ms=10000;
S held(std::uint32_t buttons=0,std::uint8_t r2=0,std::int16_t rightX=0){S s;s.inputActive=true;s.buttons=buttons;s.r2=r2;s.rightX=rightX;return s;}
}
TEST(controller_a_slow_owner_never_gets_a_release_or_an_old_stick){
    // The interface reads the pad 125 times a second; the owner, behind a decoder backlog, sends
    // 62.5 times. A held trigger with the stick moving: five seconds of it.
    ControllerQueue q;unsigned neutral=0,old=0;
    for(int tick=0;tick<625;++tick){
        const HostTime now=1000*pad::ms+tick*8*pad::ms;
        const auto state=pad::held(0,255,std::int16_t(tick));CHECK(q.push(state,now));
        if(tick%2==0){const auto out=q.take(now);neutral+=!out.inputActive||out.r2!=255;old+=out.rightX!=state.rightX;}
    }
    CHECK(neutral==0&&old==0);CHECK(q.stats().highWaterPending==1&&q.stats().expired==0&&q.stats().overflows==0&&q.stats().merged>300);
    // A stalled owner (300 ms) while a button is held: the state it finds is the newest, and it is still held.
    ControllerQueue stall;HostTime t=5000*pad::ms;
    for(int i=0;i<38;++i,t+=8*pad::ms)CHECK(stall.push(pad::held(pad::S::Cross,0,std::int16_t(i)),t));
    const auto out=stall.take(t);CHECK(out.inputActive&&out.buttons==pad::S::Cross&&out.rightX==37);CHECK(stall.stats().expired==0);
}
TEST(controller_an_old_state_is_dropped_alone_and_what_is_fresh_still_arrives_in_order){
    using namespace pad;
    // Cross pressed and too old; behind it Cross let go with Circle pressed, then Circle let go.
    ControllerQueue q;const HostTime t0=2000*ms;
    CHECK(q.push(held(S::Cross),t0));CHECK(q.push(held(S::Circle),t0+60*ms));CHECK(q.push(held(0),t0+110*ms));
    CHECK(q.pending()==3);
    const auto a=q.take(t0+115*ms);CHECK(a.inputActive&&a.buttons==S::Circle);   // until 4.0: a neutral state here
    const auto b=q.take(t0+116*ms);CHECK(b.inputActive&&b.buttons==0);
    CHECK(q.stats().expired==1&&q.pending()==0);
    // With nothing waiting, the state sent last stands (the console keeps it).
    CHECK(q.take(t0+117*ms)==b);
    // Exactly 100 ms is still in time; one tick more is not.
    ControllerQueue edge;CHECK(edge.push(held(S::Square),t0));CHECK(edge.push(held(0),t0+100*ms));
    CHECK(edge.take(t0+100*ms).buttons==S::Square);
    ControllerQueue over;CHECK(over.push(held(S::Square),t0));CHECK(over.push(held(0),t0+100*ms+1));
    CHECK(over.take(t0+100*ms+1).buttons==0&&over.stats().expired==1);
    // A clock sample from before the state was stamped is no age at all.
    ControllerQueue behind;CHECK(behind.push(held(S::Up),t0));CHECK(behind.take(t0-5).buttons==S::Up);
}
TEST(controller_neutral_only_when_nothing_true_can_be_sent){
    using namespace pad;const HostTime t0=3000*ms;
    // The interface stopped reporting: released, whatever was waiting.
    ControllerQueue silent;CHECK(silent.push(held(S::Cross,200),t0));
    CHECK(silent.take(t0+100*ms+1)==S{});CHECK(silent.stats().silences==1&&silent.pending()==0);
    CHECK(silent.take(t0+300*ms)==S{});CHECK(silent.stats().silences==1);   // counted once, not every time round
    // Input switched off (focus or device lost) cancels what waited, and coming back resumes the held state.
    ControllerQueue focus;CHECK(focus.push(held(S::Cross),t0));CHECK(focus.push(held(S::Cross|S::Circle),t0+8*ms));CHECK(focus.push({},t0+16*ms));
    CHECK(focus.pending()==1);CHECK(focus.take(t0+16*ms)==S{});
    CHECK(focus.push(held(0,255),t0+24*ms));const auto back=focus.take(t0+24*ms);CHECK(back.inputActive&&back.r2==255);
    // More edges than the queue holds: released first, then the latest, and never more than the bound.
    ControllerQueue mash;bool fitted=true;
    for(int i=0;i<40;++i)fitted=mash.push(held(i%2?S::Cross:0),t0+i*ms)&&fitted;
    CHECK(!fitted&&mash.stats().overflows>=1&&mash.pending()<=ControllerQueue::kMaxPending&&mash.stats().highWaterPending<=ControllerQueue::kMaxPending);
    // A new session starts with nothing waiting and nothing held.
    mash.clear();CHECK(mash.pending()==0);CHECK(mash.push(held(S::L1),t0+50*ms));mash.clear();CHECK(mash.take(t0+50*ms)==S{});
}
TEST(controller_presses_releases_and_trigger_pulls_keep_their_order){
    using namespace pad;const HostTime t0=4000*ms;
    // A quick full pull of the trigger while the owner is busy: 0, 80, 255, 120, 0. It arrives with its peak.
    ControllerQueue pull;int i=0;
    for(std::uint8_t travel:{0,80,255,120,0}){CHECK(pull.push(held(0,travel),t0+i*8*ms));++i;}
    std::vector<int> seen;while(pull.pending())seen.push_back(pull.take(t0+40*ms).r2);
    CHECK(seen==std::vector<int>({0,80,255,120,0}));
    // Held and wobbling inside one part of its travel it is a level: one state waits, the newest.
    ControllerQueue wobble;for(int k=0;k<50;++k)CHECK(wobble.push(held(0,std::uint8_t(200+k%7)),t0+k*8*ms));
    CHECK(wobble.pending()==1&&wobble.take(t0+392*ms).r2==200+49%7);
    CHECK(ControllerQueue::triggerPart(0)==0&&ControllerQueue::triggerPart(1)==1&&ControllerQueue::triggerPart(85)==1&&ControllerQueue::triggerPart(86)==2&&
          ControllerQueue::triggerPart(170)==2&&ControllerQueue::triggerPart(171)==3&&ControllerQueue::triggerPart(255)==3);
    // A tap of the PS button (the latency test presses it) and two buttons in quick turns.
    ControllerQueue taps;const std::uint32_t order[]={0,S::PS,0,S::Cross,S::Cross|S::Square,S::Square,0};
    i=0;for(auto b:order){CHECK(taps.push(held(b,0,std::int16_t(i)),t0+i*4*ms));++i;}
    for(auto b:order)CHECK(taps.take(t0+30*ms).buttons==b);
    // Touch: a finger down, sliding, up, and another finger with the number of the first.
    ControllerQueue touch;S none=held(),down=held(),slide=held(),second=held();
    down.touches[0]={0,100,100};slide.touches[0]={0,900,400};second.touches[0]={1,50,50};
    CHECK(touch.push(none,t0));CHECK(touch.push(down,t0+8*ms));CHECK(touch.push(slide,t0+16*ms));CHECK(touch.push(none,t0+24*ms));CHECK(touch.push(second,t0+32*ms));
    CHECK(touch.pending()==4);
    CHECK(touch.take(t0+32*ms).touches[0].id==-1);const auto moved=touch.take(t0+32*ms);CHECK(moved.touches[0].id==0&&moved.touches[0].x==900);
    CHECK(touch.take(t0+32*ms).touches[0].id==-1);CHECK(touch.take(t0+32*ms).touches[0].id==1);
    // Two fingers: the second one coming down is an edge of its own.
    ControllerQueue two;S both=down;both.touches[1]={1,300,300};
    CHECK(two.push(down,t0));CHECK(two.push(both,t0+8*ms));CHECK(two.pending()==2);
}
TEST(controller_motion_samples_survive_being_replaced){
    using namespace pad;const HostTime t0=6000*ms;ControllerQueue q;
    for(unsigned i=1;i<=20;++i){auto s=held(0,255);s.motionValid=true;s.motionSampleCount=1;s.motionTimestampUs=i;s.motionSamples[0].timestampUs=i;s.motionSamples[0].gyro={float(i),0,0};CHECK(q.push(s,t0+i*ms));}
    CHECK(q.pending()==1);const auto out=q.take(t0+20*ms);
    CHECK(out.motionSampleCount==16&&out.motionSamples[0].timestampUs==5&&out.motionSamples[15].timestampUs==20&&out.motionSamples[15].gyro[0]==20.0f&&out.motionTimestampUs==20);
    // Samples already sent are not sent again, and a sample that is not newer is left out.
    auto next=held(0,255);next.motionValid=true;next.motionSampleCount=2;next.motionSamples[0].timestampUs=21;next.motionSamples[1].timestampUs=21;
    CHECK(q.push(next,t0+21*ms));auto again=next;again.motionSamples[0].timestampUs=22;again.motionSampleCount=1;CHECK(q.push(again,t0+22*ms));
    const auto merged=q.take(t0+22*ms);CHECK(merged.motionSampleCount==2&&merged.motionSamples[0].timestampUs==21&&merged.motionSamples[1].timestampUs==22);
    // Without valid motion on both sides the newest stands as it is.
    auto plain=held(0,255);CHECK(q.push(next,t0+23*ms));CHECK(q.push(plain,t0+24*ms));CHECK(q.take(t0+24*ms).motionSampleCount==0);
}
TEST(controller_in_step_owner_sends_every_state){
    // The usual case: the owner comes round more often than the pad is read. Nothing merges, nothing waits.
    using namespace pad;ControllerQueue q;HostTime t=7000*ms;unsigned wrong=0;
    for(int i=0;i<500;++i){const auto s=held(i%50<25?S::R1:0,0,std::int16_t(i));CHECK(q.push(s,t));wrong+=!(q.take(t+2*ms)==s);wrong+=!(q.take(t+6*ms)==s);t+=8*ms;}
    CHECK(wrong==0&&q.stats().merged==0&&q.stats().expired==0&&q.stats().highWaterPending==1&&q.stats().highWaterAge100ns==2*ms);
}
TEST(input_and_mailbox_zero_generation){InputGate g;THROWS(g.begin(0));LatestMailbox<int> m;THROWS(m.begin(0));VideoIngress v;THROWS(v.begin(0));AudioIngress a;THROWS(a.begin(0));}
TEST(inbox_frame_stamped_after_reader_clock){
    // The same through the session boundary, in the product's order: the reader's clock
    // sample, then the callback stamps and queues a frame, then the reader takes the lock.
    SessionInbox q;auto t=q.begin(monotonic100ns());const auto readerNow=monotonic100ns()-1;
    CHECK(t.video(config(t.generation(),monotonic100ns())));CHECK(t.video(frame(0,true,monotonic100ns(),t.generation())));
    auto v=q.tryVideo(readerNow);CHECK(v&&v->sample.wireFrameIndex==0);CHECK(q.snapshot().video.recoveries==0&&!q.snapshot().video.waitingForIdr);
}
TEST(inbox_real_sample_vs_config_counts){SessionInbox q;auto t=q.begin(monotonic100ns());auto now=monotonic100ns();CHECK(t.video(config(t.generation(),now)));CHECK(q.snapshot().video.accessUnits==0);CHECK(t.video(frame(0,true,monotonic100ns(),t.generation())));CHECK(q.snapshot().video.accessUnits==1);auto v=q.tryVideo(monotonic100ns());CHECK(v&&v->sample.generation==t.generation());}
}
TEST(recovery_keyframes_before_reconnect){
    StreamRecovery r;using A=StreamRecovery::Action;r.beginAttempt(100);r.frame(200);
    CHECK(r.poll(1199,false,false)==A::Wait);CHECK(r.poll(1200,false,false)==A::Keyframe);
    CHECK(r.poll(3199,false,false)==A::Wait);CHECK(r.poll(3200,false,false)==A::Keyframe);
    CHECK(r.poll(6199,false,false)==A::Wait);CHECK(r.poll(6200,false,false)==A::Reconnect);CHECK(r.reconnects()==1);
}
TEST(recovery_initial_and_login_deadlines){
    StreamRecovery r;using A=StreamRecovery::Action;r.beginAttempt(100);
    CHECK(r.poll(30099,false,false)==A::Wait);CHECK(r.poll(30100,false,false)==A::Fail);CHECK(r.reconnects()==0);
    r.frame(50000);CHECK(r.poll(169999,true,true,false)==A::Wait);CHECK(r.poll(170000,true,true,false)==A::Fail);
    CHECK(r.reconnects()==0);
}
TEST(recovery_budget_survives_brief_recovery){
    StreamRecovery r;using A=StreamRecovery::Action;
    for(int i=0;i<3;++i){r.beginAttempt(i*10000);r.frame(i*10000+100);CHECK(r.poll(i*10000+6100,false,false)==A::Reconnect);CHECK(r.reconnects()==unsigned(i+1));}
    r.beginAttempt(30000);r.frame(30100);CHECK(r.poll(36100,false,false)==A::Fail);CHECK(r.reconnects()==3);
}
TEST(recovery_progress_and_terminal_quit){
    StreamRecovery r;using A=StreamRecovery::Action;r.beginAttempt(0);r.frame(100);
    CHECK(r.poll(1100,false,false)==A::Keyframe);r.frame(1500);CHECK(r.poll(2000,false,false)==A::Wait);
    CHECK(r.poll(2200,false,true,false)==A::Fail);CHECK(r.reconnects()==0);
    CHECK(r.poll(2200,false,true,true)==A::Reconnect);r.beginAttempt(5000);
    CHECK(r.poll(11000,false,false)==A::Wait);CHECK(r.poll(34999,false,false)==A::Wait);CHECK(r.poll(35000,false,false)==A::Reconnect);
}
TEST(recovery_retryable_busy_requires_prior_video){
    StreamRecovery r;using A=StreamRecovery::Action;r.beginAttempt(0);
    CHECK(r.poll(500,false,true,true)==A::Fail);CHECK(r.reconnects()==0);
    r.frame(1000);CHECK(r.poll(7000,false,false)==A::Reconnect);r.beginAttempt(8000);
    CHECK(r.poll(8500,false,true,true)==A::Reconnect);CHECK(r.reconnects()==2);
}
TEST(recovery_manual_busy_retry_is_bounded){
    StreamRecovery r;using A=StreamRecovery::Action;r.beginAttempt(0);
    for(int i=0;i<3;++i){CHECK(r.poll(i*2000+500,false,true,true,true)==A::Reconnect);r.beginAttempt((i+1)*2000);}
    CHECK(r.poll(6500,false,true,true,true)==A::Fail);CHECK(r.reconnects()==3);
    StreamRecovery denied;denied.beginAttempt(0);
    CHECK(denied.poll(500,false,true,false,true)==A::Fail);CHECK(denied.reconnects()==0);
}
TEST(recovery_busy_then_slow_handshake){
    StreamRecovery r;using A=StreamRecovery::Action;r.beginAttempt(0);
    CHECK(r.poll(500,false,true,true,true)==A::Reconnect);r.beginAttempt(2000);
    CHECK(r.poll(8000,false,false)==A::Wait);CHECK(r.poll(31999,false,false)==A::Wait);
    r.frame(31999);CHECK(r.poll(32999,false,false)==A::Keyframe);
    CHECK(r.poll(37999,false,false)==A::Reconnect);
}
TEST(recovery_stable_progress_renews_outage_budget_not_identity){
    StreamRecovery r;using A=StreamRecovery::Action;
    for(int i=0;i<3;++i){r.beginAttempt(i*10000);r.frame(i*10000+100);CHECK(r.poll(i*10000+6100,false,false)==A::Reconnect);}
    r.beginAttempt(30000);CHECK(!r.frame(30100));
    for(int64_t t=30200;t<60100;t+=100)CHECK(!r.frame(t));
    CHECK(r.episodeRetries()==3);CHECK(r.frame(60100));CHECK(r.episodeRetries()==0);CHECK(r.reconnects()==3);
    CHECK(!r.frame(60200));CHECK(r.poll(66200,false,false)==A::Reconnect);
    CHECK(r.episodeRetries()==1);CHECK(r.reconnects()==4);
}
TEST(recovery_intermittent_frames_do_not_renew_budget){
    StreamRecovery r;using A=StreamRecovery::Action;r.beginAttempt(0);r.frame(100);CHECK(r.poll(6100,false,false)==A::Reconnect);r.beginAttempt(7000);
    for(int64_t t=7100;t<150000;t+=1000)CHECK(!r.frame(t));
    CHECK(r.episodeRetries()==1);CHECK(r.reconnects()==1);
}
int main(int argc,char**argv){
    std::string filter;
    if(argc==2&&std::string_view(argv[1])=="--list"){for(auto [name,f]:tests()){(void)f;std::cout<<name<<'\n';}return 0;}
    if(argc==3&&std::string_view(argv[1])=="--case")filter=argv[2];
    else if(argc!=1){std::cerr<<"Usage: core_tests [--list | --case NAME]\n";return 2;}
    unsigned pass=0,fail=0;
    for(auto [name,f]:tests()){
        if(!filter.empty()&&filter!=name)continue;
        try{f();++pass;std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& e){++fail;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    }
    if(pass+fail==0){std::cerr<<"No test matched\n";return 2;}
    std::cout<<"TOTAL "<<pass+fail<<" PASS "<<pass<<" FAIL "<<fail<<'\n';
    return fail?1:0;
}
