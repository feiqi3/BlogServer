#include "QueryPosts.h"
#include "Model/Posts.h"
#include "ORM.h"
#include <optional>
#include <threads.h>
#include "Utils/TimeHelper.h"

namespace Blog::DAO {
std::vector<std::tuple<uint64_t, std::string, std::string, uint64_t, uint64_t, std::string,uint64_t>>
PostQuery::QueryPostsDataProfileSinceLastByPageDesc(uint64_t lastId,
                                           int perPageNum) {
  auto getQuery = []() {
    Query q(FIELD(Model::Post, id) - FIELD(Model::Post, title) -
            FIELD(Model::Post, profile) - FIELD(Model::Post, created_at) -
            FIELD(Model::Post, updated_at) - FIELD(Model::Post,tags) - FIELD(Model::Post,category_id));
    q.From("Posts").Where(FIELD(Model::Post, id) > PARAM).limit(PARAM).OrderByDesc(FIELD(Model::Post,id));
    return q;
  };
  thread_local auto query = getQuery();
  return query.exec(lastId,perPageNum).getVector();
}

auto PostQuery::QueryPostDataProfile(uint64_t pageIdx,
    int perPageNum)
-> std::vector<std::tuple<uint64_t, std::string, std::string, uint64_t,
uint64_t, std::string, uint64_t>>{
    auto getQuery = []() {
        Query q(FIELD(Model::Post, id) - FIELD(Model::Post, title) -
                FIELD(Model::Post, profile) - FIELD(Model::Post, created_at) -
                FIELD(Model::Post, updated_at) - FIELD(Model::Post,titlepic) - FIELD(Model::Post,category_id));
        q.From("Posts").skip(PARAM).limit(PARAM).OrderByDesc(FIELD(Model::Post,created_at));
        return q;
      };
      thread_local auto query = getQuery();
      return query.exec(perPageNum,pageIdx * perPageNum).getVector();
}

auto PostQuery::QueryPostsBasicStatusByPage(uint64_t lastId,int perPageNum)      -> std::vector<
std::tuple<uint64_t, std::string, uint64_t, uint64_t>>{
    auto getQuery = []() {
        Query q(FIELD(Model::Post, id) - FIELD(Model::Post, title) -
                 FIELD(Model::Post, created_at) -
                FIELD(Model::Post, updated_at));
        q.From("Posts").Where(FIELD(Model::Post, id) > PARAM).limit(PARAM).OrderByDesc(FIELD(Model::Post,id));
        return q;
      };
    thread_local auto query = getQuery();
  return query.exec(lastId,perPageNum).getVector();
}

std::optional<Model::Post> PostQuery::QueryPostById(uint64_t postId)
{
    auto getQuery = []() {
        Query<Model::Post> query;
        query.Select().Where(FIELD(Model::Post, id) == PARAM);
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

std::optional<Model::Post> PostQuery::QueryPostByTitle(const char* postName)
{
    auto getQuery = []() {
        Query<Model::Post> query;
        query.Where(FIELD(Model::Post, title) == PARAM);
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
        q.From("Posts").Where(FIELD(Model::Post, id) > PARAM && FIELD(Model::Post, category_id) == PARAM ).limit(PARAM).OrderByDesc(FIELD(Model::Post, id));
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
            (FIELD(Model::Post,updated_at) ==PARAM)).Where(FIELD(Model::Post, id) == PARAM);
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
        query.Delete().Where(FIELD(Model::Post, id) == PARAM);
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
        q.From("Posts");
        return q;
        };
    thread_local auto query = getQuery();
    return query.exec().getVector()[0];
}

} // namespace Blog::DAO