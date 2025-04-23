#include "Utils/Singleton.h"
#include <atomic>
#include <cstdint>

namespace Blog{
class BlogData:public Singleton<BlogData>{
    public:
    BlogData();
    inline uint64_t updateBlogViewTimes(){
        return ++ mBlogTotalViewTimes;
    }
    inline uint64_t getBlogViewTimes(){
        return mBlogTotalViewTimes;
    }
    uint64_t getTotalBlogNum();
    void addViewTimes();
    
    void syncData(uint64_t );
    
    private:
    std::atomic_uint64_t mBlogTotalViewTimes = 0;
    std::atomic_uint64_t mBlogTotalBlogs = 0;
};
}