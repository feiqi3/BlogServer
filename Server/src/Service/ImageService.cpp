#include "Service/ImageService.h"
#include "DAO/DataBaseOperation.h"
#include "DAO/PhotoQuery.h"
#include "FEventLoop.h"
#include "FLogger.h"
#include <deque>
#include <future>
#include <stdexcept>
#include <unordered_map>

namespace Blog {
struct ImageService::State {
    static constexpr std::size_t maxDownloads = 4;
    static constexpr int backfillPageSize = 16;
    using BackfillPage = std::vector<std::pair<std::uint64_t, std::string>>;

    Fei::FEventLoop& loop;
    Fei::Async::JobPool workers;
    Fei::Async::JobPool database{{1, 256}};
    Fei::Http::FHttpClient client;
    Utils::ImageRequester requester;
    struct Entry { std::string url; Fei::Async::OperationHandle operation; };
    std::unordered_map<std::uint64_t, Entry> active;
    std::unordered_map<std::uint64_t, std::string> pending;
    std::deque<std::uint64_t> order;
    std::uint64_t backfillCursor = 0;
    std::size_t databasePending = 0;
    bool backfilling = false;
    bool loadingPage = false;
    bool stopping = false;
    bool closed = false;
    std::function<void()> drained;

    explicit State(Fei::FEventLoop& loop) : loop(loop), client(loop), requester(client, workers) {}

    void checkDrained() {
        if (stopping && active.empty() && databasePending == 0 && drained) {
            auto notify = std::move(drained);
            notify();
        }
    }

    void runDatabase(Fei::Async::JobPool::Work work, Fei::Async::JobPool::Completion completion) {
        database.submit([work = std::move(work)](auto token) {
            DatabaseOperation::instance()->usePrivateCacheForCurrentThread();
            return work(token);
        }, [this, completion = std::move(completion)](auto result) {
            --databasePending;
            completion(std::move(result));
            checkDrained();
        }, &loop);
        ++databasePending;
    }

    void loadBackfillPage() {
        loadingPage = true;
        runDatabase([cursor = backfillCursor](auto) -> std::any {
            return DAO::PhotoQuery::QueryPhotosMissingDims(cursor, backfillPageSize);
        }, [this](auto result) {
            loadingPage = false;
            if (stopping) return;
            if (!result.ok()) {
                if (result.error.code != Fei::Async::ErrorCode::QueueFull) {
                    backfilling = false;
                    Fei::Logger::instance()->log(Fei::lvl::warn, "[ImageService] Could not load missing photo dimensions");
                }
                return;
            }
            auto photos = std::any_cast<BackfillPage>(std::move(*result.value));
            if (!photos.empty()) backfillCursor = photos.back().first;
            backfilling = photos.size() == backfillPageSize;
            for (auto& [id, url] : photos) {
                if (url.empty() || active.contains(id) || pending.contains(id)) continue;
                order.push_back(id);
                pending.emplace(id, std::move(url));
            }
            pump();
        });
    }

    void pump() {
        if (stopping) return;
        for (auto remaining = order.size(); remaining > 0 && active.size() < maxDownloads; --remaining) {
            const auto id = order.front(); order.pop_front();
            auto it = pending.find(id);
            if (it == pending.end()) continue;
            // A cancelled older URL must finish before its replacement starts.
            if (active.contains(id)) { order.push_back(id); continue; }
            auto url = std::move(it->second); pending.erase(it);
            auto& entry = active[id]; entry.url = url;
            Utils::ImageRequest request; request.url = url;
            entry.operation = requester.requestDimensions(std::move(request), [this, id, url](Fei::Async::Result<Utils::ImageInfo> result) {
                if (!stopping && result.ok()) {
                    runDatabase([id, url, info = *result.value](auto) -> std::any {
                        return !DAO::PhotoQuery::UpdatePhotoDimsIfUrlMatches(id, url, info.width, info.height);
                    }, [this, id](auto saved) {
                        if (!stopping && (!saved.ok() || !std::any_cast<bool>(*saved.value)))
                            Fei::Logger::instance()->log(Fei::lvl::warn, "[ImageService] Could not save dimensions for photo {}", id);
                        finish(id);
                    });
                    return;
                } else if (!stopping && result.error.code != Fei::Async::ErrorCode::Cancelled) {
                    // Do not log signed image URLs or upstream messages.
                    Fei::Logger::instance()->log(Fei::lvl::warn, "[ImageService] Dimension request for photo {} failed (code {})", id, static_cast<int>(result.error.code));
                }
                finish(id);
            });
        }
        if (backfilling && !loadingPage && active.size() < maxDownloads && order.empty()) loadBackfillPage();
    }

    void finish(std::uint64_t id) {
        active.erase(id);
        pump();
        checkDrained();
    }
    void enqueue(std::uint64_t id, std::string url) {
        if (stopping) return;
        // Validate edits in order on the database worker before replacing work.
        runDatabase([id, url](auto) -> std::any {
            return DAO::PhotoQuery::NeedsDimensions(id, url);
        }, [this, id, url = std::string(url)](auto needed) mutable {
            if (stopping) return;
            if (!needed.ok()) {
                Fei::Logger::instance()->log(Fei::lvl::warn, "[ImageService] Could not check dimensions for photo {}", id);
                return;
            }
            if (std::any_cast<bool>(*needed.value)) {
                auto it = active.find(id);
                if (it == active.end() || it->second.url != url || pending.contains(id)) {
                    if (!pending.contains(id)) order.push_back(id);
                    pending[id] = std::move(url);
                    if (it != active.end()) it->second.operation.cancel();
                }
            }
            pump();
        });
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
void ImageService::backfillDimensions() {
    if (!accepting_.load()) return;
    state_->loop.AddTask([state = state_.get()] {
        if (state->stopping || state->backfilling) return;
        state->backfillCursor = 0;
        state->backfilling = true;
        state->pump();
    });
}
void ImageService::shutdown() {
    if (state_->closed) return;
    if (state_->loop.isInLoopThread()) throw std::logic_error("ImageService shutdown must run on the control thread.");
    accepting_.store(false);
    std::promise<void> graphsDone;
    auto graphsFuture = graphsDone.get_future();
    std::promise<void> stopStarted;
    auto stopFuture = stopStarted.get_future();
    state_->loop.AddTask([state = state_.get(), &graphsDone, &stopStarted] {
        state->stopping = true;
        state->pending.clear(); state->order.clear();
        state->drained = [&graphsDone] { graphsDone.set_value(); };
        for (auto& [id, entry] : state->active) entry.operation.cancel();
        state->checkDrained();
        stopStarted.set_value();
    });
    stopFuture.get();
    state_->database.shutdown();
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
