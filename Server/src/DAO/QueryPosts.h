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

  // Id, Title, Profile, Create Time, Update Time, titlePic, categoryId, view_times
  static auto QueryPostDataProfile(uint64_t pageIdx,
    int perPageNum = 10)
-> std::vector<std::tuple<uint64_t, std::string, std::string, uint64_t,
uint64_t, std::string, uint64_t, uint64_t>>;

  // Id, Title, Profile, Create Time, Update Time, titlePic, view_times
  static auto QueryPostDataProfileByCategoryId(uint64_t categoryId,uint64_t pageIdx,
    int perPageNum = 10)
-> std::vector<std::tuple<uint64_t, std::string, std::string, uint64_t,
uint64_t, std::string, uint64_t>>;

  // Returns all posts (id, title, profile, titlepic, created_at, view_times) ordered by created_at DESC.
  static auto QueryPostsForArchive()
      -> std::vector<std::tuple<uint64_t, std::string, std::string, std::string,
                                uint64_t, uint64_t>>;


  // Id, Title, Create Time, Update Time
  static auto QueryPostsBasicStatusByPage(uint64_t lastId, int perPageNum = 10)
      -> std::vector<std::tuple<uint64_t, std::string, uint64_t, uint64_t>>;

  static std::optional<Model::Post> QueryPostById(uint64_t postId);

  static std::optional<Model::Post> QueryPostByTitle(const char *postName);

  static std::optional<uint64_t> QueryPostIdByTitle(const char *postName);

  static std::optional<std::string> UpdatePostById(uint64_t postId,
                                                   const Model::Post &post);

  static auto QueryPostsDataWithCategoryProfileSinceLastByPageDesc(
      uint64_t lastId, uint64_t categoryId, int perPageNum = 10)
      -> std::vector<std::tuple<uint64_t, std::string, std::string, uint64_t,
                                uint64_t, std::string>>;

  static std::optional<std::string> InsertPost(const Model::Post &post);

  static std::optional<std::string> DeletePostById(uint64_t postId);

  static void updateViewTimes(uint64_t id);

  static int QueryPostCount();
  static int QueryPostOfCategoryCount(uint64_t id);

  // Id, Title, Profile, Create Time, CategoryId — RSS feed source (allow_rss == 1)
  static auto QueryPostsForRss()
      -> std::vector<std::tuple<uint64_t, std::string, std::string, uint64_t,
                                uint64_t>>;
};

} // namespace Blog::DAO