#include "CategoryQuery.h"
#include "DAO/ORM.h"
#include "Model/Categories.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Blog::DAO {
std::optional<Model::Category>
CategoryQuery::QueryCategoryById(uint64_t categoryId) {
  auto getQuery = []() {
    Query<Model::Category> query;
    query.Select().Where(FIELD(Model::Category, id) == PARAM);
    return query;
  };
  thread_local auto query = getQuery();
  query.exec(categoryId);
  auto vec = query.getVector();
  if (vec.size() > 0) {
    return vec[0];
  }
  return std::nullopt;
}

std::optional<std::string>CategoryQuery::QueryCategoryNameById(uint64_t categoryId){
    auto getQuery = []() {
      Query query(FIELD(Model::Category, name));
      query.Where(FIELD(Model::Category, id) == PARAM).From("Categories");
      return query;
    };
    thread_local auto query = getQuery();
    query.exec(categoryId);
    auto vec = query.getVector();
    if (vec.size() > 0) {
      return vec[0];
    }
    return std::nullopt;
}

std::optional<std::string>
CategoryQuery::InsertCategory(const Model::Category &category) {
  Query<Model::Category> query;
  query.InsertWithAutoIncPk(category);
  auto ptr = query.exec(category).getResult();
  bool res = ptr->excute();
  if (res)
    return std::nullopt;
  return ptr->getErrMsg();
}

std::optional<std::string>
CategoryQuery::UpdateCategory(const Model::Category &category) {
  auto getQuery = []() {
    Query<Model::Category> query;
    query
        .update((FIELD(Model::Category, name) == PARAM) -
                (FIELD(Model::Category, categorypic) == PARAM))
        .Where(FIELD(Model::Category, id) == PARAM);
    return query;
  };
  thread_local auto query = getQuery();
  auto ptr =
      query.exec(category.name, category.categorypic, category.id).getResult();
  bool res = ptr->excute();
  if (res)
    return std::nullopt;
  return ptr->getErrMsg();
}

std::optional<std::string> CategoryQuery::DeleteCategory(uint64_t categoryId) {
  auto getQuery = []() {
    Query<Model::Category> query;
    query.Delete().Where(FIELD(Model::Category, id) == PARAM);
    return query;
  };
  thread_local auto query = getQuery();
  auto ptr = query.exec(categoryId).getResult();
  bool res = ptr->excute();
  if (res)
    return std::nullopt;
  return ptr->getErrMsg();
}

// id, name
std::tuple<uint64_t, std::string>
CategoryQuery::QueryCategoryBasicInfoById(uint64_t categoryId) {
  auto getQuery = []() {
    Query query(FIELD(Model::Category, id) - FIELD(Model::Category, name));
    query.Where(FIELD(Model::Category,id) == PARAM);
    return query;
  };
  thread_local auto q = getQuery();
  auto ret = q.exec(categoryId).getVector()[0];
  return ret;
}

std::vector<std::tuple<uint64_t, std::string>>
CategoryQuery::QueryCategoryBasicInfo(){
  auto getQuery =[](){
    Query q(FIELD(Model::Category, id) - FIELD(Model::Category, name));
    return q;
  };
  thread_local auto query = getQuery();
  return query.exec().getVector();
}

// id, name
std::vector<std::tuple<uint64_t, std::string>>
CategoryQuery::QueryAllCategoryBasicInfo ( ) {
  auto getQuery = []() {
    Query query(FIELD(Model::Category, id) - FIELD(Model::Category, name));
    query.From("Categories");
    return query;
  };
  thread_local auto q = getQuery();
  auto ret = q.exec().getVector();
  return ret;
}
} // namespace Blog::DAO