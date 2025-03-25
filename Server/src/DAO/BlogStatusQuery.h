#ifndef BLOG_STATUS_QUERY_H
#define BLOG_STATUS_QUERY_H
#include <string>
namespace Blog::DAO {
	class BlogStatusQuery {
	public:
		static std::string queryBlogStatus(const std::string& propertyName);
	};
};

#endif