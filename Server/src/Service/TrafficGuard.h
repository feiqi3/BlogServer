#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
namespace Fei::Http { class FHttpRequest; class FHttpResponse; class FHttpServer; struct FHttpRequestHeaders; }
namespace Blog {
class TrafficGuard {
public:
    explicit TrafficGuard(std::string resourceDirectory, std::function<std::int64_t()> clock = {});
    ~TrafficGuard();
    static void install(Fei::Http::FHttpServer&, const std::string& resourceDirectory);
    static std::string visitId(const Fei::Http::FHttpRequest&);
    static bool countOnRequest(const Fei::Http::FHttpRequest&);
    bool filter(const Fei::Http::FHttpRequest&, Fei::Http::FHttpResponse&);
    bool filterHeaders(const Fei::Http::FHttpRequestHeaders&, Fei::Http::FHttpResponse&);
    void prepare(const Fei::Http::FHttpRequest&, Fei::Http::FHttpResponse&);
    void reload();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
