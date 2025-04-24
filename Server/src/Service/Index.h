#pragma once
#include "Http/FHttpResponse.h"
#include "Http/FHttpRequest.h"
#include "Http/FPathVar.h"

namespace Blog{
inline int sPerPageCount = 10;
Fei::Http::FHttpResponse IndexArticles(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);
}