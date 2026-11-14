#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef POLLIN
#undef POLLIN
#undef POLLOUT
#undef POLLPRI
#undef POLLERR
#undef POLLHUP
#undef POLLNVAL
#undef POLLRDNORM
#undef POLLRDBAND
#undef POLLWRNORM
#undef POLLWRBAND
#endif
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "Http/FHttpClient.h"
#include "Http/FHttpResponseParser.h"
#include "FEvent.h"
#include "FEventLoop.h"
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <condition_variable>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace Fei::Http {
namespace {
using Clock = std::chrono::steady_clock;
using Async::Error;
using Async::ErrorCode;
using ResponseResult = Async::Result<ClientResponse>;
#ifdef _WIN32
using NativeSocket = SOCKET;
using SockLength = int;
constexpr NativeSocket InvalidSocket = INVALID_SOCKET;
int socketError() { return WSAGetLastError(); }
bool wouldBlock(int value) { return value == WSAEWOULDBLOCK; }
bool connectingError(int value) {
    return value == WSAEWOULDBLOCK || value == WSAEINPROGRESS || value == WSAEALREADY;
}
void closeSocket(NativeSocket fd) { if (fd != InvalidSocket) closesocket(fd); }
bool nonblocking(NativeSocket fd) { u_long enabled = 1; return ioctlsocket(fd, FIONBIO, &enabled) == 0; }
#else
using NativeSocket = int;
using SockLength = socklen_t;
constexpr NativeSocket InvalidSocket = -1;
int socketError() { return errno; }
bool wouldBlock(int value) { return value == EAGAIN || value == EWOULDBLOCK; }
bool connectingError(int value) { return value == EINPROGRESS || value == EALREADY; }
void closeSocket(NativeSocket fd) { if (fd != InvalidSocket) ::close(fd); }
bool nonblocking(NativeSocket fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) return false;
    int descriptor = fcntl(fd, F_GETFD, 0);
    if (descriptor >= 0) fcntl(fd, F_SETFD, descriptor | FD_CLOEXEC);
    return true;
}
#endif

std::string lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}
bool token(std::string_view value) {
    if (value.empty()) return false;
    for (unsigned char c : value)
        if (c >= 127 || (!std::isalnum(c) && std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(c)) == std::string_view::npos))
            return false;
    return true;
}
bool cleanValue(std::string_view value) {
    for (unsigned char c : value) if ((c < 32 && c != '\t') || c == 127) return false;
    return true;
}
struct Address { sockaddr_storage storage{}; SockLength length = 0; };
struct Url {
    bool tls = false;
    std::string host, port, authority, target, origin;
};

bool parseUrl(const std::string& value, Url& out) {
    if (value.empty()) return false;
    for (unsigned char c : value) if (c <= 32 || c == 127) return false;
    auto separator = value.find("://");
    if (separator == std::string::npos) return false;
    std::string scheme = lower(value.substr(0, separator));
    if (scheme != "http" && scheme != "https") return false;
    out.tls = scheme == "https";
    std::size_t start = separator + 3;
    std::size_t end = value.find_first_of("/?#", start);
    if (end == std::string::npos) end = value.size();
    std::string authority = value.substr(start, end - start);
    if (authority.empty() || authority.find('@') != std::string::npos || authority.find('\\') != std::string::npos)
        return false;
    out.port = out.tls ? "443" : "80";
    if (authority.front() == '[') {
        auto bracket = authority.find(']');
        if (bracket == std::string::npos) return false;
        out.host = lower(authority.substr(1, bracket - 1));
        if (bracket + 1 < authority.size()) {
            if (authority[bracket + 1] != ':') return false;
            out.port = authority.substr(bracket + 2);
        }
        in6_addr parsed{};
        if (inet_pton(AF_INET6, out.host.c_str(), &parsed) != 1) return false;
    } else {
        auto colon = authority.find(':');
        if (colon != std::string::npos) {
            if (authority.find(':', colon + 1) != std::string::npos) return false;
            out.host = lower(authority.substr(0, colon));
            out.port = authority.substr(colon + 1);
        } else out.host = lower(authority);
        for (unsigned char c : out.host)
            if (!std::isalnum(c) && c != '.' && c != '-') return false;
    }
    if (out.host.empty() || out.port.empty() || out.port.size() > 5) return false;
    unsigned port = 0;
    for (unsigned char c : out.port) {
        if (!std::isdigit(c)) return false;
        port = port * 10 + (c - '0');
    }
    if (port == 0 || port > 65535) return false;
    out.port = std::to_string(port);
    out.authority = out.host.find(':') != std::string::npos ? "[" + out.host + "]" : out.host;
    if ((!out.tls && port != 80) || (out.tls && port != 443)) out.authority += ":" + out.port;
    auto fragment = value.find('#', end);
    std::size_t targetEnd = fragment == std::string::npos ? value.size() : fragment;
    out.target = value.substr(end, targetEnd - end);
    if (out.target.empty() || out.target.front() == '?') out.target = "/" + out.target;
    out.origin = scheme + "://" + out.host + ":" + out.port;
    return !out.target.empty() && out.target.front() == '/';
}

