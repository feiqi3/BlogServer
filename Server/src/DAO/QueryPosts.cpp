#include "QueryPosts.h"
#include "DAO/DataBaseOperation.h"
#include "Model/Posts.h"
#include "ORM.h"
#include <cstdint>
#include <optional>
#include "Utils/TimeHelper.h"

namespace Blog::DAO {
std::vector<std::tuple<uint64_t, std::string, std::string, uint64_t, uint64_t, std::string,uint64_t>>
PostQuery::QueryPostsDataProfileSinceLastByPageDesc(uint64_t lastId,
                                           int perPageNum) {
  auto getQuery = []() {
    Query q(FIELD(Model::Post, id) - FIELD(Model::Post, title) -
            FIELD(Model::Post, profile) - FIELD(Model::Post, created_at) -
            FIELD(Model::Post, updated_at) - FIELD(Model::Post,tags) - FIELD(Model::Post,category_id));
    q.From("Posts").Where(FIELD(Model::Post, id) > PARAM).limit(PARAM).OrderByDesc(FIELD(Model::Post,id)).setStaticQuery(true);
    return q;
  };
  thread_local auto query = getQuery();
  return query.exec(lastId,perPageNum).getVector();
}

auto PostQuery::QueryPostDataProfile(uint64_t pageIdx,
    int perPageNum)
-> std::vector<std::tuple<uint64_t, std::string, std::string, uint64_t,
uint64_t, std::string, uint64_t, uint64_t>>{
    auto getQuery = []() {
        Query q(FIELD(Model::Post, id) - FIELD(Model::Post, title) -
                FIELD(Model::Post, profile) - FIELD(Model::Post, created_at) -
                FIELD(Model::Post, updated_at) - FIELD(Model::Post,titlepic) - FIELD(Model::Post,category_id) - FIELD(Model::Post, view_times));
        q.From("Posts").skip(PARAM).limit(PARAM).OrderByDesc(FIELD(Model::Post,created_at)).setStaticQuery(true);
        return q;
      };
      thread_local auto query = getQuery();
      return query.exec(perPageNum,pageIdx * perPageNum).getVector();
}

auto PostQuery::QueryPostDataProfileByCategoryId(uint64_t categoryId,uint64_t pageIdx,
    int perPageNum)
-> std::vector<std::tuple<uint64_t, std::string, std::string, uint64_t,
uint64_t, std::string, uint64_t>>{
    auto getQuery = []() {
        Query q(FIELD(Model::Post, id) - FIELD(Model::Post, title) -
                FIELD(Model::Post, profile) - FIELD(Model::Post, created_at) -
                FIELD(Model::Post, updated_at) - FIELD(Model::Post,titlepic) - FIELD(Model::Post, view_times));
        q.From("Posts").Where(FIELD(Model::Post,category_id) == PARAM).skip(PARAM).limit(PARAM).OrderByDesc(FIELD(Model::Post,created_at)).setStaticQuery(true);
        return q;
      };
      thread_local auto query = getQuery();
      return query.exec(categoryId,perPageNum,pageIdx * perPageNum).getVector();
}

auto PostQuery::QueryPostsForArchive()
    -> std::vector<std::tuple<uint64_t, std::string, std::string, std::string,
                              uint64_t, uint64_t>> {
  auto getQuery = []() {
    Query q(FIELD(Model::Post, id) - FIELD(Model::Post, title) -
            FIELD(Model::Post, profile) - FIELD(Model::Post, titlepic) -
            FIELD(Model::Post, created_at) - FIELD(Model::Post, view_times));
    q.From("Posts").OrderByDesc(FIELD(Model::Post, created_at)).setStaticQuery(true);
    return q;
  };
  thread_local auto query = getQuery();
  return query.exec().getVector();
}

auto PostQuery::QueryPostsBasicStatusByPage(uint64_t lastId,int perPageNum)      -> std::vector<
std::tuple<uint64_t, std::string, uint64_t, uint64_t>>{
    auto getQuery = []() {
        Query q(FIELD(Model::Post, id) - FIELD(Model::Post, title) -
                 FIELD(Model::Post, created_at) -
                FIELD(Model::Post, updated_at));
        q.From("Posts").Where(FIELD(Model::Post, id) > PARAM).limit(PARAM).OrderByDesc(FIELD(Model::Post,id)).setStaticQuery(true);
        return q;
      };
    thread_local auto query = getQuery();
  return query.exec(lastId,perPageNum).getVector();
}

std::optional<Model::Post> PostQuery::QueryPostById(uint64_t postId)
{
    auto getQuery = []() {
        Query<Model::Post> query;
        query.Select().Where(FIELD(Model::Post, id) == PARAM).setStaticQuery(true);
        return query;
        };
 thread_local auto query = getQuery();
    query.exec(postId);
    auto vec = query.getVector();
    if (vec.size() > 0) {
        return vec[0];
    }
    return std::nullopt;
}

std::optional<uint64_t> PostQuery::QueryPostIdByTitle(const char *postName)
{
    auto getQuery = []() {
        Query<uint64_t> query(FIELD(Model::Post,id));
        query.Where(FIELD(Model::Post, title) == PARAM).From("Posts").setStaticQuery(true);
        return query;
        };
    thread_local auto q = getQuery();
    auto vec = q.exec(postName).getVector();
    if(vec.empty()){
        return std::nullopt;
    }
    return vec[0];
}

std::optional<Model::Post> PostQuery::QueryPostByTitle(const char* postName)
{
    auto getQuery = []() {
        Query<Model::Post> query;
        query.Where(FIELD(Model::Post, title) == PARAM).setStaticQuery(true);
        return query;
        };
    thread_local auto query = getQuery();
    query.exec(postName);
    auto vec = query.getVector();
    if (vec.size() > 0) {
        return vec[0];
    }
    return std::nullopt;
}

auto PostQuery::QueryPostsDataWithCategoryProfileSinceLastByPageDesc(uint64_t lastId, uint64_t categoryId, int perPageNum) -> std::vector<std::tuple<uint64_t, std::string, std::string, uint64_t, uint64_t, std::string>>
{
    auto getQuery = []() {
        Query q(FIELD(Model::Post, id) - FIELD(Model::Post, title) -
            FIELD(Model::Post, profile) - FIELD(Model::Post, created_at) -
            FIELD(Model::Post, updated_at) - FIELD(Model::Post, tags));
        q.From("Posts").Where(FIELD(Model::Post, id) > PARAM && FIELD(Model::Post, category_id) == PARAM ).limit(PARAM).OrderByDesc(FIELD(Model::Post, id)).setStaticQuery(true);
        return q;
        };
    thread_local auto query = getQuery();
    return query.exec(lastId,categoryId, perPageNum).getVector();
}

std::optional<std::string> PostQuery::InsertPost(const Model::Post& post){
    Query<Model::Post> query;
    query.InsertWithAutoIncPk(post);
    auto ptr = query.exec(post).getResult();
    bool res = ptr->excute();
    if(res)return std::nullopt;
    return ptr->getErrMsg();
}

std::optional<std::string> PostQuery::UpdatePostById(uint64_t postId,
    const Model::Post &post){
    auto getQuery = []() {
        Query<Model::Post> query;
        query.update(
            (FIELD(Model::Post, title) == PARAM) -
            (FIELD(Model::Post,category_id) == PARAM) -
            (FIELD(Model::Post, profile) == PARAM) -
            (FIELD(Model::Post, titlepic) == PARAM) -
            (FIELD(Model::Post, content) == PARAM) -
            (FIELD(Model::Post,updated_at) ==PARAM)).Where(FIELD(Model::Post, id) == PARAM).setStaticQuery(true);
        return query;
        };
    thread_local auto query = getQuery();
    auto updateTime = TimeHelper::getCurrentTimeFromEpochMills();
    auto ptr = query.exec(post.title, post.category_id, post.profile, post.titlepic, post.content,updateTime,postId).getResult();
    bool res = ptr->excute();
    if(res)return std::nullopt;
    return ptr->getErrMsg();
}

std::optional<std::string> PostQuery::DeletePostById(uint64_t postId){
    auto getQuery = []() {
        Query<Model::Post> query;
        query.Delete().Where(FIELD(Model::Post, id) == PARAM).setStaticQuery(true);
        return query;
        };
    thread_local auto q = getQuery();
    auto ptr = q.exec(postId).getResult();
    bool res = ptr->excute();
    if(res)return std::nullopt;
    return ptr->getErrMsg();
}

int PostQuery::QueryPostCount(){
    auto getQuery = []() {
        Query<int> q;
        q.Select(SelectCount());
        q.From("Posts").setStaticQuery(true);
        return q;
        };
    thread_local auto query = getQuery();
    return query.exec().getVector()[0];
}

int PostQuery::QueryPostOfCategoryCount(uint64_t id){
    auto getQuery = []() {
        Query<int> q;
        q.Select(SelectCount());
        q.From("Posts").Where(FIELD(Model::Post, category_id) == PARAM).setStaticQuery(true);
        return q;
        };
    thread_local auto query = getQuery();
    return query.exec(id).getVector()[0];
}


void PostQuery::updateViewTimes(uint64_t id){
    auto getQueryStatement = [](){
        auto ret = DatabaseOperation::instance()->Prepare("UPDATE Posts SET view_times = view_times + 1 where id = ?");
        DatabaseOperation::addThreadCleanDBData(ret);
        return ret;
    }
    ;
    thread_local auto q = getQueryStatement();
    DatabaseOperation::instance()->ReExec(q, id);
    q->excute();
}

} // namespace Blog::DAO