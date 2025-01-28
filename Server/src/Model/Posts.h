#pragma once
#include "ModelDef.h"

class Posts{
public:
	uint64_t postId;
	std::string PostTitle;
	std::string PostContent;
	uint64_t createdAt;
	uint64_t updatedAt;
	uint64_t authorId;
	int status;
};