std::vector<Address> resolve(const Url& url) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    addrinfo* addresses = nullptr;
    if (getaddrinfo(url.host.c_str(), url.port.c_str(), &hints, &addresses) != 0) return {};
    std::vector<Address> result;
    for (auto* current = addresses; current; current = current->ai_next) {
        if ((current->ai_family != AF_INET && current->ai_family != AF_INET6) ||
            current->ai_addrlen > sizeof(sockaddr_storage)) continue;
        Address address;
        std::copy_n(reinterpret_cast<const unsigned char*>(current->ai_addr), current->ai_addrlen,
                    reinterpret_cast<unsigned char*>(&address.storage));
        address.length = static_cast<SockLength>(current->ai_addrlen);
        result.push_back(address);
    }
    freeaddrinfo(addresses);
    return result;
}
} // namespace

struct FHttpClient::State : std::enable_shared_from_this<State> {
    struct Request;
    struct Connection {
        enum class Phase { Connecting, Handshake, Writing, Reading, Idle, Closed };
        std::uint64_t id = 0;
        NativeSocket fd = InvalidSocket;
        FEventPtr event;
        SSL* ssl = nullptr;
        Url url;
        Phase phase = Phase::Closed;
        std::shared_ptr<Request> request;
        std::vector<Address> addresses;
        std::size_t addressIndex = 0;
        std::string outbound;
        std::size_t written = 0;
        std::unique_ptr<FHttpResponseParser> parser;
        Clock::time_point idleSince;
    };
    struct Request {
        std::uint64_t id = 0;
        ClientRequest input;
        Url url;
        Callback callback;
        Clock::time_point deadline;
        std::shared_ptr<std::atomic_bool> done = std::make_shared<std::atomic_bool>(false);
        std::atomic_bool cancelled{false};
        bool reserved = false;
        bool active = false;
        std::weak_ptr<Connection> connection;
    };

    FEventLoop& loop;
    ClientOptions options;
    std::atomic_bool accepting{true};
    std::atomic_bool drained{false};
    std::atomic<std::size_t> admitted{0};
    std::atomic<std::size_t> pendingSubmissions{0};
    std::unordered_map<std::uint64_t, std::shared_ptr<Request>> requests;
    std::deque<std::shared_ptr<Request>> waiting;
    std::unordered_map<std::uint64_t, std::shared_ptr<Connection>> connections;
    std::map<std::string, std::size_t> originActive;
    std::size_t active = 0, resolving = 0, callbacksPending = 0;
    TimerID pulseTimer = 0;
    bool stopping = false, pumping = false;
    // Keep asynchronous shutdown alive even when its public facade is destroyed
    // while native DNS is still running. Cleared only after workers are joined.
    std::shared_ptr<State> shutdownKeepAlive;
    SSL_CTX* tlsContext = nullptr;
    bool tlsInitializationFailed = false;
    std::vector<std::function<void()>> drainCallbacks;
    std::mutex resolverMutex;
    std::condition_variable resolverCv;
    std::deque<std::function<void()>> resolverQueue;
    bool resolverStopping = false;
    std::vector<std::thread> resolvers;

    State(FEventLoop& owner, ClientOptions input) : loop(owner), options(std::move(input)) {
        if (!options.maxConcurrent || !options.maxPerOrigin || !options.resolverThreads ||
            !options.maxHeaderBytes || options.idleTimeout.count() < 0 ||
            options.maxQueued > std::numeric_limits<std::size_t>::max() - options.maxConcurrent)
            throw std::invalid_argument("Invalid HTTP client capacity or timeout options");
        try {
            for (std::size_t i = 0; i < options.resolverThreads; ++i) {
                resolvers.emplace_back([this] {
                    for (;;) {
                        std::function<void()> task;
                        {
                            std::unique_lock<std::mutex> lock(resolverMutex);
                            resolverCv.wait(lock, [this] { return resolverStopping || !resolverQueue.empty(); });
                            if (resolverStopping && resolverQueue.empty()) return;
                            task = std::move(resolverQueue.front());
                            resolverQueue.pop_front();
                        }
                        task();
                    }
                });
            }
        } catch (...) {
            stopResolvers();
            for (auto& thread : resolvers) if (thread.joinable()) thread.join();
            throw;
        }
    }

