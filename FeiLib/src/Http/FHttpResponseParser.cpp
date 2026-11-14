#include "Http/FHttpResponseParser.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace Fei::Http {
namespace {
std::string lower(std::string_view value) {
    std::string result(value);
    for (auto& c : result)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return result;
}
std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.remove_suffix(1);
    return value;
}
bool token(std::string_view value) {
    if (value.empty()) return false;
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) continue;
        if (std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(c)) == std::string_view::npos) return false;
    }
    return true;
}
bool fieldValue(std::string_view value) {
    for (unsigned char c : value)
        if ((c < 32 && c != '\t') || c == 127) return false;
    return true;
}
bool unsignedNumber(std::string_view value, unsigned base, std::size_t& result) {
    if (value.empty()) return false;
    result = 0;
    for (unsigned char c : value) {
        unsigned digit = 255;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        if (digit >= base || result > (std::numeric_limits<std::size_t>::max() - digit) / base) return false;
        result = result * base + digit;
    }
    return true;
}
bool containsToken(std::string_view values, std::string_view wanted) {
    while (!values.empty()) {
        const auto comma = values.find(',');
        if (lower(trim(values.substr(0, comma))) == wanted) return true;
        if (comma == std::string_view::npos) break;
        values.remove_prefix(comma + 1);
    }
    return false;
}
bool chunkExtensions(std::string_view text) {
    while (!text.empty()) {
        text = trim(text);
        if (text.empty() || text.front() != ';') return false;
        text.remove_prefix(1);
        text = trim(text);
        const auto nameEnd = text.find_first_of("=; \t");
        const auto name = text.substr(0, nameEnd);
        if (!token(name)) return false;
        text.remove_prefix(name.size());
        text = trim(text);
        if (!text.empty() && text.front() == '=') {
            text.remove_prefix(1);
            text = trim(text);
            if (text.empty()) return false;
            if (text.front() == '"') {
                text.remove_prefix(1);
                bool closed = false;
                while (!text.empty()) {
                    const auto c = static_cast<unsigned char>(text.front());
                    text.remove_prefix(1);
                    if (c == '"') { closed = true; break; }
                    if (c == '\\') {
                        if (text.empty()) return false;
                        const auto escaped = static_cast<unsigned char>(text.front());
                        text.remove_prefix(1);
                        if ((escaped < 32 && escaped != '\t') || escaped == 127) return false;
                    } else if ((c < 32 && c != '\t') || c == 127) return false;
                }
                if (!closed) return false;
            } else {
                const auto valueEnd = text.find_first_of("; \t");
                const auto value = text.substr(0, valueEnd);
                if (!token(value)) return false;
                text.remove_prefix(value.size());
            }
            text = trim(text);
        }
        if (!text.empty() && text.front() != ';') return false;
    }
    return true;
}
} // namespace

FHttpResponseParser::FHttpResponseParser(std::string requestMethod,
                                       std::size_t maxBodyBytes,
                                       std::size_t maxHeaderBytes)
    : method_(std::move(requestMethod)), maxBody_(maxBodyBytes), maxHeader_(maxHeaderBytes) {
    if (maxHeader_ == 0) fail(Async::ErrorCode::InvalidArgument, "Header limit must be positive");
}

void FHttpResponseParser::fail(Async::ErrorCode code, const char* message) {
    state_ = State::Error;
    error_ = {code, message};
    response_.keepAlive = false;
}

void FHttpResponseParser::discardConsumed() {
    if (cursor_ != 0) { input_.erase(0, cursor_); cursor_ = 0; }
}

bool FHttpResponseParser::feed(std::string_view bytes) {
    if (hasError()) return false;
    while (!bytes.empty()) {
        if (isComplete()) {
            if (bytes.size() > std::numeric_limits<std::size_t>::max() - remaining_) {
                fail(Async::ErrorCode::LimitExceeded, "Excess response bytes overflow");
                return false;
            }
            remaining_ += bytes.size();
            response_.keepAlive = false;
            return true;
        }
        const auto count = std::min<std::size_t>(bytes.size(), 8192);
        input_.append(bytes.data(), count);
        bytes.remove_prefix(count);
        process();
        if (hasError()) return false;
        discardConsumed();
    }
    return !hasError();
}

