#include "VisitPolicy.h"
#include <algorithm>
#include <memory>
#include <stdexcept>

namespace Blog {
namespace {
bool acquire(std::atomic_size_t& count, const std::atomic_size_t& limit) {
    auto current = count.load();
    while (current < limit.load()) {
        if (count.compare_exchange_weak(current, current + 1)) return true;
    }
    return false;
}
}
VisitPolicy::VisitPolicy(Options options, std::shared_ptr<Budget> budget)
    : options_(options), budget_(std::move(budget)) {
    if (options.grace <= 0 || options.window <= options.grace || options.ban <= 0 ||
        options.threshold == 0 || options.maxPending < options.threshold)
        throw std::invalid_argument("Invalid visit policy");
}
VisitPolicy::~VisitPolicy() {
    for (auto& part : shards_) for (auto& [ip, state] : part.states) {
        --budget_->ips;
        budget_->visits -= state.pending.size();
        budget_->recent -= state.recent.size() + state.pending.size();
    }
}
VisitPolicy::Shard& VisitPolicy::shard(const std::string& ip) {
    return shards_[std::hash<std::string>{}(ip) % shards_.size()];
}
void VisitPolicy::unban(const std::string& ip) {
    auto& part = shard(ip);
    std::lock_guard lock(part.mutex);
    auto found = part.states.find(ip);
    if (found == part.states.end()) return;
    auto& state = found->second;
    budget_->visits -= state.pending.size();
    budget_->recent -= state.pending.size();
    state.pending.clear();
    state.expiry.clear();
    state.misses.clear();
    state.bannedUntil = 0;
}
VisitPolicy::Decision VisitPolicy::advance(State& state, std::int64_t now) {
    bool started = false;
    while (!state.recentExpiry.empty() && state.recentExpiry.front().first <= now) {
        budget_->recent -= state.recent.erase(state.recentExpiry.front().second);
        state.recentExpiry.pop_front();
    }
    while (!state.expiry.empty() && state.expiry.begin()->first <= now) {
        auto event = state.expiry.begin();
        const auto at = event->first;
        auto visit = state.pending.find(event->second);
        if (visit->second.active && at >= state.bannedUntil) {
            while (!state.misses.empty() && state.misses.front() <= at - options_.window)
                state.misses.pop_front();
            state.misses.push_back(at);
            if (state.misses.size() >= options_.threshold) {
                state.bannedUntil = at + options_.ban;
                started = true;
                state.misses.clear();
                budget_->visits -= state.pending.size();
                budget_->recent -= state.pending.size();
                state.pending.clear();
                state.expiry.clear();
                break;
            }
        }
        state.pending.erase(visit);
        state.expiry.erase(event);
        --budget_->visits;
        --budget_->recent;
    }
    while (!state.misses.empty() && state.misses.front() <= now - options_.window)
        state.misses.pop_front();
    return {state.bannedUntil > now, started, false, std::max<std::int64_t>(0, state.bannedUntil - now), state.misses.size()};
}
VisitPolicy::Decision VisitPolicy::check(const std::string& ip, std::int64_t now) {
    auto& part = shard(ip);
    std::lock_guard lock(part.mutex);
    auto found = part.states.find(ip);
    return found == part.states.end() ? Decision{} : advance(found->second, now);
}
VisitPolicy::Decision VisitPolicy::reserve(const std::string& ip, const std::string& token, std::int64_t now, bool enforce) {
    auto& part = shard(ip);
    std::lock_guard lock(part.mutex);
    auto found = part.states.find(ip);
    if (found == part.states.end()) {
        if (!acquire(budget_->ips, budget_->maxIps)) return {};
        found = part.states.try_emplace(ip).first;
    }
    auto& state = found->second;
    auto result = advance(state, now);
    if (enforce && result.blocked) return result;
    if (state.pending.size() >= options_.maxPending) {
        result.blocked = true;
        result.retryAfter = std::max<std::int64_t>(1, state.expiry.begin()->first - now);
        return result;
    }
    if (!acquire(budget_->visits, budget_->maxVisits)) return result;
    // Reserve acknowledgement retention too; capacity exhaustion must not reject a valid proof later.
    if (!acquire(budget_->recent, budget_->maxRecent)) {
        --budget_->visits;
        return result;
    }
    auto expiry = state.expiry.emplace(now + 120, token); // Reserved render deadline, not a missed visit.
    state.pending.emplace(token, Pending{false, expiry});
    result.issued = true;
    return result;
}
bool VisitPolicy::activate(const std::string& ip, const std::string& token, std::int64_t now) {
    auto& part = shard(ip);
    std::lock_guard lock(part.mutex);
    auto found = part.states.find(ip);
    if (found == part.states.end()) return false;
    auto& state = found->second;
    auto visit = state.pending.find(token);
    if (visit == state.pending.end()) return false;
    state.expiry.erase(visit->second.expiry);
    visit->second = {true, state.expiry.emplace(now + options_.grace, token)};
    return true;
}
void VisitPolicy::cancel(const std::string& ip, const std::string& token) {
    auto& part = shard(ip);
    std::lock_guard lock(part.mutex);
    auto found = part.states.find(ip);
    if (found == part.states.end()) return;
    auto& state = found->second;
    auto visit = state.pending.find(token);
    if (visit == state.pending.end()) return;
    state.expiry.erase(visit->second.expiry);
    state.pending.erase(visit);
    --budget_->visits;
    --budget_->recent;
}
int VisitPolicy::confirm(const std::string& ip, const std::string& token, std::int64_t now,
                         const std::function<bool()>& commit) {
    auto& part = shard(ip);
    std::lock_guard lock(part.mutex);
    auto found = part.states.find(ip);
    if (found == part.states.end()) return 0;
    auto& state = found->second;
    advance(state, now);
    if (state.recent.contains(token)) return 1;
    auto visit = state.pending.find(token);
    if (visit == state.pending.end() || !visit->second.active) return 0;
    try {
        if (commit && !commit()) return -1;
    } catch (...) { return -1; }
    state.recent[token] = now + options_.retention;
    state.recentExpiry.emplace_back(now + options_.retention, token);
    state.expiry.erase(visit->second.expiry);
    state.pending.erase(visit);
    --budget_->visits;
    return 1;
}
void VisitPolicy::sweep(std::int64_t now, ActiveBans* active) {
    for (auto& part : shards_) {
        std::lock_guard lock(part.mutex);
        for (auto it = part.states.begin(); it != part.states.end();) {
            advance(it->second, now);
            auto& state = it->second;
            if (state.pending.empty() && state.recent.empty() && state.misses.empty() && state.bannedUntil <= now) {
                it = part.states.erase(it);
                --budget_->ips;
            } else {
                if (active && state.bannedUntil > now) active->emplace_back(it->first, state.bannedUntil);
                ++it;
            }
        }
    }
}
void VisitPolicy::restoreBan(const std::string& ip, std::int64_t remaining, std::int64_t now) {
    if (remaining <= 0) return;
    auto& part = shard(ip);
    std::lock_guard lock(part.mutex);
    if (!part.states.contains(ip)) {
        if (!acquire(budget_->ips, budget_->maxIps)) return;
        part.states.try_emplace(ip);
    }
    auto& state = part.states.at(ip);
    state.bannedUntil = std::max(state.bannedUntil, now + remaining);
}
}