    ~State() {
        stopResolvers();
        for (auto& thread : resolvers) if (thread.joinable()) thread.join();
        // Normal shutdown has already removed these while the Loop was alive.
        for (auto& [id, conn] : connections) destroyTransport(conn);
        if (tlsContext) SSL_CTX_free(tlsContext);
    }

    void stopResolvers() {
        { std::lock_guard<std::mutex> lock(resolverMutex); resolverStopping = true; }
        resolverCv.notify_all();
    }

    bool reserve() {
        auto count = admitted.load();
        const auto capacity = options.maxConcurrent + options.maxQueued;
        while (count < capacity) {
            if (admitted.compare_exchange_weak(count, count + 1)) return true;
        }
        return false;
    }

    static void safelyCall(const std::function<void()>& callback) {
        try { if (callback) callback(); } catch (...) { /* User callbacks cannot terminate the I/O Loop. */ }
    }

    void postResult(const std::shared_ptr<Request>& req, ResponseResult result) {
        ++callbacksPending;
        auto self = shared_from_this();
        loop.AddTask([self, req, result = std::move(result)]() mutable {
            try { if (req->callback) req->callback(std::move(result)); } catch (...) {}
            req->callback = {};
            --self->callbacksPending;
            self->maybeDrained();
        });
    }

    void finish(const std::shared_ptr<Request>& req, ResponseResult result) {
        if (req->done->exchange(true)) return;
        if (req->reserved) { req->reserved = false; --admitted; }
        if (req->active) {
            req->active = false;
            --active;
            auto found = originActive.find(req->url.origin);
            if (found != originActive.end() && --found->second == 0) originActive.erase(found);
        }
        requests.erase(req->id);
        postResult(req, std::move(result));
        pump();
    }

    void fail(std::shared_ptr<Request> req, ErrorCode code, std::string message) {
        if (!req || req->done->load()) return;
        if (auto conn = req->connection.lock()) {
            req->connection.reset();
            closeConnection(conn);
        }
        finish(req, ResponseResult::failure({code, std::move(message)}));
    }

    bool canStart(const std::shared_ptr<Request>& req) const {
        auto found = originActive.find(req->url.origin);
        if (active >= options.maxConcurrent ||
            (found != originActive.end() && found->second >= options.maxPerOrigin)) return false;
        // DNS can outlive a cancelled request. Bound its queued+running work
        // independently so cancellation cannot refill an unbounded resolver queue.
        if (resolving < options.maxConcurrent) return true;
        for (const auto& [id, conn] : connections)
            if (conn->phase == Connection::Phase::Idle && conn->url.origin == req->url.origin) return true;
        return false;
    }

    void enqueue(const std::shared_ptr<Request>& req) {
        loop.isInLoopAssert();
        if (!accepting.load() || stopping) { fail(req, ErrorCode::Stopped, "HTTP client is stopped"); return; }
        if (req->cancelled.load()) { fail(req, ErrorCode::Cancelled, "Request cancelled"); return; }
        if (req->input.timeout.count() <= 0) { fail(req, ErrorCode::InvalidArgument, "HTTP timeout must be positive"); return; }
        if (Clock::now() >= req->deadline) { fail(req, ErrorCode::Timeout, "Request deadline exceeded"); return; }
        if (!parseUrl(req->input.url, req->url) || !token(req->input.method)) {
            fail(req, ErrorCode::InvalidArgument, "Invalid HTTP URL, method, or timeout"); return;
        }
        for (const auto& [name, value] : req->input.headers) {
            std::string normalized = lower(name);
            if (!token(name) || !cleanValue(value) || normalized == "host" || normalized == "content-length" ||
                normalized == "transfer-encoding" || normalized == "connection" || normalized == "upgrade") {
                fail(req, ErrorCode::InvalidArgument, "Invalid or client-managed HTTP request header"); return;
            }
        }
        pump();
        if (!canStart(req) && waiting.size() >= options.maxQueued) {
            fail(req, ErrorCode::QueueFull, "HTTP request queue is full"); return;
        }
        requests.emplace(req->id, req);
        waiting.push_back(req);
        ensurePulse();
        pump();
    }

