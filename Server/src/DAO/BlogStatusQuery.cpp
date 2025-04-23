#include "BlogStatusQuery.h"
#include "ORM.h"
#include "Model/Status.h"

std::string Blog::DAO::BlogStatusQuery::queryBlogStatus(const std::string& propertyName)
{
    auto getQuery = []()->auto {
        auto ret = Query(FIELD(Model::BlogStatus, data));
        ret.Where(FIELD(Model::BlogStatus, name) == PARAM);
        return ret;
        };
    static auto query = getQuery();
    auto vec = query.exec(propertyName).getVector();
    if (vec.size() == 0) {
        return {};
    }
    return vec[0];
}
