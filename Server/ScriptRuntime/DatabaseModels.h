#pragma once
#ifdef FEILIB_ENABLE_SCRIPT
#include "Script/FScriptEngine.h"
#include "Async/FJobPool.h"
#include "DAO/DataBaseOperation.h"
#include <memory>
namespace Blog::Script {
class DatabaseModels : public std::enable_shared_from_this<DatabaseModels> {
public:
    explicit DatabaseModels(Fei::FEventLoop& loop):loop_(loop),workers_({2,128}){}
    void bind(Fei::FScriptEngine& engine);
    Fei::Async::OperationHandle submit(Fei::Async::JobPool::Work work,Fei::Async::JobPool::Completion completion){
        return workers_.submit([work=std::move(work)](auto token){
            // WAL + busy_timeout can arbitrate independent writers. Shared-cache
            // table locks return SQLITE_LOCKED without invoking the busy handler.
            DatabaseOperation::instance()->usePrivateCacheForCurrentThread();
            return work(token);
        },std::move(completion),&loop_);
    }
    void shutdown(){workers_.shutdown();}
private:
    Fei::Async::OperationHandle dispatch(std::string operation,Fei::FScriptNativeCall call,Fei::FScriptEngine::Completion callback);
    Fei::FEventLoop& loop_;
    Fei::Async::JobPool workers_;
};
}
#endif