    void pump() {
        if (pumping || stopping) return;
        pumping = true;
        for (;;) {
            // deque::erase invalidates its end iterator. Remove retired work
            // before saving any iterators used by the selection pass.
            waiting.erase(std::remove_if(waiting.begin(), waiting.end(),
                [](const auto& req) { return req->done->load(); }), waiting.end());
            auto chosen = std::find_if(waiting.begin(), waiting.end(),
                [this](const auto& req) { return canStart(req); });
            if (chosen == waiting.end()) break;
            auto req = *chosen;
            waiting.erase(chosen);
            if (req->cancelled.load()) { fail(req, ErrorCode::Cancelled, "Request cancelled"); continue; }
            if (Clock::now() >= req->deadline) { fail(req, ErrorCode::Timeout, "Request deadline exceeded"); continue; }
            req->active = true;
            ++active;
            ++originActive[req->url.origin];
            std::shared_ptr<Connection> idle;
            for (const auto& [id, conn] : connections)
                if (conn->phase == Connection::Phase::Idle && conn->url.origin == req->url.origin) { idle = conn; break; }
            if (idle) {
                inspectIdle(idle);
                if (idle->phase != Connection::Phase::Idle) idle.reset();
            }
            if (!idle && resolving >= options.maxConcurrent) {
                req->active = false;
                --active;
                auto found = originActive.find(req->url.origin);
                if (found != originActive.end() && --found->second == 0) originActive.erase(found);
                waiting.push_front(req);
                continue;
            }
            if (idle) startRequest(idle, req);
            else startResolve(req);
        }
        pumping = false;
    }

    void startResolve(const std::shared_ptr<Request>& req) {
        ++resolving;
        auto weak = weak_from_this();
        {
            std::lock_guard<std::mutex> lock(resolverMutex);
            resolverQueue.emplace_back([weak, req] {
                std::vector<Address> addresses;
                try { if (!req->done->load()) addresses = resolve(req->url); } catch (...) {}
                if (auto self = weak.lock()) {
                    self->loop.AddTask([self, req, addresses = std::move(addresses)]() mutable {
                        --self->resolving;
                        if (!req->done->load()) {
                            if (req->cancelled.load()) self->fail(req, ErrorCode::Cancelled, "Request cancelled");
                            else if (Clock::now() >= req->deadline) self->fail(req, ErrorCode::Timeout, "Request deadline exceeded");
                            else if (addresses.empty()) self->fail(req, ErrorCode::Dns, "Host resolution failed");
                            else self->connectResolved(req, std::move(addresses));
                        }
                        self->pump();
                        self->maybeDrained();
                    });
                }
            });
        }
        resolverCv.notify_one();
    }

    void connectResolved(const std::shared_ptr<Request>& req, std::vector<Address> addresses) {
        auto conn = std::make_shared<Connection>();
        conn->id = Async::nextOperationId();
        conn->request = req;
        conn->url = req->url;
        conn->addresses = std::move(addresses);
        req->connection = conn;
        connections.emplace(conn->id, conn);
        connectNext(conn);
    }

    void destroyTransport(const std::shared_ptr<Connection>& conn) {
        if (conn->event) { conn->event->remove(); conn->event.reset(); }
        if (conn->ssl) { SSL_free(conn->ssl); conn->ssl = nullptr; }
        closeSocket(conn->fd);
        conn->fd = InvalidSocket;
    }

    void closeConnection(const std::shared_ptr<Connection>& conn) {
        conn->phase = Connection::Phase::Closed;
        destroyTransport(conn);
        conn->request.reset();
        connections.erase(conn->id);
    }

    void connectNext(const std::shared_ptr<Connection>& conn) {
        destroyTransport(conn);
        while (conn->addressIndex < conn->addresses.size()) {
            const auto& address = conn->addresses[conn->addressIndex++];
            conn->fd = ::socket(address.storage.ss_family, SOCK_STREAM, IPPROTO_TCP);
            if (conn->fd == InvalidSocket) continue;
            if (!nonblocking(conn->fd)) { closeSocket(conn->fd); conn->fd = InvalidSocket; continue; }
            int result = ::connect(conn->fd, reinterpret_cast<const sockaddr*>(&address.storage), address.length);
            if (result != 0 && !connectingError(socketError())) {
                closeSocket(conn->fd); conn->fd = InvalidSocket; continue;
            }
            conn->phase = Connection::Phase::Connecting;
            attachEvent(conn);
            if (result == 0) connected(conn);
            return;
        }
        auto req = conn->request;
        closeConnection(conn);
        if (req) finish(req, ResponseResult::failure({ErrorCode::Connect, "TCP connection failed"}));
    }

