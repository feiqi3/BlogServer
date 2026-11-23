#pragma once
#include "Async/FAsyncTypes.h"
#include "FDef.h"
#include "Http/FHttpClientTypes.h"
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace Fei { class FEventLoop; }

namespace Fei::Http {
struct ClientOptions {
    std::size_t maxConcurrent = 8;
    std::size_t maxPerOrigin = 4;
    std::size_t maxQueued = 256;
    std::size_t maxIdleConnections = 8;
    std::chrono::milliseconds idleTimeout{30000};
    std::size_t maxHeaderBytes = 64 * 1024;
    std::size_t resolverThreads = 2;
    // Empty uses the system OpenSSL trust store. Verification cannot be disabled.
    std::string caFile;
    // Optional immutable connection policy, called on loop() after DNS and before
    // any socket is opened. All resolved addresses must pass. Empty preserves
    // existing behavior. Pooled connections retain the policy applied at connect.
    std::function<bool(std::string_view host, std::string_view address)> allowAddress;
};

// Nonblocking HTTP/1.1 client bound to an existing EventLoop. It owns no I/O
// thread; only blocking DNS resolution uses its small, bounded resolver pool.
// Call shutdown and wait for onDrained before stopping/destroying the Loop.
class F_API FHttpClient {
public:
    using Callback = std::function<void(Async::Result<ClientResponse>)>;
    explicit FHttpClient(FEventLoop& loop, ClientOptions options = {});
    ~FHttpClient();
    FHttpClient(const FHttpClient&) = delete;
    FHttpClient& operator=(const FHttpClient&) = delete;
    FHttpClient(FHttpClient&&) = delete;
    FHttpClient& operator=(FHttpClient&&) = delete;

    // Thread-safe admission. Callback is always queued on loop(), even when a
    // request is invalid, cancelled before admission, or rejected after shutdown.
    Async::OperationHandle submit(ClientRequest request, Callback callback);
    // Thread-safe and idempotent. Cancels admitted operations and closes idle
    // connections; onDrained runs on loop() after their completion callbacks.
    void shutdown(std::function<void()> onDrained = {});
    FEventLoop& loop() const noexcept;
private:
    struct State;
    std::shared_ptr<State> state_;
};
} // namespace Fei::Http
