#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Blog {
class VisitPolicy {
public:
    struct Budget {
        std::atomic_size_t ips{0}, visits{0}, recent{0};
        std::atomic_size_t maxIps{4096}, maxVisits{16384}, maxRecent{16384};
    };
    struct Options {
        std::int64_t window = 600, grace = 30, ban = 3600, retention = 60;
        std::size_t threshold = 30, maxPending = 64;
    };
    struct Decision {
        bool blocked = false, newlyBanned = false, issued = false;
        std::int64_t retryAfter = 0;
        std::size_t unreported = 0;
    };
    VisitPolicy(Options options, std::shared_ptr<Budget> budget);
    ~VisitPolicy();
    Decision check(const std::string& ip, std::int64_t now);
    Decision reserve(const std::string& ip, const std::string& token, std::int64_t now, bool enforce);
    bool activate(const std::string& ip, const std::string& token, std::int64_t now);
    void cancel(const std::string& ip, const std::string& token);
    // 1 accepted, 0 invalid/expired, -1 storage callback failed.
    int confirm(const std::string& ip, const std::string& token, std::int64_t now,
                const std::function<bool()>& commit = {});
    using ActiveBans = std::vector<std::pair<std::string, std::int64_t>>;
    // Appends IPs and absolute monotonic expiration times when active is supplied.
    void sweep(std::int64_t now, ActiveBans* active = nullptr);
    void restoreBan(const std::string& ip, std::int64_t remaining, std::int64_t now);
    void unban(const std::string& ip);
    Options options() const { return options_; }
private:
    struct Pending { bool active; std::multimap<std::int64_t, std::string>::iterator expiry; };
    struct State {
        std::unordered_map<std::string, Pending> pending;
        std::multimap<std::int64_t, std::string> expiry;
        std::unordered_map<std::string, std::int64_t> recent;
        std::deque<std::pair<std::int64_t, std::string>> recentExpiry;
        std::deque<std::int64_t> misses;
        std::int64_t bannedUntil = 0;
    };
    struct Shard { std::mutex mutex; std::unordered_map<std::string, State> states; };
    Shard& shard(const std::string& ip);
    Decision advance(State& state, std::int64_t now);
    Options options_;
    std::shared_ptr<Budget> budget_;
    std::array<Shard, 16> shards_;
};
}
