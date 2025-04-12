#include "TestMessageQuery.h"
#include "DAO/ORM.h"
#include "FLogger.h"
#include "Model/TestMessages.h"
#include <optional>
#include <string>
#include <tuple>
#include <vector>
namespace Blog::DAO {

std::optional<std::string>
TestMessageQuery::InsertMessage(const TestMessage &message){
    auto query = Query<TestMessage>();
    query.InsertWithAutoIncPk(message);
    auto ptr = query.exec().getResult();
    bool res = ptr->excute();
    if (res)
        return std::nullopt;
    return ptr->getErrMsg();
}

std::vector<std::tuple<std::string, std::string>>
TestMessageQuery::QueryMessageByPage(uint32_t pageNum, uint32_t perPage){
    auto getQuery = []() {
        Query q(FIELD(TestMessage, name) - FIELD(TestMessage, content));
        q.From("TestMessage").skip(PARAM).limit(PARAM).OrderByDesc(FIELD(TestMessage, id));
        return q;
    };
    thread_local auto query = getQuery();
    auto vec = query.exec(perPage,pageNum * perPage).getVector();
    std::vector<std::tuple<std::string, std::string>> ret;
    for (auto &v : vec) {
        ret.emplace_back(std::get<0>(v), std::get<1>(v));
    }
    return ret;
}

uint32_t TestMessageQuery::QueryMessagePageNum(){
        auto getQuery = []() {
            Query<int> q;
            q.Select(SelectCount()).From("TestMessage");
            return q;
        };
        thread_local auto query = getQuery();
        auto vec = query.exec().getVector();
        auto ptr= query.getResult();
        if(vec.size() > 0){
            return vec[0];
        }
        Fei::Logger::instance()->log(Fei::lvl::err, "SQL error, reason \"{}\"", ptr->getErrMsg());
        return 0;
}


} // namespace Blog::DAO