#include "QueryPosts.h"
#include "Model/Posts.h"
#include "ORM.h"

namespace Blog::DAO {
std::vector<std::tuple<uint64_t, std::string, std::string, uint64_t, uint64_t>>
PostQuery::QueryPostsDataProfileSinceLastByPageDesc(uint64_t lastId,
                                           int perPageNum) {
  auto getQuery = []() {
    Query q(FIELD(Model::Post, id) - FIELD(Model::Post, title) -
            FIELD(Model::Post, profile) - FIELD(Model::Post, created_at) -
            FIELD(Model::Post, updated_at));
    q.From("Posts").Where(FIELD(Model::Post, id) > PARAM).limit(PARAM).OrderByDesc(FIELD(Model::Post,id));
    return q;
  };
  static auto query = getQuery();
  return query.exec(lastId,perPageNum).getVector();
}

std::optional<Model::Post> PostQuery::QueryPostById(uint64_t postId)
{
    auto getQuery = []() {
        Query<Model::Post> query;
        query.Where(FIELD(Model::Post, id) == PARAM);
        return query;
        };
    static auto query = getQuery();
    query.exec(postId);
    auto vec = query.getVector();
    if (vec.size() > 0) {
        return vec[0];
    }
    return {};
}

std::optional<Model::Post> PostQuery::QueryPostByTitle(const char* postName)
{
    auto getQuery = []() {
        Query<Model::Post> query;
        query.Where(FIELD(Model::Post, title) == PARAM);
        return query;
        };
    static auto query = getQuery();
    query.exec(postName);
    auto vec = query.getVector();
    if (vec.size() > 0) {
        return vec[0];
    }
    return {};
}

} // namespace Blog::DAO