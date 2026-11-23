#include "DAO/DataBaseOperation.h"
// HTTP adapter tests isolate the database; normal Server links the real DAO.
void Blog::DatabaseOperation::registerArticleVisit(const std::string&, uint64_t) {}
bool Blog::DatabaseOperation::confirmArticleVisit(const std::string&) { return true; }
