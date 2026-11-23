#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace Fei::Async {
enum class ErrorCode {
    None, Cancelled, Timeout, QueueFull, Stopped, InvalidArgument,
    Dns, Connect, Tls, Io, Protocol, LimitExceeded, UserException, AccessDenied
};

struct Error {
    ErrorCode code = ErrorCode::None;
    std::string message;
};

template<class T> struct Result {
    std::optional<T> value;
    Error error;
    bool ok() const noexcept { return value.has_value() && error.code == ErrorCode::None; }
    static Result success(T value) { return {std::move(value), {}}; }
    static Result failure(Error error) { return {std::nullopt, std::move(error)}; }
};

class CancellationToken {
public:
    CancellationToken() = default;
    explicit CancellationToken(std::shared_ptr<std::atomic_bool> flag) : flag_(std::move(flag)) {}
    bool cancelled() const noexcept { return flag_ && flag_->load(); }
private:
    std::shared_ptr<std::atomic_bool> flag_;
};

// The operation implementation owns scheduling and guarantees one terminal result.
// A handle is safe to copy; dropping it does not cancel the operation.
class OperationHandle {
public:
    OperationHandle() = default;
    OperationHandle(std::uint64_t id, std::function<void()> cancel,
                    std::shared_ptr<std::atomic_bool> done)
        : id_(id), cancel_(std::move(cancel)), done_(std::move(done)) {}
    std::uint64_t id() const noexcept { return id_; }
    void cancel() const { if (cancel_ && !done()) cancel_(); }
    bool done() const noexcept { return !done_ || done_->load(); }
    explicit operator bool() const noexcept { return id_ != 0; }
private:
    std::uint64_t id_ = 0;
    std::function<void()> cancel_;
    std::shared_ptr<std::atomic_bool> done_;
};

inline std::uint64_t nextOperationId() {
    static std::atomic<std::uint64_t> id{0};
    return ++id;
}
} // namespace Fei::Async
