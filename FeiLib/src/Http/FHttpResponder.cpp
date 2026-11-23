#include "Http/FHttpResponder.h"
#include "Http/FHttpResponse.h"
#include "FDeferredResponse.h"
#include "FEventLoop.h"
#include <atomic>
#include <mutex>
#include <vector>
#include <stdexcept>

namespace Fei::Http {
struct FHttpResponder::State : std::enable_shared_from_this<State> {
    enum Phase { Pending, Queued, Finished, Cancelled };
    State(FEventLoop& owner,std::function<void(FHttpResponse)> callback):loop(owner),publish(std::move(callback)){}
    FEventLoop& loop;
    // Serializes enqueueing against cancellation before the owning loop dies.
    mutable std::mutex mutex;
    std::atomic<Phase> phase{Pending};
    std::function<void(FHttpResponse)> publish;
    std::vector<std::function<void()>> cancellations;
    TimerID timer=0;
    void finish(FHttpResponse response) {
        std::function<void(FHttpResponse)> callback;
        {
            std::lock_guard lock(mutex);
            if(phase!=Queued)return;
            phase=Finished;
            if(timer)loop.CancelTimer(timer);
            callback=std::move(publish);cancellations.clear();
        }
        if(callback)callback(std::move(response));
    }
    void stop(bool timeout) {
        std::function<void(FHttpResponse)> callback;
        std::vector<std::function<void()>> callbacks;
        {
            std::lock_guard lock(mutex);
            if(phase==Finished||phase==Cancelled)return;
            phase=Cancelled;
            if(timer)loop.CancelTimer(timer);
            if(timeout)callback=std::move(publish);else publish={};
            callbacks=std::move(cancellations);
        }
        for(auto& fn:callbacks){try{fn();}catch(...){}}
        if(callback){FHttpResponse response;response.setStatusCode(StatusCode::_408).setBody("Request processing timed out");callback(std::move(response));}
    }
};
FHttpResponse FHttpResponse::defer(std::function<void(FHttpResponder)> start,std::chrono::milliseconds timeout) {
    if(!start||timeout.count()<=0||timeout>std::chrono::minutes(5))throw std::invalid_argument("Invalid deferred response");
    FHttpResponse response;response.mDeferred=std::make_shared<DeferredResponse>(DeferredResponse{std::move(start),timeout});return response;
}
FHttpResponder FHttpResponder::create(FEventLoop& loop,std::chrono::milliseconds timeout,std::function<void(FHttpResponse)> publish) {
    auto state=std::make_shared<State>(loop,std::move(publish));
    std::weak_ptr<State> weak=state;
    state->timer=loop.RunAfter(timeout.count(),[weak]{if(auto state=weak.lock())state->stop(true);});
    return FHttpResponder(std::move(state));
}
bool FHttpResponder::complete(FHttpResponse response) const {
    if(!state_)return false;
    if(response.isDeferred()){response=FHttpResponse();response.setStatusCode(StatusCode::_500).setBody("Nested deferred response is invalid");}
    std::lock_guard lock(state_->mutex);
    if(state_->phase!=State::Pending)return false;
    state_->phase=State::Queued;
    auto state=state_;
    state_->loop.AddTask([state,response=std::move(response)]()mutable{state->finish(std::move(response));});
    return true;
}
bool FHttpResponder::cancelled() const {return !state_||state_->phase==State::Cancelled;}
void FHttpResponder::onCancel(std::function<void()> callback) const {
    if(!state_||!callback)return;
    std::unique_lock lock(state_->mutex);
    if(state_->phase==State::Cancelled){lock.unlock();try{callback();}catch(...){}return;}
    if(state_->phase==State::Finished)return;
    auto state=state_;
    state_->loop.AddTask([state,callback=std::move(callback)]()mutable{
        bool cancelled=false;
        {std::lock_guard lock(state->mutex);cancelled=state->phase==State::Cancelled;
         if(!cancelled&&state->phase!=State::Finished)state->cancellations.push_back(std::move(callback));}
        if(cancelled){try{callback();}catch(...){}}
    });
}
void FHttpResponder::cancel() const {if(state_)state_->stop(false);}
}
