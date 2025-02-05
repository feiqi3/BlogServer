#pragma once

#include "Http/FHttpServer.h"
#define FHTTP Fei::Http
namespace Blog {
	bool filterAll(const FHTTP::FHttpRequest&, FHTTP::FHttpResponse&);
};