#pragma once
#include "Async/FAsyncTypes.h"
#include "FDef.h"
#include "Http/FHttpClientTypes.h"
#include <cstddef>
#include <string>
#include <string_view>

namespace Fei::Http {
// Incremental HTTP/1.x client response parser. It does not perform I/O or
// content decoding. Header limits cover interim responses and final trailers.
class F_API FHttpResponseParser {
public:
    explicit FHttpResponseParser(std::string requestMethod = "GET",
                                 std::size_t maxBodyBytes = 4 * 1024 * 1024,
                                 std::size_t maxHeaderBytes = 64 * 1024);
    bool feed(std::string_view bytes);
    bool finishEof();
    bool isComplete() const noexcept { return state_ == State::Done; }
    bool hasError() const noexcept { return state_ == State::Error; }
    Async::Error error() const { return error_; }
    const ClientResponse& response() const noexcept { return response_; }
    ClientResponse takeResponse();
    std::size_t remainingBytes() const noexcept { return remaining_; }

private:
    enum class State { Status, Headers, FixedBody, EofBody, ChunkSize,
                       ChunkBody, ChunkEnd, Trailers, Done, Error };
    void process();
    bool readLine(std::string& line, bool countHeaderBytes = true);
    bool parseStatus(const std::string& line);
    bool parseHeader(const std::string& line, bool trailer);
    bool finishHeaders();
    bool appendBody(std::size_t count);
    void complete();
    void fail(Async::ErrorCode code, const char* message);
    void discardConsumed();
    std::size_t available() const noexcept { return input_.size() - cursor_; }

    State state_ = State::Status;
    ClientResponse response_;
    Async::Error error_;
    std::string method_;
    std::string input_;
    std::size_t cursor_ = 0;
    std::size_t maxBody_;
    std::size_t maxHeader_;
    std::size_t headerBytes_ = 0;
    std::size_t contentLength_ = 0;
    std::size_t bodyRemaining_ = 0;
    std::size_t remaining_ = 0;
    bool hasContentLength_ = false;
    bool hasTransferEncoding_ = false;
    bool close_ = false;
    bool keepAlive_ = false;
};
} // namespace Fei::Http
