#include "Utils/ImageRequester.h"
#include "Async/FJobGraph.h"
#include <algorithm>
#include <charconv>
#include <climits>
#include <set>
#include <variant>

namespace Blog::Utils {
namespace {
using namespace Fei::Async;
using Clock = std::chrono::steady_clock;
struct Download {
    ImageResponse image;
    unsigned redirects = 0;
};
std::string lower(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}
// Resolve HTTP Location references without decoding/rewriting signed queries.
std::string resolve(const std::string& base, std::string location) {
    auto fragment = location.find('#');
    if (fragment != std::string::npos) location.resize(fragment);
    if (location.empty()) return base;
    auto schemeEnd = location.find(':');
    auto firstDelimiter = location.find_first_of("/?");
    if (schemeEnd != std::string::npos && (firstDelimiter == std::string::npos || schemeEnd < firstDelimiter)) return location;
    auto scheme = base.find("://");
    if (scheme == std::string::npos) return {};
    if (location.starts_with("//")) return base.substr(0, scheme + 1) + location;
    auto path = base.find_first_of("/?", scheme + 3);
    auto origin = base.substr(0, path);
    auto oldPath = path == std::string::npos ? "/" : base.substr(path);
    if (oldPath.front() == '?') oldPath.insert(0, "/");
    oldPath.resize(oldPath.find('?') == std::string::npos ? oldPath.size() : oldPath.find('?'));
    if (location.front() == '?') return origin + oldPath + location;
    std::string combined = location.front() == '/' ? location : oldPath.substr(0, oldPath.rfind('/') + 1) + location;
    auto query = combined.find('?');
    auto suffix = query == std::string::npos ? std::string{} : combined.substr(query);
    combined.resize(query == std::string::npos ? combined.size() : query);
    // RFC-style dot segment removal; retain empty segments (//) and escaping.
    std::vector<std::string> parts;
    for (std::size_t pos = 1; pos <= combined.size();) {
        auto end = combined.find('/', pos);
        if (end == std::string::npos) end = combined.size();
        auto part = combined.substr(pos, end - pos);
        if (part == "..") { if (!parts.empty()) parts.pop_back(); }
        else if (part != ".") parts.push_back(std::move(part));
        if (end == combined.size()) {
            if (part == "." || part == "..") parts.emplace_back();
            break;
        }
        pos = end + 1;
    }
    std::string normalized;
    for (const auto& part : parts) normalized += "/" + part;
    return origin + (normalized.empty() ? "/" : normalized) + suffix;
}
bool validRange(const Fei::Http::ClientResponse& response, std::size_t limit) {
    auto text = response.header("Content-Range");
    if (!text.starts_with("bytes 0-")) return false;
    const auto slash = text.find('/', 8);
    if (slash == std::string::npos) return false;
    std::uint64_t last = 0, total = 0;
    auto number = [](const char* begin, const char* end, std::uint64_t& value) {
        auto parsed = std::from_chars(begin, end, value);
        return parsed.ec == std::errc{} && parsed.ptr == end;
    };
    if (!number(text.data() + 8, text.data() + slash, last) || last >= limit || last + 1 != response.body.size()) return false;
    return number(text.data() + slash + 1, text.data() + text.size(), total) && total > last;
}
struct Fetch : std::enable_shared_from_this<Fetch> {
    Fei::Http::FHttpClient& client;
    ImageRequest options;
    std::string url;
    bool range;
    unsigned redirects = 0;
    Clock::time_point deadline;
    std::set<std::string> visited;
    std::function<void(Result<Download>)> completion;
    OperationHandle active;
    std::shared_ptr<std::atomic_bool> done = std::make_shared<std::atomic_bool>(false);
    std::shared_ptr<Fetch> keepAlive;

