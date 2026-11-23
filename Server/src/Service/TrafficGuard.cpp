#include "TrafficGuard.h"
#include "VisitPolicy.h"
#include "DAO/DataBaseOperation.h"
#include "FLogger.h"
#include "Http/FHttpServer.h"
#include "Http/FHttpRequest.h"
#include "Http/FHttpResponse.h"
#include <nlohmann/json.hpp>
#include <openssl/rand.h>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>

namespace Blog {
namespace {
std::atomic_bool activeConfirmedViews{false};
using namespace Fei::Http;
using Json = nlohmann::json;
std::int64_t steadyNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::int64_t wallNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
void diagnostic(const std::string& text) {
    if (Fei::Logger::valid()) Fei::Logger::instance()->log(Fei::lvl::info, "[TrafficGuard] {}", text);
}
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}
std::string header(const auto& request, std::string_view name) {
    for (const auto& [key, value] : request.headers()) {
        if (std::equal(key.begin(), key.end(), name.begin(), name.end(),
                       [](unsigned char a, char b) { return std::tolower(a) == b; })) return value;
    }
    return {};
}
std::uint32_t address(const std::string& text) {
    if (std::count(text.begin(), text.end(), '.') != 3) throw std::invalid_argument("Invalid IPv4");
    std::uint32_t result = 0;
    std::istringstream input(text);
    for (int i = 0; i < 4; ++i) {
        std::string part;
        if (!std::getline(input, part, '.')) throw std::invalid_argument("Invalid IPv4");
        unsigned int value = 0;
        auto parsed = std::from_chars(part.data(), part.data() + part.size(), value);
        if (part.empty() || parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size() || value > 255)
            throw std::invalid_argument("Invalid IPv4");
        result = (result << 8) | value;
    }
    if (input.rdbuf()->in_avail()) throw std::invalid_argument("Invalid IPv4");
    return result;
}
struct Cidr {
    std::uint32_t network, mask;
    explicit Cidr(const std::string& text) {
        auto slash = text.find('/');
        unsigned int bits = 32;
        if (slash != std::string::npos) {
            auto part = text.substr(slash + 1);
            auto parsed = std::from_chars(part.data(), part.data() + part.size(), bits);
            if (part.empty() || parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size() || bits > 32)
                throw std::invalid_argument("Invalid CIDR");
        }
        mask = bits == 0 ? 0 : (0xffffffffU << (32 - bits));
        network = address(text.substr(0, slash)) & mask;
    }
    bool contains(const std::string& ip) const { return (address(ip) & mask) == network; }
    bool contains(std::uint32_t ip) const { return (ip & mask) == network; }
};
bool matches(const std::vector<Cidr>& ranges, const std::string& ip) {
    return std::any_of(ranges.begin(), ranges.end(), [&](const auto& range) { return range.contains(ip); });
}
bool matches(const std::vector<Cidr>& ranges, std::uint32_t ip) {
    return std::any_of(ranges.begin(), ranges.end(), [&](const auto& range) { return range.contains(ip); });
}
std::uint32_t socketKey(const FHttpRequestHeaders& request) {
    const auto& a = request.getAddrIn().un.un_byte;
    return (std::uint32_t(a.a0) << 24) | (std::uint32_t(a.a1) << 16) |
           (std::uint32_t(a.a2) << 8) | std::uint32_t(a.a3);
}
std::string socketIp(const auto& request) {
    const auto& a = request.getAddrIn().un.un_byte;
    return std::to_string(a.a0) + "." + std::to_string(a.a1) + "." + std::to_string(a.a2) + "." + std::to_string(a.a3);
}
bool digits(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
}
std::string pageKind(const std::string& path) {
    if (path == "/" || (path.starts_with("/page=") && digits(path.substr(6))) ||
        (path.starts_with("/realIndex=") && digits(path.substr(11)))) return "home";
    if (path.starts_with("/post/") && digits(path.substr(6))) return "article";
    if (path == "/categories" || path.starts_with("/category/id=")) return "category";
    if (path == "/archives") return "archive";
    if (path == "/photos") return "photos";
    return {};
}
bool publicApi(const auto& request) {
    if (request.getMethod() != Method::GET) return false;
    const auto& path = request.getPath();
    return path == "/api/archive" || path == "/api/categories" || path == "/api/albums" ||
        path == "/api/photos" || path.starts_with("/api/blog/") || path.starts_with("/api/category/") ||
        path.starts_with("/api/album/") || path.starts_with("/api/albums/") ||
        path.starts_with("/api/photos/") || path.starts_with("/api/photo/");
}
std::string nonce() {
    unsigned char bytes[16];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) return {};
    std::string result;
    constexpr char hex[] = "0123456789abcdef";
    for (auto value : bytes) { result += hex[value >> 4]; result += hex[value & 15]; }
    return result;
}
Json readJson(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > 1024 * 1024) throw std::invalid_argument("Configuration/state file too large");
    std::ifstream input(path);
    return Json::parse(input);
}
std::int64_t number(const Json& object, const char* key, std::int64_t fallback, std::int64_t min, std::int64_t max) {
    auto found = object.find(key);
    if (found == object.end()) return fallback;
    if (!found->is_number_integer()) throw std::invalid_argument(key);
    auto value = found->get<std::int64_t>();
    if (value < min || value > max) throw std::invalid_argument(key);
    return value;
}
struct Rule {
    std::string id;
    std::set<std::string> pages, blockScopes;
    std::shared_ptr<VisitPolicy> policy;
};
struct Generation {
    std::string mode = "off";
    bool confirmedViews = false, logBans = true;
    std::vector<Cidr> allowed, proxies;
    std::string source = "socket", forwarded = "x-forwarded-for";
    std::string banFile;
    std::int64_t flush = 5, created = 0, lifetime = 0;
    double sample = 0.01;
    std::vector<Rule> rules;
};
struct Snapshot {
    std::shared_ptr<Generation> current;
    std::vector<std::shared_ptr<Generation>> generations;
};
struct Visit {
    std::shared_ptr<VisitPolicy> policy;
    std::string ip, token;
    bool activated = false, confirmedViews = false;
    ~Visit() { if (!activated) policy->cancel(ip, token); }
};
std::shared_ptr<Visit> visit(const FHttpRequest& request) {
    return std::static_pointer_cast<Visit>(request.applicationContext());
}
std::string clientIp(const Generation& config, const auto& request) {
    auto peer = socketIp(request);
    if (config.source == "socket" || !matches(config.proxies, peer)) return peer;
    try {
        std::vector<std::string> chain;
        auto raw = header(request, config.forwarded);
        if (raw.size() > 2048) return peer;
        std::istringstream input(raw);
        for (std::string item; std::getline(input, item, ',');) {
            auto begin = item.find_first_not_of(" ");
            auto end = item.find_last_not_of(" ");
            if (begin == std::string::npos) return peer;
            item = item.substr(begin, end - begin + 1);
            address(item);
            chain.push_back(item);
        }
        for (auto it = chain.rbegin(); it != chain.rend() && matches(config.proxies, peer); ++it) peer = *it;
        return peer;
    } catch (...) { diagnostic("Invalid forwarding chain"); return socketIp(request); }
}
FHttpResponse reject(std::int64_t seconds) {
    FHttpResponse response;
    response.setStatusCode(StatusCode::_429).setContentType("text/plain; charset=utf-8")
        .addHeader("Cache-Control", "no-store").addHeader("Retry-After", std::to_string(std::max<std::int64_t>(1, seconds)))
        .setBody("Too many page visits without script confirmation. Please retry later.");
    return response;
}
}
struct TrafficGuard::Impl {
    using FastBans = std::unordered_map<std::uint32_t, std::array<std::int64_t, 2>>;
    using PersistentBans = std::map<std::pair<std::string, std::string>, std::int64_t>;
    std::string root;
    std::function<std::int64_t()> clock;
    std::shared_ptr<VisitPolicy::Budget> budget = std::make_shared<VisitPolicy::Budget>();
    std::atomic<std::shared_ptr<const Snapshot>> snapshot;
    std::atomic<std::shared_ptr<const FastBans>> earlyBans{std::make_shared<FastBans>()};
    std::mutex reloadMutex, workerMutex, maintenanceMutex;
    std::condition_variable wake;
    bool stopping = false;
    std::thread worker;
    std::filesystem::file_time_type modified{};
    std::string lastReloadError;
    std::atomic_uint64_t sampled{0}, skipped{0}, confirmed{0}, rejected{0};
    std::int64_t lastFlush = 0;
    std::string persistedPath;
    PersistentBans persistedBans;
    std::set<std::string> loggedBans;
    Impl(std::string directory, std::function<std::int64_t()> time)
        : root(std::move(directory)), clock(time ? std::move(time) : steadyNow) {
        auto initial = std::make_shared<Snapshot>();
        initial->current = std::make_shared<Generation>();
        snapshot.store(initial);
        reload();
        restore();
        maintain();
        worker = std::thread([this] {
            std::unique_lock lock(workerMutex);
            while (!wake.wait_for(lock, std::chrono::seconds(1), [&] { return stopping; })) {
                lock.unlock();
                try { reload(); maintain(); } catch (const std::exception& error) { diagnostic(error.what()); }
                lock.lock();
            }
            lock.unlock();
            try { maintain(true); } catch (const std::exception& error) { diagnostic(error.what()); }
        });
    }
    ~Impl() {
        { std::lock_guard lock(workerMutex); stopping = true; }
        wake.notify_one();
        worker.join();
    }
    std::shared_ptr<Generation> parse(const Json& json) {
        if (json.at("version") != 1) throw std::invalid_argument("Unsupported traffic configuration version");
        auto config = std::make_shared<Generation>();
        config->mode = json.value("mode", "observe");
        if (config->mode != "off" && config->mode != "observe" && config->mode != "enforce")
            throw std::invalid_argument("Invalid mode");
        config->created = clock();
        const auto ip = json.value("clientIp", Json::object());
        config->source = ip.value("source", "socket");
        config->forwarded = lower(ip.value("forwardedHeader", "X-Forwarded-For"));
        for (const auto& range : ip.value("allowCidrs", Json::array())) config->allowed.emplace_back(range.get<std::string>());
        for (const auto& range : ip.value("trustedProxyCidrs", Json::array())) config->proxies.emplace_back(range.get<std::string>());
        if ((config->source != "socket" && config->source != "forwarded") ||
            (config->source == "forwarded" && (config->proxies.empty() || config->forwarded != "x-forwarded-for")))
            throw std::invalid_argument("Forwarded mode requires trusted proxies and X-Forwarded-For");
        const auto limits = json.value("limits", Json::object());
        auto ips = number(limits, "maxTrackedIps", 4096, 1, 1000000);
        auto visits = number(limits, "maxOutstandingVisits", 16384, 1, 1000000);
        auto recent = number(limits, "maxRecentConfirmations", 16384, 1, 1000000);
        auto retention = number(limits, "confirmationRetentionSeconds", 60, 1, 600);
        std::set<std::string> categories, ids;
        auto rules = json.at("rules");
        if (!rules.is_array() || rules.size() > 16) throw std::invalid_argument("Invalid rules");
        for (const auto& entry : rules) {
            if (!entry.value("enabled", true)) continue;
            Rule rule;
            rule.id = entry.at("id").get<std::string>();
            if (rule.id.empty() || rule.id.size() > 64 || !ids.insert(rule.id).second || entry.value("groupBy", "clientIp") != "clientIp")
                throw std::invalid_argument("Invalid rule identity");
            for (const auto& page : entry.at("pages")) {
                auto name = page.get<std::string>();
                if (name != "home" && name != "article" && name != "archive" && name != "category" && name != "photos")
                    throw std::invalid_argument("Invalid page category");
                if (!categories.insert(name).second) throw std::invalid_argument("Overlapping rules");
                rule.pages.insert(name);
            }
            if (rule.pages.empty()) throw std::invalid_argument("Empty page rule");
            for (const auto& scope : entry.at("blockScopes")) {
                auto name = scope.get<std::string>();
                if (name != "publicPages" && name != "publicApi") throw std::invalid_argument("Invalid block scope");
                rule.blockScopes.insert(name);
            }
            VisitPolicy::Options options;
            options.window = number(entry, "windowSeconds", 600, 60, 86400);
            options.grace = number(entry, "reportGraceSeconds", 30, 1, 300);
            options.ban = number(entry, "banSeconds", 3600, 1, 604800);
            options.threshold = number(entry, "maxUnreported", 30, 1, 10000);
            options.maxPending = number(entry, "maxPendingPerIp", 64, 1, visits);
            options.retention = retention;
            rule.policy = std::make_shared<VisitPolicy>(options, budget);
            config->lifetime = std::max(config->lifetime, options.ban + options.window + 120);
            config->rules.push_back(std::move(rule));
        }
        auto countOn = json.value("articleViews", Json::object()).value("countOn", "pageRequest");
        if (countOn != "pageRequest" && countOn != "confirmedReport") throw std::invalid_argument("Invalid countOn");
        config->confirmedViews = countOn == "confirmedReport";
        if (config->confirmedViews && (config->mode == "off" || !categories.contains("article")))
            throw std::invalid_argument("Confirmed views require active article tracking");
        auto persistence = json.value("persistence", Json::object());
        auto path = std::filesystem::path(persistence.value("banFile", "cache/traffic-guard-bans.json"));
        config->banFile = (path.is_absolute() ? path : std::filesystem::path(root) / path).string();
        config->flush = number(persistence, "flushIntervalSeconds", 5, 1, 60);
        auto logging = json.value("observability", Json::object());
        config->sample = logging.value("requestSampleRate", 0.01);
        config->logBans = logging.value("logBanEvents", true);
        if (!std::isfinite(config->sample) || config->sample < 0 || config->sample > 1) throw std::invalid_argument("Invalid log sample rate");
        budget->maxIps = ips; budget->maxVisits = visits; budget->maxRecent = recent;
        return config;
    }
    void reload() {
        std::lock_guard lock(reloadMutex);
        auto path = std::filesystem::path(root) / "config/traffic-guard.json";
        try {
            auto stamp = std::filesystem::last_write_time(path);
            if (stamp == modified) return;
            modified = stamp;
            auto previous = snapshot.load();
            auto next = std::make_shared<Snapshot>();
            for (auto generation : previous->generations)
                if (clock() <= generation->created + generation->lifetime) next->generations.push_back(generation);
            auto json = readJson(path);
            if (next->generations.size() >= 8 && json.value("mode", "observe") != "off")
                throw std::invalid_argument("Too many live configuration generations");
            next->current = parse(json);
            if (next->current->mode != "off") next->generations.push_back(next->current);
            snapshot.store(next);
            activeConfirmedViews = next->current->confirmedViews;
            modified = stamp;
            diagnostic("Loaded mode=" + next->current->mode);
            lastReloadError.clear();
        } catch (const std::exception& error) {
            if (lastReloadError != error.what()) diagnostic(std::string("Configuration retained: ") + error.what());
            lastReloadError = error.what();
        }
    }
    void restore() {
        auto current = snapshot.load()->current;
        if (current->banFile.empty() || !std::filesystem::exists(current->banFile)) return;
        try {
            auto data = readJson(current->banFile);
            auto restored = std::make_shared<Generation>(*current);
            restored->mode = "enforce";
            for (auto& rule : restored->rules)
                rule.policy = std::make_shared<VisitPolicy>(rule.policy->options(), budget);
            for (const auto& record : data) for (auto& rule : restored->rules)
                if (record.at("rule") == rule.id) {
                    auto ip = record.at("ip").get<std::string>(); address(ip);
                    rule.policy->restoreBan(ip, std::min<std::int64_t>(604800, record.at("until").get<std::int64_t>() - wallNow()), clock());
                }
            auto next = std::make_shared<Snapshot>(*snapshot.load());
            next->generations.push_back(restored);
            snapshot.store(next);
        } catch (const std::exception& error) { diagnostic(std::string("Restore failed: ") + error.what()); }
    }
    void persist(const std::string& path, PersistentBans bans, std::int64_t now) {
        if (path.empty() || (path == persistedPath && bans == persistedBans)) return;
        Json data = Json::array();
        const auto wall = wallNow();
        for (const auto& [key, until] : bans)
            data.push_back({{"rule", key.first}, {"ip", key.second}, {"until", wall + (until - now)}});
        std::filesystem::path target(path);
        std::filesystem::create_directories(target.parent_path());
        auto temporary = target; temporary += ".tmp";
        { std::ofstream out(temporary); out << data.dump(2); out.close(); if (!out) throw std::runtime_error("Ban write failed"); }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Ban replacement failed");
#else
        std::filesystem::rename(temporary, target);
#endif
        persistedPath = path;
        persistedBans = std::move(bans);
    }
    void maintain(bool forceFlush = false) {
        std::lock_guard lock(maintenanceMutex);
        auto state = snapshot.load();
        std::set<std::string> active;
        FastBans fast;
        const auto now = clock();
        const bool flush = forceFlush || now - lastFlush >= state->current->flush;
        PersistentBans pendingWrite;
        VisitPolicy::ActiveBans bans;
        for (const auto& generation : state->generations) for (const auto& rule : generation->rules) {
            const bool enforce = generation->mode == "enforce";
            bans.clear();
            rule.policy->sweep(now, enforce || generation->logBans ? &bans : nullptr);
            for (const auto& [ip, until] : bans) {
                if (enforce) {
                    auto& entry = fast[address(ip)];
                    if (rule.blockScopes.contains("publicPages")) entry[0] = std::max(entry[0], until);
                    if (rule.blockScopes.contains("publicApi")) entry[1] = std::max(entry[1], until);
                    if (flush) {
                        auto& expiry = pendingWrite[{rule.id, ip}];
                        expiry = std::max(expiry, until);
                    }
                }
                if (generation->logBans) {
                    auto key = std::to_string(generation->created) + "/" + rule.id + "/" + ip;
                    if (!loggedBans.contains(key))
                        diagnostic("mode=" + generation->mode + " rule=" + rule.id + " ip=" + ip + " retry=" + std::to_string(until - now));
                    active.insert(std::move(key));
                }
            }
        }
        loggedBans = std::move(active);
        if (fast != *earlyBans.load()) earlyBans.store(std::make_shared<FastBans>(std::move(fast)));
        if (flush) {
            persist(state->current->banFile, std::move(pendingWrite), now);
            lastFlush = now;
        }
    }
};
TrafficGuard::TrafficGuard(std::string root, std::function<std::int64_t()> clock)
    : impl_(std::make_unique<Impl>(std::move(root), std::move(clock))) {}