bool FHttpResponseParser::readLine(std::string& line, bool countHeaderBytes) {
    const auto end = input_.find("\r\n", cursor_);
    const auto newline = input_.find('\n', cursor_);
    const auto carriage = input_.find('\r', cursor_);
    if ((newline != std::string::npos && (end == std::string::npos || newline != end + 1)) ||
        (carriage != std::string::npos && carriage + 1 < input_.size() && input_[carriage + 1] != '\n')) {
        fail(Async::ErrorCode::Protocol, "HTTP lines require CRLF");
        return false;
    }
    const auto size = end == std::string::npos ? available() : end - cursor_ + 2;
    if (size > maxHeader_ || (countHeaderBytes && size > maxHeader_ - headerBytes_)) {
        fail(Async::ErrorCode::LimitExceeded, "Response header or trailer limit exceeded");
        return false;
    }
    if (end == std::string::npos) return false;
    line.assign(input_, cursor_, end - cursor_);
    cursor_ = end + 2;
    if (countHeaderBytes) headerBytes_ += size;
    return true;
}

bool FHttpResponseParser::parseStatus(const std::string& line) {
    if (line.size() < 12 || (line.compare(0, 8, "HTTP/1.1") != 0 && line.compare(0, 8, "HTTP/1.0") != 0)
        || line[8] != ' ' || (line.size() > 12 && line[12] != ' ')
        || line[9] < '1' || line[9] > '5' || line[10] < '0' || line[10] > '9'
        || line[11] < '0' || line[11] > '9' || !fieldValue(line)) {
        fail(Async::ErrorCode::Protocol, "Invalid HTTP response status line");
        return false;
    }
    response_ = {};
    response_.version = line.substr(0, 8);
    response_.status = (line[9] - '0') * 100 + (line[10] - '0') * 10 + line[11] - '0';
    response_.reason = line.size() > 12 ? line.substr(13) : std::string{};
    hasContentLength_ = false;
    hasTransferEncoding_ = false;
    contentLength_ = 0;
    close_ = false;
    keepAlive_ = false;
    if (response_.status == 101) {
        fail(Async::ErrorCode::Protocol, "Protocol upgrades are not supported");
        return false;
    }
    state_ = State::Headers;
    return true;
}

bool FHttpResponseParser::parseHeader(const std::string& line, bool trailer) {
    const auto colon = line.find(':');
    if (colon == std::string::npos || !token(std::string_view(line).substr(0, colon)) ||
        !fieldValue(std::string_view(line).substr(colon + 1))) {
        fail(Async::ErrorCode::Protocol, "Invalid HTTP header or trailer");
        return false;
    }
    const auto name = lower(std::string_view(line).substr(0, colon));
    const auto value = trim(std::string_view(line).substr(colon + 1));
    if (trailer) {
        if (name == "content-length" || name == "transfer-encoding" || name == "host" || name == "connection"
            || name == "trailer" || name == "content-encoding" || name == "content-type") {
            fail(Async::ErrorCode::Protocol, "Trailer attempts to change response framing or interpretation");
            return false;
        }
    } else if (name == "content-length") {
        if (hasContentLength_ || !unsignedNumber(value, 10, contentLength_)) {
            fail(Async::ErrorCode::Protocol, "Invalid or duplicate Content-Length");
            return false;
        }
        hasContentLength_ = true;
    } else if (name == "transfer-encoding") {
        if (hasTransferEncoding_ || lower(value) != "chunked") {
            fail(Async::ErrorCode::Protocol, "Only a single chunked transfer coding is supported");
            return false;
        }
        hasTransferEncoding_ = true;
    } else if (name == "connection") {
        close_ = close_ || containsToken(value, "close");
        keepAlive_ = keepAlive_ || containsToken(value, "keep-alive");
    }
    response_.headers.emplace_back(line.substr(0, colon), std::string(value));
    return true;
}

