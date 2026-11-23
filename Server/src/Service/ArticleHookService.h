#pragma once
#ifdef FEILIB_ENABLE_SCRIPT
#include "Http/FHttpResponse.h"
#include "Http/FHttpRequest.h"
#include "Script/FScriptEngine.h"
#include "Model/Posts.h"
#include <atomic>
#include <optional>
namespace Blog::Script {
class ArticleHookService : public std::enable_shared_from_this<ArticleHookService> {
public:
    static std::shared_ptr<ArticleHookService> create(Fei::FEventLoop&,Fei::FScriptEngine&);
    static std::shared_ptr<ArticleHookService> current(){return current_.load();}
    static bool stopping(){return stopping_.load();}
    ~ArticleHookService();
    void functions(const nlohmann::json& names);
    void saved(uint64_t id,const nlohmann::json& input);
    std::optional<Fei::Http::FHttpResponse> submit(nlohmann::json input);
    std::optional<Fei::Http::FHttpResponse> request(const Fei::Http::FHttpRequest&,Model::Post,
        std::function<Fei::Http::FHttpResponse(Model::Post)> render);
    void stopAccepting(); // Cancel article calls; leave models alive for onStop.
    void shutdown();
private:
    struct State;
    explicit ArticleHookService(std::unique_ptr<State>);
    std::unique_ptr<State> state_;
    inline static std::atomic<std::shared_ptr<ArticleHookService>> current_;
    inline static std::atomic_bool stopping_{false};
};
}
#endif
