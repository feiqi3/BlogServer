#ifndef SERVER_BASIC_DEF_H
#define SERVER_BASIC_DEF_H

#include <string>
namespace Blog{
    const std::string BlogResourcePath = SERVER_RESOURCE_DIR;
    const std::string BlogWebPath = SERVER_RESOURCE_DIR "web/";
    const std::string BlogWebAssetsPath = SERVER_RESOURCE_DIR "web/assets/";
    const std::string BlogWebPagePath = SERVER_RESOURCE_DIR "web/page/";
    const std::string BlogDataBasePath = SERVER_RESOURCE_DIR "database/blog.db";
};

#endif