    void attachEvent(const std::shared_ptr<Connection>& conn) {
        conn->event = FEvent::createEvent(&loop, static_cast<Socket>(conn->fd), loop.getUniqueIdInLoop());
        auto weak = weak_from_this();
        std::weak_ptr<Connection> socket = conn;
        conn->event->setReadCallback([weak, socket] {
            if (auto self = weak.lock()) if (auto conn = socket.lock()) self->ready(conn, true);
        });
        conn->event->setWriteCallback([weak, socket] {
            if (auto self = weak.lock()) if (auto conn = socket.lock()) self->ready(conn, false);
        });
        conn->event->setCloseCallback([weak, socket] {
            if (auto self = weak.lock()) if (auto conn = socket.lock()) self->eof(conn);
        });
        conn->event->setErrorCallback([weak, socket] {
            if (auto self = weak.lock()) if (auto conn = socket.lock()) {
                if (conn->phase == Connection::Phase::Connecting) self->connectNext(conn);
                else if (auto req = conn->request) self->fail(req, ErrorCode::Io, "Socket error");
                else self->closeConnection(conn);
            }
        });
        conn->event->enableReading();
        conn->event->enableWriting();
        conn->event->init();
    }

    void connected(const std::shared_ptr<Connection>& conn) {
        if (!conn->url.tls) { startRequest(conn, conn->request); return; }
        if (!tlsContext && !tlsInitializationFailed) {
            tlsContext = SSL_CTX_new(TLS_client_method());
            if (tlsContext) {
                SSL_CTX_set_verify(tlsContext, SSL_VERIFY_PEER, nullptr);
                int loaded = options.caFile.empty() ? SSL_CTX_set_default_verify_paths(tlsContext)
                                                   : SSL_CTX_load_verify_locations(tlsContext, options.caFile.c_str(), nullptr);
                if (loaded != 1 || SSL_CTX_set_min_proto_version(tlsContext, TLS1_2_VERSION) != 1) {
                    SSL_CTX_free(tlsContext); tlsContext = nullptr;
                }
            }
            if (!tlsContext) tlsInitializationFailed = true;
        }
        if (!tlsContext) { fail(conn->request, ErrorCode::Tls, "Cannot initialize TLS trust store"); return; }
        conn->ssl = SSL_new(tlsContext);
        if (!conn->ssl) { fail(conn->request, ErrorCode::Tls, "Cannot create TLS connection"); return; }
        SSL_set_fd(conn->ssl, static_cast<int>(conn->fd));
        SSL_set_connect_state(conn->ssl);
        SSL_set_mode(conn->ssl, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
        std::array<unsigned char, 16> ip{};
        bool isIp = inet_pton(AF_INET, conn->url.host.c_str(), ip.data()) == 1 ||
                    inet_pton(AF_INET6, conn->url.host.c_str(), ip.data()) == 1;
        int checked = isIp ? X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(conn->ssl), conn->url.host.c_str())
                           : SSL_set1_host(conn->ssl, conn->url.host.c_str());
        if (checked != 1 || (!isIp && SSL_set_tlsext_host_name(conn->ssl, conn->url.host.c_str()) != 1)) {
            fail(conn->request, ErrorCode::Tls, "Cannot configure TLS hostname verification"); return;
        }
        const unsigned char protocols[] = {8, 'h', 't', 't', 'p', '/', '1', '.', '1'};
        SSL_set_alpn_protos(conn->ssl, protocols, sizeof(protocols));
        conn->phase = Connection::Phase::Handshake;
        handshake(conn);
    }

    void interest(const std::shared_ptr<Connection>& conn, bool read, bool write) {
        if (!conn->event) return;
        if (read) conn->event->enableReading(); else conn->event->disableReading();
        if (write) conn->event->enableWriting(); else conn->event->disableWriting();
    }

    void handshake(const std::shared_ptr<Connection>& conn) {
        ERR_clear_error();
        int result = SSL_connect(conn->ssl);
        if (result == 1) {
            if (SSL_get_verify_result(conn->ssl) != X509_V_OK) {
                fail(conn->request, ErrorCode::Tls, "TLS certificate verification failed"); return;
            }
            startRequest(conn, conn->request);
            return;
        }
        int error = SSL_get_error(conn->ssl, result);
        if (error == SSL_ERROR_WANT_READ) interest(conn, true, false);
        else if (error == SSL_ERROR_WANT_WRITE) interest(conn, false, true);
        else fail(conn->request, ErrorCode::Tls, "TLS handshake or certificate verification failed");
    }

