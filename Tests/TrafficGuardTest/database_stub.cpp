#include "DAO/DataBaseOperation.h"
// HTTP adapter tests isolate the database; normal Server links the real DAO.
bool Blog::DatabaseOperation::confirmArticleVisit(const std::string&, uint64_t) { return true; }
void Blog::DatabaseOperation::usePrivateCacheForCurrentThread() {}
