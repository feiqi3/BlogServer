#include "QuickRedirect.h"

Fei::Http::FHttpResponse Blog::Redirector::RedirectTo(std::string path, bool permanent)
{
	using namespace Fei::Http;
	StatusCode code = permanent ? StatusCode::_301 : StatusCode::_302;
	FHttpResponse response;
	response.setStatusCode(code);
	response.addHeader("Location", path);
	return response;
}