TrafficGuard::~TrafficGuard() = default;
void TrafficGuard::reload() { impl_->reload(); impl_->maintain(); }
void TrafficGuard::install(FHttpServer& server, const std::string& root) {
    auto guard = std::make_shared<TrafficGuard>(root);
    server.setConnFilterCB([guard](const auto& request, auto& response) { return guard->filter(request, response); });
    server.setHeadersFilterCB([guard](const auto& request, auto& response) { return guard->filterHeaders(request, response); });
    server.setPreSendResponseCB([guard](const auto& request, auto& response) { guard->prepare(request, response); });
}
std::string TrafficGuard::visitId(const FHttpRequest& request) {
    auto context = visit(request);
    return context ? context->token : std::string{};
}
bool TrafficGuard::countOnRequest(const FHttpRequest& request) {
    auto context = visit(request);
    return context ? !context->confirmedViews : !activeConfirmedViews.load();
}
bool TrafficGuard::filterHeaders(const FHttpRequestHeaders& request, FHttpResponse& response) {
    auto config = impl_->snapshot.load()->current;
    if (config->mode != "enforce") return false;
    const auto& path = request.getPath();
    const auto kind = pageKind(path);
    const int scope = !kind.empty() ? 0 : publicApi(request) ? 1 : -1;
    if (scope < 0) return false;
    auto ip = config->source == "socket" ? socketKey(request) : address(clientIp(*config, request));
    if (matches(config->allowed, ip)) return false;
    auto bans = impl_->earlyBans.load();
    auto found = bans->find(ip);
    if (found == bans->end()) return false;
    auto remaining = found->second[scope] - impl_->clock();
    if (remaining <= 0) return false;
    response = reject(remaining);
    ++impl_->rejected;
    return true;
}
bool TrafficGuard::filter(const FHttpRequest& request, FHttpResponse& response) {
    auto snapshot = impl_->snapshot.load();
    auto config = snapshot->current;
    if (request.getPath() == "/_traffic/status" || request.getPath() == "/_traffic/unban") {
        response = FHttpResponse{};
        response.addHeader("Cache-Control", "no-store").setContentType("application/json");
        auto peer = socketIp(request);
        if (!peer.starts_with("127.") || (config->source == "forwarded" && matches(config->proxies, peer))) {
            response.setStatusCode(StatusCode::_403); return true;
        }
        if (request.getPath() == "/_traffic/status" && request.getMethod() == Method::GET) {
            response.setBody(Json({{"mode", config->mode}, {"trackedIps", impl_->budget->ips.load()},
                {"pendingVisits", impl_->budget->visits.load()}, {"recentConfirmations", impl_->budget->recent.load()},
                {"confirmed", impl_->confirmed.load()}, {"rejected", impl_->rejected.load()},
                {"capacitySkipped", impl_->skipped.load()}}).dump());
        } else if (request.getPath() == "/_traffic/unban" && request.getMethod() == Method::POST &&
                   header(request, "x-blog-visit") == "1" && request.getRequestBody().size() <= 15) {
            auto source = std::string(request.getRequestBody());
            try {
                address(source);
                std::lock_guard maintenanceLock(impl_->maintenanceMutex);
                for (auto generation : snapshot->generations) for (auto& rule : generation->rules) rule.policy->unban(source);
                impl_->earlyBans.store(std::make_shared<Impl::FastBans>());
                response.setBody("{\"ok\":true}");
            } catch (...) { response.setStatusCode(StatusCode::_400); }
        } else response.setStatusCode(StatusCode::_405);
        return true;
    }
    if (request.getPath() == "/_traffic/proof") {
        response = FHttpResponse{};
        response.addHeader("Cache-Control", "no-store");
        if (request.getMethod() != Method::POST) {
            response.setStatusCode(StatusCode::_405).addHeader("Allow", "POST"); return true;
        }
        auto body = request.getRequestBody();
        auto origin = lower(header(request, "origin"));
        auto host = lower(header(request, "host"));
        if (host.empty()) host = lower(header(request, ":authority"));
        bool valid = body.size() == 32 && std::all_of(body.begin(), body.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        });
        auto type = lower(header(request, "content-type"));
        if (!valid || header(request, "x-blog-visit") != "1" || !type.starts_with("text/plain") ||
            lower(header(request, "sec-fetch-site")) == "cross-site" ||
            (!origin.empty() && origin != "https://" + host && origin != "http://" + host)) {
            response.setStatusCode(StatusCode::_403); return true;
        }
        for (auto generation : snapshot->generations) {
            auto ip = clientIp(*generation, request);
            for (auto& rule : generation->rules) {
                auto result = rule.policy->confirm(ip, std::string(body), impl_->clock(), [&] {
                    return !generation->confirmedViews || DatabaseOperation::instance()->confirmArticleVisit(std::string(body));
                });
                if (result == 1) { ++impl_->confirmed; response.setStatusCode(StatusCode::_204); return true; }
                if (result < 0) { response.setStatusCode(StatusCode::_503); return true; }
            }
        }
        response.setStatusCode(StatusCode::_403); return true;
    }
    if (config->mode == "off") return false;
    auto ip = clientIp(*config, request);
    if (matches(config->allowed, ip)) return false;
    auto kind = pageKind(request.getPath());
    auto scope = !kind.empty() ? "publicPages" : publicApi(request) ? "publicApi" : "";
    if (scope[0] == '\0') return false;
    std::int64_t retry = 0;
    if (config->mode == "enforce") for (auto generation : snapshot->generations)
        if (generation->mode == "enforce") for (auto& rule : generation->rules)
            if (rule.blockScopes.contains(scope)) retry = std::max(retry, rule.policy->check(ip, impl_->clock()).retryAfter);
    if (retry > 0) { response = reject(retry); ++impl_->rejected; return true; }
    if (request.getMethod() != Method::GET || kind.empty()) return false;
    for (auto& rule : config->rules) if (rule.pages.contains(kind)) {
        auto token = nonce();
        if (token.empty()) return false;
        auto result = rule.policy->reserve(ip, token, impl_->clock(), config->mode == "enforce");
        if (result.blocked && config->mode == "enforce") { response = reject(result.retryAfter); ++impl_->rejected; return true; }
        if (!result.issued) { ++impl_->skipped; return false; }
        auto context = std::make_shared<Visit>();
        context->policy = rule.policy; context->ip = ip; context->token = token; context->confirmedViews = config->confirmedViews;
        request.setApplicationContext(context);
        return false;
    }
    return false;
}
void TrafficGuard::prepare(const FHttpRequest& request, FHttpResponse& response) {
    auto context = visit(request);
    if (context && !context->activated) {
        std::string type;
        response.getHeader("Content-Type", type);
        if (response.getStatusCode() == StatusCode::_200 && (type.empty() || type.starts_with("text/html")) &&
            response.getBody().find(context->token) != std::string::npos) {
            if (context->confirmedViews && pageKind(request.getPath()) == "article") {
                std::uint64_t id = 0;
                auto text = request.getPath().substr(6);
                auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
                if (parsed.ec == std::errc{}) {
                    try { DatabaseOperation::instance()->registerArticleVisit(context->token, id); }
                    catch (const std::exception& error) {
                        diagnostic(error.what());
                        context->policy->cancel(context->ip, context->token);
                        response = FHttpResponse{};
                        response.setStatusCode(StatusCode::_503).setContentType("text/plain").setBody("Visit registration unavailable");
                        return;
                    }
                }
            }
            context->activated = context->policy->activate(context->ip, context->token, impl_->clock());
            response.addHeader("Cache-Control", "private, no-store");
        } else context->policy->cancel(context->ip, context->token);
    }
    auto config = impl_->snapshot.load()->current;
    auto sequence = impl_->sampled.fetch_add(1);
    if (config->sample > 0 && sequence % std::max<std::uint64_t>(1, static_cast<std::uint64_t>(1 / config->sample)) == 0) {
        auto path = request.getPath().substr(0, 4096);
        std::replace(path.begin(), path.end(), '\r', '?'); std::replace(path.begin(), path.end(), '\n', '?');
        diagnostic("unix=" + std::to_string(wallNow()) + " ip=" + clientIp(*config, request) +
            " path=" + path + " status=" + statusCodeToStr2(response.getStatusCode()));
    }
}
}
