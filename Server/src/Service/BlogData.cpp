#include "BlogData.h"
#include "DAO/DataBaseOperation.h"
#include "DAO/ORM.h"
#include <cstdint>
#include "DAO/ORM.h"
#include "Model/Status.h"
namespace Blog {
BlogData::BlogData() {
  auto updateState = DatabaseOperation::instance()->Prepare(
      "INSERT INTO BlogStatus(name, data) "
      "VALUES(?, ?) "
      "ON CONFLICT(name) DO UPDATE "
      "SET data = BlogStatus.data "
      "RETURNING data;");
      DatabaseOperation::instance()->ReExec(updateState, "view_times",0);
      updateState->excute();
      this->mBlogTotalViewTimes = updateState->getInteger64(0);
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
        q.exec((uint64_t)mBlogTotalViewTimes,std::string("view_times"));
        thread_local auto q2 = genQueryForTotalBlogs();
        q2.exec();
        this->mBlogTotalBlogs = q2.getVector()[0];
    }

} // namespace Blog