    void startRequest(const std::shared_ptr<Connection>& conn, const std::shared_ptr<Request>& req) {
        conn->request = req;
        req->connection = conn;
        conn->parser = std::make_unique<FHttpResponseParser>(req->input.method, req->input.maxBodyBytes, options.maxHeaderBytes);
        conn->outbound = req->input.method + " " + req->url.target + " HTTP/1.1\r\nHost: " + req->url.authority + "\r\n";
        bool hasEncoding = false;
        for (const auto& [name, value] : req->input.headers) {
            conn->outbound += name + ": " + value + "\r\n";
            if (lower(name) == "accept-encoding") hasEncoding = true;
        }
        if (!hasEncoding) conn->outbound += "Accept-Encoding: identity\r\n";
        conn->outbound += std::string("Connection: ") + (req->input.keepAlive ? "keep-alive" : "close") + "\r\n";
        if (!req->input.body.empty() || req->input.method == "POST" || req->input.method == "PUT" || req->input.method == "PATCH")
            conn->outbound += "Content-Length: " + std::to_string(req->input.body.size()) + "\r\n";
        conn->outbound += "\r\n";
        if (conn->outbound.size() > options.maxHeaderBytes) {
            fail(req, ErrorCode::LimitExceeded, "HTTP request headers exceed configured limit"); return;
        }
        conn->outbound += req->input.body;
        conn->written = 0;
        conn->phase = Connection::Phase::Writing;
        interest(conn, false, true);
        write(conn);
    }

    void ready(const std::shared_ptr<Connection>& conn, bool readable) {
        if (conn->phase == Connection::Phase::Closed) return;
        if (conn->request && (conn->request->cancelled.load() || Clock::now() >= conn->request->deadline)) {
            fail(conn->request, conn->request->cancelled.load() ? ErrorCode::Cancelled : ErrorCode::Timeout,
                 "Request cancelled or deadline exceeded"); return;
        }
        switch (conn->phase) {
        case Connection::Phase::Connecting: {
            int error = 0;
            SockLength length = sizeof(error);
            if (getsockopt(conn->fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &length) != 0 || error != 0)
                connectNext(conn);
            else connected(conn);
            break;
        }
        case Connection::Phase::Handshake: handshake(conn); break;
        case Connection::Phase::Writing: write(conn); break;
        case Connection::Phase::Reading: read(conn); break;
        case Connection::Phase::Idle: if (readable) inspectIdle(conn); else interest(conn, true, false); break;
        case Connection::Phase::Closed: break;
        }
    }

    void write(const std::shared_ptr<Connection>& conn) {
        std::size_t budget = 256 * 1024;
        while (conn->written < conn->outbound.size() && budget) {
            int length = static_cast<int>(std::min<std::size_t>({conn->outbound.size() - conn->written, budget, 65536}));
            int count;
            if (conn->ssl) {
                ERR_clear_error();
                count = SSL_write(conn->ssl, conn->outbound.data() + conn->written, length);
                if (count <= 0) {
                    int error = SSL_get_error(conn->ssl, count);
                    if (error == SSL_ERROR_WANT_READ) interest(conn, true, false);
                    else if (error == SSL_ERROR_WANT_WRITE) interest(conn, false, true);
                    else fail(conn->request, ErrorCode::Io, "TLS request write failed");
                    return;
                }
            } else {
#ifdef _WIN32
                count = ::send(conn->fd, conn->outbound.data() + conn->written, length, 0);
#else
                count = static_cast<int>(::send(conn->fd, conn->outbound.data() + conn->written, static_cast<std::size_t>(length), MSG_NOSIGNAL));
#endif
                if (count < 0) {
                    int error = socketError();
#ifndef _WIN32
                    if (error == EINTR) continue;
#endif
                    if (wouldBlock(error)) interest(conn, false, true);
                    else fail(conn->request, ErrorCode::Io, "HTTP request write failed");
                    return;
                }
                if (count == 0) { fail(conn->request, ErrorCode::Io, "HTTP request write made no progress"); return; }
            }
            conn->written += static_cast<std::size_t>(count);
            budget -= static_cast<std::size_t>(count);
        }
        if (conn->written == conn->outbound.size()) {
            conn->outbound.clear();
            conn->phase = Connection::Phase::Reading;
            interest(conn, true, false);
            // TLS may already have decrypted response bytes while finishing a write.
            if (conn->ssl && SSL_pending(conn->ssl) > 0) read(conn);
        } else interest(conn, false, true);
    }

