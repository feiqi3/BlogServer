#include "Model/Posts.h"
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
namespace Blog::DAO {
class PostQuery {
public:
  // Id, Title, Profile, Create Time, Update Time
  static auto QueryPostsDataProfileSinceLastByPageDesc(uint64_t lastId,
                                              int perPageNum = 10)
      -> std::vector<
          std::tuple<uint64_t, std::string, std::string, uint64_t, uint64_t>>;
  static std::optional<Model::Post> QueryPostById(uint64_t postId);
  static std::optional<Model::Post> QueryPostByTitle(const char* postName);
};
} // namespace Blog::DAO