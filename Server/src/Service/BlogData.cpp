#include "BlogData.h"
#include "DAO/DataBaseOperation.h"
#include "DAO/ORM.h"
#include <cstdint>
#include <string>
#include "DAO/ORM.h"
#include "Model/Status.h"
namespace Blog {
BlogData::BlogData() {
  auto updateState = DatabaseOperation::instance()->Prepare(
      "INSERT INTO BlogStatus(name, data) "
      "VALUES(\"view_times\", \"0\") "
      "ON CONFLICT(name) DO NOTHING"
      "; ");
      DatabaseOperation::instance()->ReExec(updateState);
      updateState->excute();
      Query<std::string> q(FIELD(Model::BlogStatus, data));
      q.From("BlogStatus").Where(FIELD(Model::BlogStatus, name) == "view_times");
      this->mBlogTotalViewTimes = std::stoull(q.exec().getVector()[0]);
      Query<uint64_t> query;
      query.Select(SelectCount()).From("Posts");
      mBlogTotalBlogs = query.exec().getVector()[0];
    }
    void BlogData::addViewTimes(){
        this->mBlogTotalViewTimes ++;
    }
    uint64_t BlogData::getTotalBlogNum(){
        return mBlogTotalBlogs;
    }

    void BlogData::syncData(uint64_t){
        auto genQuery = [](){
            Query<Model::BlogStatus> query;
            query.update((FIELD(Model::BlogStatus, data)==PARAM)).Where((FIELD(Model::BlogStatus, name)==PARAM));
            return query;
        };
        auto genQueryForTotalBlogs = [](){
            Query<uint64_t> query;
            query.Select(SelectCount()).From("Posts");
            return query;
        };
        thread_local auto q = genQuery();
        auto res = q.exec(std::to_string((uint64_t)mBlogTotalViewTimes),std::string("view_times")).getResult();
        res->excute();
        thread_local auto q2 = genQueryForTotalBlogs();
        q2.exec();
        this->mBlogTotalBlogs = q2.getVector()[0];
    }

} // namespace Blog