    void read(const std::shared_ptr<Connection>& conn) {
        std::array<char, 16384> buffer{};
        std::size_t budget = 256 * 1024;
        while (budget && conn->phase == Connection::Phase::Reading) {
            int count;
            if (conn->ssl) {
                ERR_clear_error();
                count = SSL_read(conn->ssl, buffer.data(), static_cast<int>(buffer.size()));
                if (count <= 0) {
                    int error = SSL_get_error(conn->ssl, count);
                    if (error == SSL_ERROR_WANT_READ) interest(conn, true, false);
                    else if (error == SSL_ERROR_WANT_WRITE) interest(conn, false, true);
                    else if (error == SSL_ERROR_ZERO_RETURN) eof(conn);
                    else fail(conn->request, ErrorCode::Io, "TLS response read failed");
                    return;
                }
            } else {
                count = static_cast<int>(::recv(conn->fd, buffer.data(), static_cast<int>(buffer.size()), 0));
                if (count == 0) { eof(conn); return; }
                if (count < 0) {
                    int error = socketError();
#ifndef _WIN32
                    if (error == EINTR) continue;
#endif
                    if (wouldBlock(error)) interest(conn, true, false);
                    else fail(conn->request, ErrorCode::Io, "HTTP response read failed");
                    return;
                }
            }
            budget -= std::min<std::size_t>(budget, static_cast<std::size_t>(count));
            conn->parser->feed(std::string_view(buffer.data(), static_cast<std::size_t>(count)));
            if (conn->parser->hasError()) {
                auto error = conn->parser->error();
                fail(conn->request, error.code, std::move(error.message));
                return;
            }
            if (conn->parser->isComplete()) { responseComplete(conn); return; }
        }
        if (conn->ssl && SSL_pending(conn->ssl) > 0 && conn->phase == Connection::Phase::Reading) {
            auto weak = weak_from_this();
            std::weak_ptr<Connection> socket = conn;
            loop.AddTask([weak, socket] {
                if (auto self = weak.lock()) if (auto conn = socket.lock())
                    if (conn->phase == Connection::Phase::Reading) self->read(conn);
            });
        }
    }

    void eof(const std::shared_ptr<Connection>& conn) {
        if (conn->phase == Connection::Phase::Idle || conn->phase == Connection::Phase::Closed) {
            closeConnection(conn); return;
        }
        if (conn->phase == Connection::Phase::Connecting) { connectNext(conn); return; }
        if (conn->phase == Connection::Phase::Reading && conn->parser) {
            conn->parser->finishEof();
            if (conn->parser->isComplete() && !conn->parser->hasError()) { responseComplete(conn); return; }
            auto error = conn->parser->error();
            fail(conn->request, error.code == ErrorCode::None ? ErrorCode::Protocol : error.code,
                 error.message.empty() ? "Truncated HTTP response" : std::move(error.message));
        } else if (conn->request) fail(conn->request, ErrorCode::Io, "Connection closed before response");
        else closeConnection(conn);
    }

    void responseComplete(const std::shared_ptr<Connection>& conn) {
        auto req = conn->request;
        auto response = conn->parser->takeResponse();
        bool reusable = !stopping && req->input.keepAlive && response.keepAlive &&
                        conn->parser->remainingBytes() == 0 && options.maxIdleConnections > 0;
        req->connection.reset();
        conn->request.reset();
        conn->parser.reset();
        if (reusable) {
            conn->phase = Connection::Phase::Idle;
            conn->idleSince = Clock::now();
            interest(conn, true, false);
        } else closeConnection(conn);
        finish(req, ResponseResult::success(std::move(response)));
        trimIdle();
    }

    void inspectIdle(const std::shared_ptr<Connection>& conn) {
        char byte;
        int count;
        if (conn->ssl) {
            ERR_clear_error();
            count = SSL_peek(conn->ssl, &byte, 1);
            if (count <= 0) {
                int error = SSL_get_error(conn->ssl, count);
                if (error == SSL_ERROR_WANT_READ) { interest(conn, true, false); return; }
                if (error == SSL_ERROR_WANT_WRITE) { interest(conn, false, true); return; }
            }
        } else {
            count = static_cast<int>(::recv(conn->fd, &byte, 1, MSG_PEEK));
            if (count < 0 && wouldBlock(socketError())) return;
        }
        // EOF or unsolicited bytes both make an idle connection unusable.
        closeConnection(conn);
    }

    void trimIdle() {
        std::vector<std::shared_ptr<Connection>> idle;
        for (const auto& [id, conn] : connections)
            if (conn->phase == Connection::Phase::Idle) idle.push_back(conn);
        std::sort(idle.begin(), idle.end(), [](const auto& a, const auto& b) { return a->idleSince < b->idleSince; });
        for (std::size_t i = 0; i < idle.size(); ++i)
            if (idle.size() - i > options.maxIdleConnections || Clock::now() - idle[i]->idleSince >= options.idleTimeout)
                closeConnection(idle[i]);
    }