bool FHttpResponseParser::finishHeaders() {
    if (hasContentLength_ && hasTransferEncoding_) {
        fail(Async::ErrorCode::Protocol, "Ambiguous Content-Length and Transfer-Encoding");
        return false;
    }
    if (response_.version == "HTTP/1.0" && hasTransferEncoding_) {
        fail(Async::ErrorCode::Protocol, "HTTP/1.0 cannot use chunked transfer coding");
        return false;
    }
    if (response_.status < 200) {
        if (hasContentLength_ || hasTransferEncoding_) {
            fail(Async::ErrorCode::Protocol, "Interim response cannot have body framing");
            return false;
        }
        state_ = State::Status;
        return true;
    }
    if (response_.status == 204 && (hasContentLength_ || hasTransferEncoding_)) {
        fail(Async::ErrorCode::Protocol, "204 response cannot have body framing");
        return false;
    }
    if (method_ == "CONNECT" && response_.status >= 200 && response_.status < 300) {
        fail(Async::ErrorCode::Protocol, "CONNECT tunnels are not supported");
        return false;
    }
    response_.keepAlive = !close_ && (response_.version == "HTTP/1.1" || keepAlive_);
    if (method_ == "HEAD" || response_.status == 204 || response_.status == 304) {
        complete();
    } else if (hasTransferEncoding_) {
        state_ = State::ChunkSize;
    } else if (hasContentLength_) {
        if (contentLength_ > maxBody_) {
            fail(Async::ErrorCode::LimitExceeded, "Response body limit exceeded");
            return false;
        }
        bodyRemaining_ = contentLength_;
        state_ = State::FixedBody;
        if (bodyRemaining_ == 0) complete();
    } else {
        response_.keepAlive = false;
        state_ = State::EofBody;
    }
    return !hasError();
}

bool FHttpResponseParser::appendBody(std::size_t count) {
    if (count > maxBody_ - response_.body.size()) {
        fail(Async::ErrorCode::LimitExceeded, "Response body limit exceeded");
        return false;
    }
    response_.body.append(input_, cursor_, count);
    cursor_ += count;
    return true;
}

void FHttpResponseParser::complete() {
    state_ = State::Done;
    remaining_ += available();
    cursor_ = input_.size();
    if (remaining_ != 0) response_.keepAlive = false;
}

void FHttpResponseParser::process() {
    while (!hasError() && !isComplete()) {
        std::string line;
        switch (state_) {
        case State::Status:
            if (!readLine(line) || !parseStatus(line)) return;
            break;
        case State::Headers:
            if (!readLine(line)) return;
            if (line.empty()) { if (!finishHeaders()) return; }
            else if (!parseHeader(line, false)) return;
            break;
        case State::FixedBody:
        case State::ChunkBody: {
            const auto count = std::min(bodyRemaining_, available());
            if (!appendBody(count)) return;
            bodyRemaining_ -= count;
            if (bodyRemaining_ != 0) return;
            if (state_ == State::FixedBody) complete();
            else state_ = State::ChunkEnd;
            break;
        }
        case State::EofBody:
            appendBody(available());
            return;
        case State::ChunkSize: {
            if (!readLine(line, false)) return;
            const auto semi = line.find(';');
            const auto rawSize = std::string_view(line).substr(0, semi);
            const auto sizePart = trim(rawSize);
            if (sizePart.empty() || rawSize.front() == ' ' || rawSize.front() == '\t' ||
                (semi == std::string::npos && sizePart.size() != rawSize.size()) ||
                !unsignedNumber(sizePart, 16, bodyRemaining_) || !fieldValue(line) ||
                (semi != std::string::npos && !chunkExtensions(std::string_view(line).substr(semi)))) {
                fail(Async::ErrorCode::Protocol, "Invalid chunk size");
                return;
            }
            if (bodyRemaining_ > maxBody_ - response_.body.size()) {
                fail(Async::ErrorCode::LimitExceeded, "Chunked response body limit exceeded");
                return;
            }
            state_ = bodyRemaining_ == 0 ? State::Trailers : State::ChunkBody;
            break;
        }
        case State::ChunkEnd:
            if (available() < 2) return;
            if (input_.compare(cursor_, 2, "\r\n") != 0) {
                fail(Async::ErrorCode::Protocol, "Chunk data must end with CRLF");
                return;
            }
            cursor_ += 2;
            state_ = State::ChunkSize;
            break;
        case State::Trailers:
            if (!readLine(line)) return;
            if (line.empty()) complete();
            else if (!parseHeader(line, true)) return;
            break;
        case State::Done:
        case State::Error:
            return;
        }
    }
}

bool FHttpResponseParser::finishEof() {
    if (hasError()) return false;
    response_.keepAlive = false;
    if (isComplete()) return true;
    process();
    if (state_ == State::EofBody) complete();
    if (!isComplete()) fail(Async::ErrorCode::Protocol, "Unexpected EOF before complete HTTP response");
    discardConsumed();
    return !hasError();
}

ClientResponse FHttpResponseParser::takeResponse() {
    if (!isComplete()) throw std::logic_error("HTTP response is not complete");
    return std::move(response_);
}
} // namespace Fei::Http
