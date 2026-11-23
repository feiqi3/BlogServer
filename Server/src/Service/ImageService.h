#pragma once
#include "Utils/ImageRequester.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace Blog {
// Server-owned adapter for photo dimension updates. The supplied Loop must be
// running; shutdown() drains on a control thread before that Loop is stopped.
class ImageService {
public:
    explicit ImageService(Fei::FEventLoop& loop);
    ~ImageService();
    ImageService(const ImageService&) = delete;
    static ImageService* current() { return current_.load(std::memory_order_acquire); }
    void refreshDimensions(std::uint64_t photoId, std::string url);
    void shutdown();
private:
    struct State;
    std::unique_ptr<State> state_;
    std::atomic_bool accepting_{true};
    inline static std::atomic<ImageService*> current_{nullptr};
};
} // namespace Blog
