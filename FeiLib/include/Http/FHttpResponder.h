#pragma once
#include "FDef.h"
#include <chrono>
#include <functional>
#include <memory>

namespace Fei { class FEventLoop; }
namespace Fei::Http {
class FHttpResponse;
class FHttpServer;
// Copyable completion handle. Complete may be called from any thread.
// Register cancellation in the starter on the connection loop; late registration
// after cancellation may execute inline. Do not block cancellation callbacks.
class F_API FHttpResponder {
public:
    FHttpResponder() = default;
    bool complete(FHttpResponse response) const;
    bool cancelled() const;
    void onCancel(std::function<void()> callback) const;
private:
    struct State;
    std::shared_ptr<State> state_;
    explicit FHttpResponder(std::shared_ptr<State> state) : state_(std::move(state)) {}
    static FHttpResponder create(FEventLoop&,std::chrono::milliseconds,std::function<void(FHttpResponse)>);
    void cancel() const;
    friend class FHttpServer;
};
}
