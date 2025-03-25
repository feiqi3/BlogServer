#include "CategoriesQuery.h"
std::vector<std::string> Blog::DAO::CategoriesQuery::queryAllCategroyNames()
{
    auto getQuery = []()->auto {
        auto ret = Query(FIELD(Model::Category, name));
        return ret;
        };
    static auto query = getQuery();
    return query.exec().getVector();
}

std::vector<Blog::Model::Category> Blog::DAO::CategoriesQuery::queryAllCategrories()
{
    auto getQuery = []()->auto {
        auto ret = Query<Blog::Model::Category>();
        return ret;
        };
    static auto query = getQuery();
    return query.exec().getVector();
}
