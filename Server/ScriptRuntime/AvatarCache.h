#pragma once
#ifdef FEILIB_ENABLE_SCRIPT
#include "Script/FScriptEngine.h"
#include "Async/FJobPool.h"
#include "Http/FHttpResponse.h"
#include <atomic>
#include <filesystem>

namespace Blog::Script {
// Host-owned storage only. JS owns HTTP downloads, refresh policy and scheduling.
class AvatarCache : public std::enable_shared_from_this<AvatarCache> {
public:
    static std::shared_ptr<AvatarCache> create(Fei::FEventLoop&,const std::string& directory);
    static std::shared_ptr<AvatarCache> current(){return current_.load();}
    void bind(Fei::FScriptEngine&);
    Fei::Http::FHttpResponse image(std::string source,std::string etag);
    void shutdown();
private:
    AvatarCache(Fei::FEventLoop&,const std::string&);
    Fei::ScriptValue lookup(const std::string&);
    Fei::ScriptValue store(const std::string&,const std::string&,Fei::Async::CancellationToken);
    Fei::FEventLoop& loop_;
    std::filesystem::path root_;
    Fei::Async::JobPool files_{{1,64}};
    std::atomic_bool closed_{false};
    inline static std::atomic<std::shared_ptr<AvatarCache>> current_;
};
}
#endif
