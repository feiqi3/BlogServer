#ifdef FEILIB_ENABLE_SCRIPT
#include "ArticleHookService.h"
#include "ScriptRuntime/ArticleDraft.h"
#include "ScriptRuntime/DatabaseModels.h"
#include "Service/ScriptAdminService.h"
#include "DAO/DataBaseOperation.h"
#include "FEventLoop.h"
#include <future>
#include <mutex>
#include <unordered_map>

namespace Blog::Script {
namespace {
using Json=nlohmann::json;
using Response=Fei::Http::FHttpResponse;
using Status=Fei::Http::StatusCode;
Response error(std::string reason){Response response;response.setStatusCode(Status::_400).setContentType("application/json").setBody(Json({{"result",500},{"msg",std::move(reason)}}).dump());return response;}
bool validGroup(const std::string& name){return !name.empty()&&name.size()<=64&&name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos;}
void report(const std::string& phase,const std::string& error,std::chrono::steady_clock::time_point start){
    if(auto admin=ScriptAdminService::current())admin->record({"blog",phase,error,std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start)});
}
struct Call {
    uint64_t id=Fei::Async::nextOperationId();
    Fei::Http::FHttpResponder reply;
    std::atomic_bool cancelled{false};
    std::mutex mutex;
    std::vector<Fei::Async::OperationHandle> operations;
    void track(Fei::Async::OperationHandle operation){
        std::lock_guard lock(mutex);if(cancelled){operation.cancel();return;}operations.push_back(std::move(operation));
    }
    void cancel(){
        if(cancelled.exchange(true))return;
        std::vector<Fei::Async::OperationHandle> pending;
        {std::lock_guard lock(mutex);pending=operations;}
        for(auto& operation:pending){try{operation.cancel();}catch(...){}}
    }
};
}
struct ArticleHookService::State {
    struct Binding {std::string submit,request;};
    using Catalog=std::unordered_map<std::string,Binding>;
    State(Fei::FEventLoop& owner,Fei::FScriptEngine& runtime):loop(owner),engine(runtime),models(std::make_shared<DatabaseModels>(owner)){}
    Fei::FEventLoop& loop;
    Fei::FScriptEngine& engine;
    std::shared_ptr<DatabaseModels> models;
    std::atomic<std::shared_ptr<const Catalog>> catalog{std::make_shared<const Catalog>()};
    std::mutex mutex,stopMutex;
    std::unordered_map<uint64_t,std::string> associations;
    std::unordered_map<uint64_t,std::shared_ptr<Call>> active;
    bool accepting=true,closed=false;
    std::shared_ptr<Call> begin(Fei::Http::FHttpResponder reply,std::weak_ptr<ArticleHookService> service){
        auto call=std::make_shared<Call>();call->reply=reply;
        {std::lock_guard lock(mutex);if(!accepting){reply.complete(error("Article hooks are stopping"));return {};}active.emplace(call->id,call);}
        reply.onCancel([service,weak=std::weak_ptr<Call>(call)]{
            if(auto call=weak.lock()){
                call->cancel();if(auto self=service.lock()){std::lock_guard lock(self->state_->mutex);self->state_->active.erase(call->id);}
            }
        });
        return call;
    }
    void finish(const std::shared_ptr<Call>& call,Response response){
        {std::lock_guard lock(mutex);active.erase(call->id);}
        if(!call->cancelled)call->reply.complete(std::move(response));
    }
};
ArticleHookService::ArticleHookService(std::unique_ptr<State> state):state_(std::move(state)){}
ArticleHookService::~ArticleHookService(){shutdown();}
std::shared_ptr<ArticleHookService> ArticleHookService::create(Fei::FEventLoop& loop,Fei::FScriptEngine& engine){
    auto service=std::shared_ptr<ArticleHookService>(new ArticleHookService(std::make_unique<State>(loop,engine)));
    service->state_->models->bind(engine);
    auto rows=DatabaseOperation::instance()->Exec("SELECT id, script_name FROM Posts WHERE script_name IS NOT NULL AND script_name != ''");
    while(rows->step())service->state_->associations.emplace(rows->getInteger64(0),rows->getString(1));
    std::shared_ptr<ArticleHookService> empty;
    if(!current_.compare_exchange_strong(empty,service))throw std::logic_error("Article hooks already configured");
    stopping_.store(false);
    return service;
}
void ArticleHookService::functions(const Json& names){
    auto catalog=std::make_shared<State::Catalog>();
    if(names.is_array())for(const auto& value:names){
        if(!value.is_string())continue;auto function=value.get<std::string>();
        auto dot=function.find('.');auto group=dot==std::string::npos?"default":function.substr(0,dot);
        auto hook=dot==std::string::npos?function:function.substr(dot+1);
        if(!validGroup(group)||(dot!=std::string::npos&&group=="default"))continue;
        if(hook=="OnSubmit")(*catalog)[group].submit=function;
        if(hook=="OnRequest")(*catalog)[group].request=function;
    }
    state_->catalog.store(std::move(catalog));
}
void ArticleHookService::saved(uint64_t id,const Json& input){
    if(!id||!input.contains("script_name"))return;
    auto name=input.at("script_name").get<std::string>();std::lock_guard lock(state_->mutex);
    if(name.empty())state_->associations.erase(id);else state_->associations[id]=std::move(name);
}
std::optional<Response> ArticleHookService::submit(Json input){
    auto catalog=state_->catalog.load();std::string name;
    try{
        if(input.contains("script_name"))name=input.at("script_name").get<std::string>();
        else if(input.contains("id")){
            auto id=modelId(input.at("id"));std::lock_guard lock(state_->mutex);
            if(auto it=state_->associations.find(id);it!=state_->associations.end())name=it->second;
        }
        if(name.empty())return {};
        auto binding=catalog->find(name);
        if(binding==catalog->end())return error("Article hook group is unavailable; choose a group or disable hooks");
        if(binding->second.submit.empty())return {};
        input["script_name"]=name;auto function=binding->second.submit;auto self=shared_from_this();
        return Response::defer([self,input=std::move(input),function=std::move(function)](auto reply){
            auto call=self->state_->begin(reply,self);if(!call)return;
            auto started=std::chrono::steady_clock::now();
            call->track(self->state_->models->submit([input](auto)->std::any{
                try{return std::make_pair(std::optional<ArticleDraft>(ArticleDraft::prepare(input)),std::string());}
                catch(const std::exception& error){return std::make_pair(std::optional<ArticleDraft>(),std::string(error.what()));}
            },[self,call,function,started](auto result){
                if(call->cancelled)return;
                if(!result.ok()){self->state_->finish(call,error(result.error.message));return;}
                auto prepared=std::any_cast<std::pair<std::optional<ArticleDraft>,std::string>>(std::move(*result.value));
                if(!prepared.first){self->state_->finish(call,error(prepared.second));return;}
                auto draft=std::make_shared<ArticleDraft>(std::move(*prepared.first));
                call->track(self->state_->engine.invoke("blog",function,draft->event(),[self,call,draft,started](auto processed){
                    if(call->cancelled)return;
                    if(!processed.ok()){report("OnSubmit",processed.error.message,started);self->state_->finish(call,error(processed.error.message));return;}
                    call->track(self->state_->models->submit([call,draft,patch=std::move(processed.value)](auto token)->std::any{
                        try{draft->apply(patch);auto id=draft->save([&]{return token.cancelled()||call->cancelled.load();});return std::make_pair(id,std::string());}
                        catch(const std::exception& error){return std::make_pair(uint64_t(0),std::string(error.what()));}
                    },[self,call,draft,started](auto saved){
                        if(call->cancelled)return;
                        if(!saved.ok()){self->state_->finish(call,error(saved.error.message));return;}
                        auto [id,reason]=std::any_cast<std::pair<uint64_t,std::string>>(std::move(*saved.value));
                        report("OnSubmit",reason,started);
                        if(!id){self->state_->finish(call,error(reason));return;}
                        self->saved(id,draft->input);Response response;
                        response.setContentType("application/json").addHeader("Cache-Control","no-store").setBody(Json({{"result",200},{"id",std::to_string(id)}}).dump());
                        self->state_->finish(call,std::move(response));
                    }));
                },std::chrono::seconds(10)));
            }));
        },std::chrono::seconds(20));
    }catch(const std::exception& exception){return error(exception.what());}
}
std::optional<Response> ArticleHookService::request(const Fei::Http::FHttpRequest& request,Model::Post post,std::function<Response(Model::Post)> render){
    auto catalog=state_->catalog.load();auto binding=catalog->find(post.script_name);
    if(binding==catalog->end()||binding->second.request.empty())return {};
    auto function=binding->second.request;auto self=shared_from_this();
    Json event={{"postId",std::to_string(post.id)},{"post",modelPost(post)},{"request",{{"path",request.getPath()},{"method",Fei::Http::methodToStr(request.getMethod())}}}};
    return Response::defer([self,function,event=std::move(event),post=std::move(post),render=std::move(render)](auto reply){
        auto call=self->state_->begin(reply,self);if(!call)return;auto started=std::chrono::steady_clock::now();
        call->track(self->state_->engine.invoke("blog",function,event,[self,call,post,render,started](auto processed){
            if(call->cancelled)return;
            call->track(self->state_->models->submit([call,post,render,processed=std::move(processed)](auto token)->std::any{
                if(call->cancelled||token.cancelled())throw std::runtime_error("Article request cancelled");
                auto displayed=post;
                auto reason=processed.error.message;
                if(processed.ok()){
                    try{displayed=patchArticleView(post,processed.value);}
                    catch(const std::exception& exception){reason=exception.what();}
                }
                try{
                    auto response=render(std::move(displayed));response.addHeader("Cache-Control","no-store");
                    return std::make_pair(std::move(response),std::move(reason));
                }catch(const std::exception& exception){
                    return std::make_pair(error("Cannot render article"),std::string("Article rendering failed: ")+exception.what());
                }
            },[self,call,started](auto result){
                if(result.ok()){
                    auto [response,reason]=std::any_cast<std::pair<Response,std::string>>(std::move(*result.value));
                    report("OnRequest",reason,started);
                    self->state_->finish(call,std::move(response));
                }else{
                    report("OnRequest",result.error.message,started);
                    self->state_->finish(call,error(result.error.message));
                }
            }));
        },std::chrono::seconds(2)));
    },std::chrono::seconds(10));
}
void ArticleHookService::stopAccepting(){
    std::vector<std::shared_ptr<Call>> active;
    {std::lock_guard lock(state_->mutex);if(!state_->accepting)return;state_->accepting=false;for(auto& [id,call]:state_->active)active.push_back(call);state_->active.clear();}
    auto current=current_.load();if(current.get()==this){stopping_.store(true);current_.store({});}
    for(auto& call:active)call->cancel();
}
void ArticleHookService::shutdown(){
    {std::lock_guard lock(state_->mutex);if(state_->closed)return;}
    if(state_->loop.isInLoopThread())throw std::logic_error("Stop article hooks from the control thread");
    std::lock_guard stopping(state_->stopMutex);
    {std::lock_guard lock(state_->mutex);if(state_->closed)return;}
    stopAccepting();
    state_->models->shutdown();
    // This object is also constructed before server.run(); an unused instance
    // needs no loop barrier. Accepted work always has its own shared lifetime.
    {std::lock_guard lock(state_->mutex);state_->closed=true;}
}
}
#endif
