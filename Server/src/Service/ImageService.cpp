#include "Service/ImageService.h"
#include "DAO/PhotoQuery.h"
#include "FEventLoop.h"
#include "FLogger.h"
#include <deque>
#include <future>
#include <stdexcept>
#include <unordered_map>

namespace Blog {
struct ImageService::State {
    Fei::FEventLoop& loop;
    Fei::Async::JobPool workers;
    Fei::Http::FHttpClient client;
    Utils::ImageRequester requester;
    struct Entry { std::string url; Fei::Async::OperationHandle operation; };
    std::unordered_map<std::uint64_t, Entry> active;
    std::unordered_map<std::uint64_t, std::string> pending;
    std::deque<std::uint64_t> order;
    bool stopping = false;
    bool closed = false;
    std::function<void()> drained;

    explicit State(Fei::FEventLoop& loop) : loop(loop), client(loop), requester(client, workers) {}
    void pump() {
        while (!stopping && active.size() < 4 && !order.empty()) {
            const auto id = order.front(); order.pop_front();
            auto it = pending.find(id);
            if (it == pending.end()) continue;
            // A cancelled older URL must finish before its replacement starts.
            if (active.contains(id)) { order.push_back(id); break; }
            auto url = std::move(it->second); pending.erase(it);
            auto& entry = active[id]; entry.url = url;
            Utils::ImageRequest request; request.url = url;
            entry.operation = requester.requestDimensions(std::move(request), [this, id, url](Fei::Async::Result<Utils::ImageInfo> result) {
                active.erase(id);
                try {
                if (!stopping && result.ok()) {
                    auto error = DAO::PhotoQuery::UpdatePhotoDimsIfUrlMatches(id, url, result.value->width, result.value->height);
                    if (error) Fei::Logger::instance()->log(Fei::lvl::warn, "[ImageService] Could not save dimensions for photo {}", id);
                } else if (!stopping && result.error.code != Fei::Async::ErrorCode::Cancelled) {
                    // Do not log signed image URLs or upstream messages.
                    Fei::Logger::instance()->log(Fei::lvl::warn, "[ImageService] Dimension request for photo {} failed (code {})", id, static_cast<int>(result.error.code));
                }
                }
                catch (...) { Fei::Logger::instance()->log(Fei::lvl::warn, "[ImageService] Dimension completion for photo {} failed", id); }
                pump();
                if (stopping && active.empty() && drained) { auto notify = std::move(drained); notify(); }
            });
        }
    }
    void enqueue(std::uint64_t id, std::string url) {
        if (stopping) return;
        // Edits and queued refreshes may arrive in different orders. Never let
        // an obsolete URL cancel a request for the row's current image.
        auto photo = DAO::PhotoQuery::QueryPhotoById(id);
        if (!photo || photo->url != url || (photo->width > 0 && photo->height > 0)) return;
        auto it = active.find(id);
        if (it != active.end() && it->second.url == url && !pending.contains(id)) return;
        if (!pending.contains(id)) order.push_back(id);
        pending[id] = std::move(url);
        if (it != active.end()) it->second.operation.cancel();
        pump();
    }
};

ImageService::ImageService(Fei::FEventLoop& loop) : state_(std::make_unique<State>(loop)) {
    ImageService* empty = nullptr;
    if (!current_.compare_exchange_strong(empty, this, std::memory_order_release))
        throw std::logic_error("Only one Server ImageService may be active.");
}
ImageService::~ImageService() {
    shutdown();
    current_.store(nullptr, std::memory_order_release);
}
void ImageService::refreshDimensions(std::uint64_t id, std::string url) {
    if (!id || url.empty() || !accepting_.load()) return;
    state_->loop.AddTask([state = state_.get(), id, url = std::move(url)]() mutable {
        try { state->enqueue(id, std::move(url)); }
        catch (...) { Fei::Logger::instance()->log(Fei::lvl::warn, "[ImageService] Could not queue dimensions for photo {}", id); }
    });
}
void ImageService::shutdown() {
    if (state_->closed) return;
    if (state_->loop.isInLoopThread()) throw std::logic_error("ImageService shutdown must run on the control thread.");
    accepting_.store(false);
    std::promise<void> graphsDone;
    auto graphsFuture = graphsDone.get_future();
    state_->loop.AddTask([state = state_.get(), &graphsDone] {
        state->stopping = true;
        state->pending.clear(); state->order.clear();
        state->drained = [&graphsDone] { graphsDone.set_value(); };
        for (auto& [id, entry] : state->active) entry.operation.cancel();
        if (state->active.empty()) { auto notify = std::move(state->drained); notify(); }
    });
    graphsFuture.get();
    // Graph cancellation may finish before a cooperative worker returns.
    state_->workers.shutdown();
    std::promise<void> httpDone;
    auto httpFuture = httpDone.get_future();
    state_->client.shutdown([&httpDone] { httpDone.set_value(); });
    httpFuture.get();
    state_->closed = true;
}
} // namespace Blog
