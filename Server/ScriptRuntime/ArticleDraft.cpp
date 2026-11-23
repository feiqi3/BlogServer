#include "ArticleDraft.h"
#include "DAO/DataBaseOperation.h"
#include "DAO/QueryPosts.h"
#include "Service/AdminLogin.h"
#include "Service/RssBuilder.h"
#include "Http/FReflect.h"
#include "Utils/HtmlHelper.h"
#include <set>
#include <limits>
#include <stdexcept>
namespace Blog::Script {
namespace {
const std::set<std::string> fields={"title","profile","content","titlepic","category_id","tags","allow_comment","allow_rss","hide"};
const std::set<std::string> viewFields={"title","profile","content","titlepic","allow_comment"};
void validateField(const std::string& key,const ArticleJson& value){
    if(key=="allow_comment"||key=="allow_rss"||key=="hide"){
        if(!value.is_number_integer()||(value!=0&&value!=1))throw std::invalid_argument("Article switch must be 0 or 1");
    }else if(key=="category_id"){modelId(value);}
    else if(!value.is_string()||value.get_ref<const std::string&>().size()>256*1024)throw std::invalid_argument("Invalid article text field");
}
ArticleJson editable(const Model::Post& post){
    auto value=modelPost(post);ArticleJson result=ArticleJson::object();
    for(const auto& key:fields)result[key]=value.at(key);
    result["script_name"]=post.script_name;result["updated_at"]=post.updated_at;return result;
}
void statement(const char* sql){
    auto result=DatabaseOperation::instance()->Exec(sql);
    if(!result||!result->excute())throw std::runtime_error("Article database operation failed");
}
struct Transaction {
    bool committed=false;
    Transaction(){statement("BEGIN IMMEDIATE");}
    ~Transaction(){if(!committed){try{statement("ROLLBACK");}catch(...){}}}
    void commit(){statement("COMMIT");committed=true;}
};
}
uint64_t modelId(const ArticleJson& value){
    if(value.is_string()){
        const auto& text=value.get_ref<const std::string&>();
        if(text.empty()||text.size()>19||text.find_first_not_of("0123456789")!=std::string::npos)throw std::invalid_argument("Invalid model ID");
        auto id=std::stoull(text);
        if(!id||id>static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))throw std::invalid_argument("Invalid model ID");
        return id;
    }
    if(value.is_number_integer()){
        auto id=value.get<int64_t>();if(id>0&&id<=9007199254740991LL)return static_cast<uint64_t>(id);
    }
    throw std::invalid_argument("Model ID must be a positive decimal string or safe integer");
}
ArticleJson modelPost(const Model::Post& post){
    auto result=Fei::Http::FReflect::fromClass(post);
    result["id"]=std::to_string(post.id);result["category_id"]=std::to_string(post.category_id);result["user_id"]=std::to_string(post.user_id);
    result["content"]=html_unescape(post.content);return result;
}
ArticleDraft ArticleDraft::prepare(ArticleJson value){
    if(!value.is_object())throw std::invalid_argument("Expected an article object");
    ArticleDraft draft;draft.input=ArticleJson::object();
    ArticleJson defaults=ArticleJson::object();
    if(value.contains("id")){
        auto id=modelId(value["id"]);auto post=DAO::PostQuery::QueryPostById(id);
        if(!post)throw std::invalid_argument("Article does not exist");
        draft.expected=editable(*post);defaults=modelPost(*post);draft.input["id"]=std::to_string(id);
    }
    for(const auto& key:fields){
        if(value.contains(key))draft.input[key]=value[key];
        else if(defaults.contains(key))draft.input[key]=defaults[key];
        else if(key=="allow_comment")draft.input[key]=1;
        else if(key=="allow_rss"||key=="hide")draft.input[key]=0;
        else draft.input[key]="";
        if(key!="category_id"||draft.input[key]!="")validateField(key,draft.input[key]);
    }
    if(draft.input["category_id"]!="")draft.input["category_id"]=std::to_string(modelId(draft.input["category_id"]));
    draft.input["script_name"]=value.value("script_name",defaults.value("script_name",std::string()));
    if(!draft.input["script_name"].is_string())throw std::invalid_argument("Invalid script association");
    return draft;
}
void ArticleDraft::apply(const ArticleJson& patch){
    if(patch.is_null())return;
    if(!patch.is_object())throw std::invalid_argument("Article hook must return a field patch or null");
    for(const auto& [key,value]:patch.items()){
        if(!fields.contains(key))throw std::invalid_argument("Article field cannot be changed by preprocessing: "+key);
        validateField(key,value);input[key]=key=="category_id"?ArticleJson(std::to_string(modelId(value))):value;
    }
}
ArticleJson ArticleDraft::event()const{
    return {{"draft",input},{"operation",expected?"update":"create"},{"postId",input.value("id",ArticleJson(nullptr))}};
}
uint64_t ArticleDraft::save(const std::function<bool()>& cancelled)const{
    if(cancelled())throw std::runtime_error("Article submission cancelled");
    Transaction transaction;
    if(cancelled())throw std::runtime_error("Article submission cancelled");
    if(expected){
        auto current=DAO::PostQuery::QueryPostById(modelId(input["id"]));
        if(!current||editable(*current)!=*expected)throw std::runtime_error("Article changed during preprocessing; reload and retry");
    }
    std::string error;uint64_t id=0;
    if(!AdminLogin::instance()->postOrModifyBlog(input,error,&id))throw std::runtime_error(error.empty()?"Cannot save article":error);
    if(cancelled())throw std::runtime_error("Article submission cancelled");
    transaction.commit();RssBuilder::instance()->setDirty();return id;
}
Model::Post patchArticleView(Model::Post post,const ArticleJson& patch){
    if(patch.is_null())return post;
    if(!patch.is_object())throw std::invalid_argument("Request hook must return a field patch or null");
    for(const auto& [key,value]:patch.items()){
        if(!viewFields.contains(key))throw std::invalid_argument("Article field is not a presentation field: "+key);
        validateField(key,value);
        if(key=="title")post.title=value.get<std::string>();
        else if(key=="profile")post.profile=value.get<std::string>();
        else if(key=="content")post.content=html_escape(value.get<std::string>());
        else if(key=="titlepic")post.titlepic=value.get<std::string>();
        else post.allow_comment=value.get<int>();
    }
    return post;
}
}