    Fetch(Fei::Http::FHttpClient& client, ImageRequest request, bool range,
          std::function<void(Result<Download>)> callback)
        : client(client), options(std::move(request)), url(options.url), range(range), completion(std::move(callback)) {
        auto fragment = url.find('#');
        if (fragment != std::string::npos) url.resize(fragment);
        const auto now = Clock::now();
        const auto max = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::time_point::max() - now);
        deadline = options.timeout.count() <= 0 ? now : (options.timeout >= max ? Clock::time_point::max() : now + options.timeout);
        options.probeBytes = std::min(options.probeBytes, options.maxBytes);
    }
    void finish(Result<Download> result) {
        if (done->exchange(true)) return;
        active.cancel(); active = {};
        auto callback = std::move(completion);
        keepAlive.reset();
        callback(std::move(result));
    }
    void fail(ErrorCode code, const char* message) { finish(Result<Download>::failure({code, message})); }
    void send() {
        if (done->load()) return;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (left.count() <= 0) { fail(ErrorCode::Timeout, "Image request deadline exceeded."); return; }
        Fei::Http::ClientRequest request;
        request.url = url;
        request.timeout = left;
        request.maxBodyBytes = range ? options.probeBytes : options.maxBytes;
        request.headers.emplace_back("Accept-Encoding", "identity");
        if (range) request.headers.emplace_back("Range", "bytes=0-" + std::to_string(options.probeBytes - 1));
        auto self = shared_from_this();
        active = client.submit(std::move(request), [self](Result<Fei::Http::ClientResponse> result) {
            if (self->done->load()) return;
            self->active = {};
            if (!result.ok()) {
                // Range is optional: some origins send the entire image anyway.
                if (self->range && result.error.code == ErrorCode::LimitExceeded) {
                    self->range = false; self->send();
                } else self->finish(Result<Download>::failure(std::move(result.error)));
                return;
            }
            auto& response = *result.value;
            if (response.status == 301 || response.status == 302 || response.status == 303 || response.status == 307 || response.status == 308) {
                auto next = resolve(self->url, response.header("Location"));
                if (response.header("Location").empty() || next.empty() || self->redirects >= self->options.maxRedirects ||
                    self->visited.contains(next)) { self->fail(ErrorCode::Protocol, "Invalid or excessive image redirects."); return; }
                if (lower(self->url).starts_with("https://") && !lower(next).starts_with("https://")) {
                    self->fail(ErrorCode::Protocol, "Image redirect would downgrade HTTPS."); return;
                }
                self->visited.insert(self->url); self->url = std::move(next); ++self->redirects; self->send(); return;
            }
            if (response.status != 200 && response.status != 206) { self->fail(ErrorCode::Protocol, "Image origin returned a non-success status."); return; }
            auto encoding = lower(response.header("Content-Encoding"));
            if (!encoding.empty() && encoding != "identity") { self->fail(ErrorCode::Protocol, "Image origin ignored identity encoding."); return; }
            if (response.status == 206 && (!self->range || !validRange(response, self->options.probeBytes))) {
                self->fail(ErrorCode::Protocol, "Invalid image Content-Range."); return;
            }
            Download output;
            output.image.bytes = std::make_shared<const std::string>(std::move(response.body));
            output.image.finalUrl = self->url;
            output.image.contentType = response.header("Content-Type");
            output.image.partial = response.status == 206;
            output.redirects = self->redirects;
            self->finish(Result<Download>::success(std::move(output)));
        });
    }
};
OperationHandle download(Fei::Http::FHttpClient& client, ImageRequest request, bool range,
                         std::function<void(Result<Download>)> callback) {
    auto state = std::make_shared<Fetch>(client, std::move(request), range, std::move(callback));
    state->keepAlive = state;
    std::weak_ptr<Fetch> weak = state;
    OperationHandle handle(nextOperationId(), [weak] {
        if (auto self = weak.lock()) self->client.loop().AddTask([self] { self->fail(ErrorCode::Cancelled, "Image download cancelled."); });
    }, state->done);
    client.loop().AddTask([state] {
        if (state->options.url.empty() || state->options.timeout.count() <= 0 ||
            !state->options.maxBytes || state->options.maxBytes > INT_MAX ||
            !state->options.probeBytes || state->options.probeBytes > state->options.maxBytes || !state->options.maxPixels ||
            state->options.maxRedirects > 10) state->fail(ErrorCode::InvalidArgument, "Invalid image request limits.");
        else state->send();
    });
    return handle;
}
struct Candidate { Download body; bool known = false; };
}

Fei::Async::OperationHandle ImageRequester::start(ImageRequest request, bool probe, Callback callback) {
    auto* client = &client_;
    auto inspect = [maxPixels = request.maxPixels](Candidate input) {
        if (!input.known) input.known = readImageInfo(*input.body.image.bytes, input.body.image.info, maxPixels);
        return input;
    };
    auto timeout = request.timeout.count() > 0 ? request.timeout : std::chrono::milliseconds{0};
    // Fetch -> inspect on worker -> optional full fetch -> inspect -> caller.
    // A single graph deadline/cancel handle covers redirects and fallback.
    return JobGraph::begin(client_.loop())
        .thenAsync<Download>([client, request, probe](const std::monostate&, auto done) {
            return download(*client, request, probe, std::move(done));
        })
        .thenJob(workers_, [inspect](const Download& body) { return inspect(Candidate{body, false}); })
        .thenAsync<Candidate>([client, request, probe](const Candidate& candidate, auto done) mutable {
            if (candidate.known || !probe || !candidate.body.image.partial) {
                done(Result<Candidate>::success(candidate)); return OperationHandle{};
            }
            request.url = candidate.body.image.finalUrl;
            request.maxRedirects -= candidate.body.redirects;
            return download(*client, request, false, [done = std::move(done)](Result<Download> body) mutable {
                if (body.ok()) done(Result<Candidate>::success(Candidate{std::move(*body.value), false}));
                else done(Result<Candidate>::failure(std::move(body.error)));
            });
        })
        .thenJob(workers_, inspect)
        .onFinish([callback](const Candidate& result) {
            if (result.known) callback(Result<ImageResponse>::success(result.body.image));
            else callback(Result<ImageResponse>::failure({ErrorCode::Protocol, "Unsupported, invalid or excessive image metadata."}));
        })
        .onError([callback](const Error& error) { callback(Result<ImageResponse>::failure(error)); })
        .dispatch(timeout);
}

Fei::Async::OperationHandle ImageRequester::requestImage(ImageRequest request, Callback callback) {
    return start(std::move(request), false, std::move(callback));
}
Fei::Async::OperationHandle ImageRequester::requestDimensions(ImageRequest request, DimensionsCallback callback) {
    return start(std::move(request), true, [callback = std::move(callback)](Result<ImageResponse> result) {
        if (result.ok()) callback(Result<ImageInfo>::success(result.value->info));
        else callback(Result<ImageInfo>::failure(std::move(result.error)));
    });
}
} // namespace Blog::Utils
