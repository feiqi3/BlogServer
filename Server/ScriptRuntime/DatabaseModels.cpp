#ifdef FEILIB_ENABLE_SCRIPT
#include "DatabaseModels.h"
#include "ArticleDraft.h"
#include "DAO/ORM.h"
#include "DAO/CategoryQuery.h"
#include "DAO/QueryPosts.h"
#include "Http/FReflect.h"
#include <set>
#include <stdexcept>
namespace Blog::Script {
namespace {
using Json=nlohmann::json;
void count(const Json& args,std::size_t expected){if(!args.is_array()||args.size()!=expected)throw std::invalid_argument("Wrong model argument count");}
template<class T> auto rows(Query<T>& query){
    auto values=query.getVector();
    if(auto error=query.getResult()->getErrMsg())throw std::runtime_error(error);
    return values;
}
Json category(uint64_t id){
    Query<Model::Category> query;query.Select().Where(FIELD(Model::Category,id)==PARAM).limit(1).exec(id);
    auto values=rows(query);if(values.empty())return nullptr;
    auto result=Fei::Http::FReflect::fromClass(values[0]);result["id"]=std::to_string(values[0].id);return result;
}
Json post(uint64_t id){
    Query<Model::Post> query;query.Select().Where(FIELD(Model::Post,id)==PARAM).limit(1).exec(id);
    auto values=rows(query);return values.empty()?Json(nullptr):modelPost(values[0]);
}
std::pair<int,int> paging(const Json& args){
    if(!args.is_array()||args.size()>1||(args.size()&&!args[0].is_object()))throw std::invalid_argument("Expected optional model paging options");
    Json options=args.empty()?Json::object():args[0];
    for(const auto& [key,value]:options.items())if((key!="limit"&&key!="offset")||!value.is_number_integer())throw std::invalid_argument("Invalid model paging option");
    auto limit=options.value("limit",20LL),offset=options.value("offset",0LL);
    if(limit<1||limit>100||offset<0||offset>100000)throw std::invalid_argument("Model paging limit exceeded");
    return {static_cast<int>(limit),static_cast<int>(offset)};
}
Json execute(const std::string& operation,const Fei::FScriptNativeCall& call,Fei::Async::CancellationToken token){
    auto cancelled=[&]{return token.cancelled();};
    if(cancelled())throw std::runtime_error("Model operation cancelled");
    const auto& args=call.arguments;
    if(operation=="db.posts.get"){count(args,1);return post(modelId(args[0]));}
    if(operation=="db.posts.findByTitle"){
        count(args,1);auto title=args[0].get<std::string>();
        if(title.empty()||title.size()>512)throw std::invalid_argument("Invalid article title");
        Query<Model::Post> query;query.Select().Where(FIELD(Model::Post,title)==PARAM).limit(1).exec(title);
        auto values=rows(query);return values.empty()?Json(nullptr):modelPost(values[0]);
    }
    if(operation=="db.categories.get"){count(args,1);return category(modelId(args[0]));}
    if(operation=="db.posts.list"){
        auto [limit,offset]=paging(args);
        Query query(FIELD(Model::Post,id)-FIELD(Model::Post,title)-FIELD(Model::Post,profile)-FIELD(Model::Post,category_id)-FIELD(Model::Post,updated_at));
        query.From("Posts").OrderByDesc(FIELD(Model::Post,id)).limit(limit).skip(offset).exec();
        Json result=Json::array();for(const auto& [id,title,profile,categoryId,updated]:rows(query))result.push_back({{"id",std::to_string(id)},{"title",title},{"profile",profile},{"category_id",std::to_string(categoryId)},{"updated_at",updated}});
        return result;
    }
    if(operation=="db.categories.list"){
        auto [limit,offset]=paging(args);Query<Model::Category> query;
        query.Select().OrderByDesc(FIELD(Model::Category,id)).limit(limit).skip(offset).exec();
        Json result=Json::array();for(const auto& value:rows(query)){auto item=Fei::Http::FReflect::fromClass(value);item["id"]=std::to_string(value.id);result.push_back(std::move(item));}return result;
    }
    if(call.hook.empty()||call.hook=="OnSubmit"||call.hook.ends_with(".OnSubmit"))throw std::invalid_argument("Model writes are unavailable during article preprocessing; return a draft patch");
    if(operation=="db.posts.update"){
        count(args,2);auto id=modelId(args[0]);auto existing=post(id);
        if(existing.is_null())throw std::invalid_argument("Article does not exist");
        // Preparing from an ID loads the current row; updates use the same short,
        // optimistic transaction as processed submissions and never emit hooks.
        auto draft=ArticleDraft::prepare({{"id",std::to_string(id)}});draft.apply(args[1]);draft.save(cancelled);return post(id);
    }
    throw std::invalid_argument("Unknown model operation");
}
}
void DatabaseModels::bind(Fei::FScriptEngine& engine){
    auto self=shared_from_this();
    for(const auto* operation:{"db.posts.get","db.posts.findByTitle","db.posts.list","db.posts.update","db.categories.get","db.categories.list"}){
        engine.bindAsyncFunction(operation,[self,operation](auto call,auto completion){return self->dispatch(operation,std::move(call),std::move(completion));});
    }
}
Fei::Async::OperationHandle DatabaseModels::dispatch(std::string operation,Fei::FScriptNativeCall call,Fei::FScriptEngine::Completion callback){
    return submit([operation=std::move(operation),call=std::move(call)](auto token)->std::any{
        try{return Fei::FScriptResult{execute(operation,call,token),{}};}
        catch(const std::exception& error){return Fei::FScriptResult{nullptr,{Fei::Async::ErrorCode::UserException,error.what()}};}
    },[callback=std::move(callback)](auto result){
        if(result.ok())callback(std::any_cast<Fei::FScriptResult>(std::move(*result.value)));
        else callback({nullptr,result.error});
    });
}
}
#endif
