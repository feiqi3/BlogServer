#include "Model/Categories.h"
#include <cstdint>
#include <vector>
#include <optional>
namespace Blog::DAO {
class CategoryQuery {
public:
  static std::optional<Model::Category> QueryCategoryById(uint64_t categoryId);
  static std::optional<std::string>  InsertCategory(const Model::Category &category);
  static std::optional<std::string>  UpdateCategory(const Model::Category &category);
  static std::optional<std::string>  DeleteCategory(uint64_t categoryId);
  //id, name
  static std::tuple<uint64_t, std::string>
  QueryCategoryBasicInfoById(uint64_t categoryId);

  //id, name
  static std::vector<std::tuple<uint64_t, std::string>>
  QueryCategoryBasicInfo();

    //id, name
    static std::vector<std::tuple<uint64_t, std::string>>
    QueryAllCategoryBasicInfo( );
};
} // namespace Blog::DAO