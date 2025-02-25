#include "QueryPosts.h"
#include "Model/Posts.h"
#include "ORM.h"

namespace Blog::DAO {
std::vector<std::tuple<uint64_t, std::string, std::string, uint64_t, uint64_t>>
PostQuery::QueryPostsDataProfileSinceLastByPageDesc(uint64_t lastId,
                                           int perPageNum) {
  auto getQuery = [perPageNum]() {
    Query q(FIELD(Model::Post, id) - FIELD(Model::Post, title) -
            FIELD(Model::Post, profile) - FIELD(Model::Post, created_at) -
            FIELD(Model::Post, updated_at));
    q.From("Posts").Where(FIELD(Model::Post, id) > PARAM).limit(perPageNum).OrderByDesc(FIELD(Model::Post,id));
    return q;
  };
  static auto query = getQuery();
  return query.exec(lastId).getVector();
}

} // namespace Blog::DAO