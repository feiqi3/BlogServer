#include "Service/VisitPolicy.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using Blog::VisitPolicy;
void require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
int main() {
    auto budget = std::make_shared<VisitPolicy::Budget>();
    {
        VisitPolicy policy({}, budget);
        for (int i = 0; i < 30; ++i) {
            auto token = std::to_string(i);
            require(policy.reserve("a", token, 100, true).issued, "reserve");
            require(policy.activate("a", token, 100), "activate");
        }
        require(!policy.check("a", 129).blocked, "grace");
        require(policy.confirm("a", "0", 129) == 1, "confirmation");
        require(policy.confirm("a", "0", 130) == 1, "idempotent retry");
        require(!policy.check("a", 130).blocked, "only 29 misses");
        require(policy.confirm("a", "1", 130) == 0, "deadline cannot be rescued by a late report");
        require(policy.reserve("a", "last", 131, true).issued, "next visit");
        policy.activate("a", "last", 131);
        auto banned = policy.check("a", 161);
        require(banned.blocked && banned.retryAfter == 3600, "30 misses ban");
        require(policy.check("a", 162).retryAfter == 3599, "requests cannot extend ban");
        require(!policy.check("a", 3761).blocked, "ban expiration");
    }
    require(budget->ips == 0 && budget->visits == 0 && budget->recent == 0, "generation releases all budgets");
    {
        VisitPolicy policy({}, budget);
        for (int i = 0; i < 29; ++i) {
            auto token = std::to_string(i);
            policy.reserve("a", token, 100, true);
            policy.activate("a", token, 100);
        }
        policy.reserve("a", "later", 700, true);
        policy.activate("a", "later", 700);
        require(!policy.check("a", 730).blocked, "window is measured from missed deadline");
        policy.reserve("b", "cancel", 100, true);
        policy.cancel("b", "cancel");
        policy.sweep(1000);
        require(budget->visits == 0, "failed responses release reservation");
    }
    {
        VisitPolicy policy({}, budget);
        for (int i = 0; i < 64; ++i) require(policy.reserve("a", std::to_string(i), 100, true).issued, "burst reserve");
        require(policy.reserve("a", "overflow", 100, true).blocked, "burst protection before controller");
        require(!policy.check("a", 250).blocked, "unactivated reservation is not a miss");
    }
    {
        VisitPolicy policy({}, budget);
        require(policy.reserve("a", "once", 100, true).issued, "reserve once");
        policy.activate("a", "once", 100);
        require(policy.confirm("b", "once", 101) == 0, "cross-IP rejected");
        require(policy.confirm("a", "once", 101, [] { return false; }) == -1, "commit failure");
        require(policy.confirm("a", "once", 102) == 1, "commit retry");
        budget->maxVisits = 1;
        policy.reserve("a", "one", 103, true);
        require(!policy.reserve("b", "two", 103, true).issued, "global capacity");
        budget->maxVisits = 16384;
    }
    {
        VisitPolicy policy({}, budget);
        auto work = [&](int index) {
            for (int i = 0; i < 100; ++i) {
                auto key = std::to_string(index) + "-" + std::to_string(i);
                require(policy.reserve(std::to_string(index), key, 100, true).issued, "concurrent reservation");
                policy.activate(std::to_string(index), key, 100);
                require(policy.confirm(std::to_string(index), key, 101) == 1, "concurrent confirmation");
            }
        };
        std::thread a(work, 1), b(work, 2), c(work, 3);
        a.join(); b.join(); c.join();
    }
    {
        budget->maxRecent = 1;
        VisitPolicy policy({}, budget);
        require(policy.reserve("a", "slot", 100, true).issued, "acknowledgement slot reservation");
        require(!policy.reserve("b", "overflow", 100, true).issued, "skip tracking before acknowledgement capacity is exhausted");
        policy.activate("a", "slot", 100);
        require(policy.confirm("a", "slot", 101) == 1, "existing valid proof keeps its reserved capacity");
        budget->maxRecent = 16384;
    }
    {
        VisitPolicy policy({}, budget);
        auto started = std::chrono::steady_clock::now();
        for (int i = 0; i < 5000; ++i) {
            auto token = std::to_string(i);
            require(policy.reserve("benchmark", token, 100, true).issued, "sustained reservation");
            policy.activate("benchmark", token, 100);
            require(policy.confirm("benchmark", token, 101) == 1, "sustained confirmation");
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
        require(budget->visits == 0, "successful traffic must release pending capacity immediately");
        policy.sweep(161);
        require(budget->recent == 0 && budget->ips == 0, "retention must release recent and IP capacity");
        std::cout << "Policy-only 5000 reserve/activate/confirm cycles: " << elapsed << " us\n";
    }
    std::cout << "VisitPolicy threshold, boundaries, idempotence, reservation, capacity and concurrency passed\n";
}
