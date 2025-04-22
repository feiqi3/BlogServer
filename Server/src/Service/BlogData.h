#include "Utils/Singleton.h"
#include <atomic>
#include <cstdint>

namespace Blog{
class BlogData:public Singleton<BlogData>{
    public:
    BlogData();
    uint64_t updateBlogViewTimes();

    void addBlogNum();
    void addViewTimes();
    
    void syncData();
    
    private:
    std::atomic_uint64_t mBlogTotalViewTimes = 0;
    std::atomic_uint64_t mBlogTotalBlogs = 0;
};
}