    void ensurePulse() {
        if (pulseTimer || stopping) return;
        auto weak = weak_from_this();
        pulseTimer = loop.RunAfter(20, [weak] {
            if (auto self = weak.lock()) {
                self->pulseTimer = 0;
                std::vector<std::shared_ptr<Request>> expired;
                for (const auto& [id, req] : self->requests)
                    if (req->cancelled.load() || Clock::now() >= req->deadline) expired.push_back(req);
                for (const auto& req : expired)
                    self->fail(req, req->cancelled.load() ? ErrorCode::Cancelled : ErrorCode::Timeout,
                               req->cancelled.load() ? "Request cancelled" : "Request deadline exceeded");
                self->trimIdle();
                if (!self->requests.empty() || !self->connections.empty()) self->ensurePulse();
                self->maybeDrained();
            }
        });
    }

    void stop(std::function<void()> onDrained) {
        loop.isInLoopAssert();
        if (drained.load()) {
            if (onDrained) loop.AddTask([callback = std::move(onDrained)] { safelyCall(callback); });
            return;
        }
        if (!shutdownKeepAlive) shutdownKeepAlive = shared_from_this();
        if (onDrained) drainCallbacks.push_back(std::move(onDrained));
        stopping = true;
        if (pulseTimer) { loop.CancelTimer(pulseTimer); pulseTimer = 0; }
        std::vector<std::shared_ptr<Request>> abandoned;
        for (const auto& [id, req] : requests) abandoned.push_back(req);
        for (const auto& req : abandoned) fail(req, ErrorCode::Stopped, "HTTP client shut down");
        waiting.clear();
        std::vector<std::shared_ptr<Connection>> sockets;
        for (const auto& [id, conn] : connections) sockets.push_back(conn);
        for (const auto& conn : sockets) closeConnection(conn);
        maybeDrained();
    }

    void maybeDrained() {
        if (!stopping || drained.load() || admitted.load() || pendingSubmissions.load() ||
            !requests.empty() || resolving || callbacksPending) return;
        // A DNS worker can still hold a local shared_ptr<State> after its posted
        // completion has run here. Join its short post/unwind tail on this Loop
        // before publishing drained; otherwise dropping the facade in onDrained
        // could make that worker destroy State and attempt to join itself.
        // resolving==0 means every native lookup is already finished and every
        // resolver completion has been consumed, so no DNS wait occurs here.
        stopResolvers();
        for (auto& thread : resolvers) if (thread.joinable()) thread.join();
        drained.store(true);
        auto callbacks = std::move(drainCallbacks);
        if (!callbacks.empty()) loop.AddTask([callbacks = std::move(callbacks)] {
            for (const auto& callback : callbacks) safelyCall(callback);
        });
        shutdownKeepAlive.reset();
    }
};

FHttpClient::FHttpClient(FEventLoop& loop, ClientOptions options)
    : state_(std::make_shared<State>(loop, std::move(options))) {}

FHttpClient::~FHttpClient() {
    // Shutdown remains asynchronous. The documented owner contract keeps the
    // Loop alive until onDrained; callbacks retain State, not the facade object.
    if (state_ && !state_->drained.load()) shutdown();
}

Async::OperationHandle FHttpClient::submit(ClientRequest request, Callback callback) {
    auto self = state_;
    auto req = std::make_shared<State::Request>();
    req->id = Async::nextOperationId();
    const auto now = Clock::now();
    const auto delay = std::max(request.timeout, std::chrono::milliseconds(0));
    const auto maximumDelay = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::time_point::max() - now);
    req->deadline = delay >= maximumDelay ? Clock::time_point::max() : now + delay;
    req->input = std::move(request);
    req->callback = std::move(callback);
    ++self->pendingSubmissions;
    if (self->accepting.load()) req->reserved = self->reserve();
    auto weak = std::weak_ptr<State>(self);
    Async::OperationHandle handle(req->id, [weak, req] {
        req->cancelled.store(true);
        if (auto owner = weak.lock()) owner->loop.AddTask([owner, req] {
            if (!req->done->load()) owner->fail(req, ErrorCode::Cancelled, "Request cancelled");
        });
    }, req->done);
    self->loop.AddTask([self, req] {
        if (!req->reserved) {
            self->fail(req, self->accepting.load() ? ErrorCode::QueueFull : ErrorCode::Stopped,
                       self->accepting.load() ? "HTTP request capacity is full" : "HTTP client is stopped");
        } else self->enqueue(req);
        --self->pendingSubmissions;
        self->maybeDrained();
    });
    return handle;
}

void FHttpClient::shutdown(std::function<void()> onDrained) {
    auto self = state_;
    self->accepting.store(false);
    self->loop.AddTask([self, onDrained = std::move(onDrained)]() mutable { self->stop(std::move(onDrained)); });
}

FEventLoop& FHttpClient::loop() const noexcept { return state_->loop; }
} // namespace Fei::Http
