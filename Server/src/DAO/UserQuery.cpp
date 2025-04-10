#include "UserQuery.h"
#include "DAO/ORM.h"
#include "Model/Users.h"
namespace Blog::DAO{
   bool UserQuery::queryUserAndPasswordHash(const std::string &userName, const std::string &hash){
    auto getQuery = []()->auto {
        auto ret = Query(FIELD(Model::User, id));
        ret.Where(FIELD(Model::User,username) == PARAM && FIELD(Model::User,password_hash) == PARAM);
        return ret;
    };
    thread_local auto query = getQuery();
    auto res = query.exec(userName,hash).getVector().size();
    return res == 1;
}
}