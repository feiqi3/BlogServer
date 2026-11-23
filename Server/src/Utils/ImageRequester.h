#pragma once
#include "Async/FAsyncTypes.h"
#include "Async/FJobPool.h"
#include "Http/FHttpClient.h"
#include "Utils/ImageInfo.h"
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace Blog::Utils {
struct ImageRequest {
    std::string url;
    std::chrono::milliseconds timeout{12000};
    std::size_t maxBytes = 8 * 1024 * 1024;
    std::size_t probeBytes = 64 * 1024;
    std::uint64_t maxPixels = 200'000'000;
    unsigned maxRedirects = 3;
};
struct ImageResponse {
    std::shared_ptr<const std::string> bytes;
    ImageInfo info;
    std::string finalUrl;
    std::string contentType;
    bool partial = false;
};

// Application-internal image fetcher; owns no thread, Loop, client or worker.
// HTTP/TLS uses client; metadata inspection uses workers. Completion always
// returns to client.loop(). Keep dependencies alive until all work has drained.
class ImageRequester {
public:
    using Callback = std::function<void(Fei::Async::Result<ImageResponse>)>;
    using DimensionsCallback = std::function<void(Fei::Async::Result<ImageInfo>)>;
    ImageRequester(Fei::Http::FHttpClient& client, Fei::Async::JobPool& workers)
        : client_(client), workers_(workers) {}
    Fei::Async::OperationHandle requestImage(ImageRequest request, Callback callback);
    // First tries a bounded Range prefix, then one bounded full-body fallback
    // when the server ignores Range or the image metadata is beyond the prefix.
    Fei::Async::OperationHandle requestDimensions(ImageRequest request, DimensionsCallback callback);
private:
    Fei::Async::OperationHandle start(ImageRequest request, bool probe, Callback callback);
    Fei::Http::FHttpClient& client_;
    Fei::Async::JobPool& workers_;
};
} // namespace Blog::Utils
