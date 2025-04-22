#ifndef STATUS_H
#define STATUS_H
#include "ModelDef.h"
#include <string>

namespace Blog::Model {

	struct BlogStatus {
		std::string name;
		std::string data;
		ENTITY_TABLE(BlogStatus)
	};
}
#endif