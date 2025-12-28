#pragma once
#include "Core/TemplateRender.h"
#include "BlogData.h"
#include <algorithm>
namespace Blog{
        //currentPage: 0 to n
        inline void setPageData(TemplateRenderData& data,int currentPage,int maxPage,int radius){
            int lastPage = std::max(1,currentPage - 1);
            int nextPage = std::min(maxPage ,lastPage + 2);
            data.setData("pageForward",lastPage);
            data.setData("pageNext",nextPage);
            data.setData("radius",radius);
            data.setData("currentPage", currentPage);
            data.setData("pageNums", maxPage);
        }

        inline void updateAndsetBlogCommonData(TemplateRenderData& data){
            data.setData("blogViewTimes",BlogData::instance()->updateBlogViewTimes());
            data.setData("blogTotalNum",BlogData::instance()->getTotalBlogNum());
        }

        inline void setBlogCommonData(TemplateRenderData& data){
            data.setData("blogViewTimes",BlogData::instance()->getBlogViewTimes());
            data.setData("blogTotalNum",BlogData::instance()->getTotalBlogNum());
        }
}