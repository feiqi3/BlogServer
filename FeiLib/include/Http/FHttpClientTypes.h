#pragma once
#include <chrono>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Fei::Http {
using ClientHeaders = std::vector<std::pair<std::string, std::string>>;

struct ClientRequest {
    std::string url;
    std::string method = "GET";
    ClientHeaders headers;
    std::string body;
    std::chrono::milliseconds timeout{30000};
    std::size_t maxBodyBytes = 4 * 1024 * 1024;
    bool keepAlive = true;
};

struct ClientResponse {
    std::string version;
    int status = 0;
    std::string reason;
    ClientHeaders headers;
    std::string body;
    bool keepAlive = false;
    std::string header(std::string_view name) const {
        for (const auto& [key, value] : headers) {
            if (key.size() != name.size()) continue;
            bool equal = true;
            for (std::size_t i = 0; i < key.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(key[i])) !=
                    std::tolower(static_cast<unsigned char>(name[i]))) { equal = false; break; }
            if (equal) return value;
        }
        return {};
    }
};
} // namespace Fei::Http
