#include "BlogData.h"
#include "DAO/DataBaseOperation.h"
#include "DAO/ORM.h"
#include <cstdint>
#include "DAO/ORM.h"
#include "Model/Status.h"
namespace Blog {
BlogData::BlogData() {
  auto updateState = DatabaseOperation::instance()->Prepare(
      "INSERT INTO BlogStatus(name, data)"
      "VALUES(?, ?)"
      "ON CONFLICT(name) DO UPDATE"
      "SET data = BlogStatus.data"
      "RETURNING data;");
      DatabaseOperation::instance()->ReExec(updateState, "view_times",0);
      updateState->excute();
      this->mBlogTotalViewTimes = updateState->getInteger64(0);
      DatabaseOperation::instance()->ReExec(updateState, "total_blogs",0);
      updateState->excute();
      this->mBlogTotalBlogs = updateState->getInteger64(0);
    }
    void BlogData::addViewTimes(){
        this->mBlogTotalViewTimes ++;
    }
    void BlogData::addBlogNum(){
        mBlogTotalBlogs ++;
    }
    void BlogData::syncData(){
        auto genQuery = [](){
            Query<Model::BlogStatus> query;
            query.update((FIELD(Model::BlogStatus, data)==PARAM)).Where((FIELD(Model::BlogStatus, name)==PARAM));
            return query;
        };
        thread_local auto q = genQuery();
        q.exec(mBlogTotalBlogs,"total_blogs");
        q.exec(mBlogTotalViewTimes,"view_times");
    }

} // namespace Blog