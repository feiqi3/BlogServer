#include "Model/Posts.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace Blog::DAO {
class PostQuery {
public:
  // Id, Title, Profile, Create Time, Update Time, tags, categoryId
  static auto QueryPostsDataProfileSinceLastByPageDesc(uint64_t lastId,
                                                       int perPageNum = 10)
      -> std::vector<std::tuple<uint64_t, std::string, std::string, uint64_t,
                                uint64_t, std::string, uint64_t>>;

  // Id, Title, Create Time, Update Time
  static auto QueryPostsBasicStatusByPage(uint64_t lastId, int perPageNum = 10)
      -> std::vector<std::tuple<uint64_t, std::string, uint64_t, uint64_t>>;

  static std::optional<Model::Post> QueryPostById(uint64_t postId);

  static std::optional<Model::Post> QueryPostByTitle(const char *postName);

  static std::optional<std::string> UpdatePostById(uint64_t postId,
                                                   const Model::Post &post);

  static auto QueryPostsDataWithCategoryProfileSinceLastByPageDesc(
      uint64_t lastId, uint64_t categoryId, int perPageNum = 10)
      -> std::vector<std::tuple<uint64_t, std::string, std::string, uint64_t,
                                uint64_t, std::string>>;

  static std::optional<std::string> InsertPost(const Model::Post &post);

  static std::optional<std::string> DeletePostById(uint64_t postId);
};

} // namespace Blog::DAO