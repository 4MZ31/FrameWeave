// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// PushFPS: ask something that may never answer (a device driver) without waiting for ever.
//
// Each question runs on a thread of its own and is waited for up to a limit. One that has not
// answered by then is reported as not answered and its thread is left to finish; while it still
// waits the same key is not asked again (`Outcome::StillWaiting`, at once), and an answer that comes
// late is handed out once at the next question (`Outcome::AnsweredEarlier`) and then forgotten, so
// the key is asked afresh after that. Pure C++, no Windows: the thread's own set-up (COM) is the
// caller's, inside the function it passes.
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace veyra::source {

template<class Key,class Value>
class PushTimedProbes {
public:
    enum class Outcome{Answered,AnsweredEarlier,NoAnswer,StillWaiting,NoThread};
    struct Result{Outcome outcome=Outcome::NoAnswer;Value value{};};

    Result ask(const Key& key,std::function<Value()> question,std::chrono::milliseconds limit){
        std::shared_ptr<Slot> slot;
        {
            std::lock_guard g(lock_);
            auto it=slots_.find(key);
            if(it!=slots_.end()){
                const auto held=it->second;
                bool answered=false;Value value{};
                {std::lock_guard sg(held->lock);answered=held->answered;value=held->value;}
                if(!answered)return {Outcome::StillWaiting,{}};
                slots_.erase(it);
                return {Outcome::AnsweredEarlier,value};
            }
            slot=std::make_shared<Slot>();
            slots_[key]=slot;
        }
        try{
            std::thread([slot,question=std::move(question)]{
                Value value{};
                try{value=question();}catch(...){}
                {std::lock_guard g(slot->lock);slot->answered=true;slot->value=value;}
                slot->done.notify_all();
            }).detach();
        }catch(...){
            std::lock_guard g(lock_);slots_.erase(key);
            return {Outcome::NoThread,{}};
        }
        std::unique_lock u(slot->lock);
        if(!slot->done.wait_for(u,limit,[&]{return slot->answered;}))return {Outcome::NoAnswer,{}};
        const Value value=slot->value;
        u.unlock();
        std::lock_guard g(lock_);
        auto it=slots_.find(key);
        if(it!=slots_.end()&&it->second==slot)slots_.erase(it);
        return {Outcome::Answered,value};
    }

    // How many questions are still waiting on their thread (for the log and the tests).
    size_t waiting()const{
        std::lock_guard g(lock_);size_t n=0;
        for(const auto& [k,s]:slots_){std::lock_guard sg(s->lock);if(!s->answered)++n;}
        return n;
    }

private:
    struct Slot{std::mutex lock;std::condition_variable done;bool answered=false;Value value{};};
    mutable std::mutex lock_;
    std::map<Key,std::shared_ptr<Slot>> slots_;
};

}
