#include "BlogStatusQuery.h"
#include "ORM.h"
#include "Model/Status.h"

std::string Blog::BlogStatusQuery::queryBlogStatus(const std::string& propertyName)
{
    auto getQuery = []()->auto {
        auto ret = Query(FIELD(Model::Status, data));
        ret.Where(FIELD(Model::Status, name) == PARAM);
        return ret;
        };
    static auto query = getQuery();
    auto vec = query.exec(propertyName).getVector();
    if (vec.size() == 0) {
        return {};
    }
    return vec[0];
}
