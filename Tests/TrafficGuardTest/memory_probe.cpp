#include "Service/VisitPolicy.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
long rssKiB() {
    std::ifstream file("/proc/self/status");
    for (std::string line; std::getline(file, line);) if (line.starts_with("VmRSS:")) {
        std::istringstream input(line.substr(6));
        long value = 0; input >> value; return value;
    }
    return -1;
}
int main() {
    auto budget = std::make_shared<Blog::VisitPolicy::Budget>();
    Blog::VisitPolicy policy({}, budget);
    std::vector<std::string> ips;
    for (int i = 0; i < 4096; ++i) ips.push_back("10.0." + std::to_string(i / 256) + "." + std::to_string(i % 256));
    auto before = rssKiB();
    for (int i = 0; i < 16384; ++i) {
        auto token = std::to_string(i); token.resize(32, 'a');
        auto result = policy.reserve(ips[i % ips.size()], token, 100, true);
        if (!result.issued) return 1;
        policy.activate(ips[i % ips.size()], token, 100);
    }
    std::cout << "ips=" << budget->ips << " pending=" << budget->visits << " rss_base_kib=" << before
              << " rss_loaded_kib=" << rssKiB() << " delta_kib=" << rssKiB() - before << "\n";
    auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < 100000; ++i) policy.check(ips[i % ips.size()], 110);
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
    std::cout << "100000_existing_ip_checks_us=" << elapsed << "\n";
    policy.sweep(131);
    std::cout << "after_grace pending=" << budget->visits << " rss_kib=" << rssKiB() << "\n";
    policy.sweep(731);
    std::cout << "after_window ips=" << budget->ips << " pending=" << budget->visits << " rss_kib=" << rssKiB() << "\n";
    for (int cycle = 1; cycle <= 5; ++cycle) {
        auto at = cycle * 1000;
        for (int i = 0; i < 16384; ++i) {
            auto token = std::to_string(i); token.resize(32, 'a');
            if (!policy.reserve(ips[i % ips.size()], token, at, true).issued) return 1;
            policy.activate(ips[i % ips.size()], token, at);
        }
        policy.sweep(at + 31);
        policy.sweep(at + 631);
        std::cout << "cycle=" << cycle << " ips=" << budget->ips << " pending=" << budget->visits
                  << " rss_kib=" << rssKiB() << "\n";
    }
}
