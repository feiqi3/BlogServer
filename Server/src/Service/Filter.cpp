#include "Filter.h"
#include "Http/FHttpResponse.h"
#include "Http/FHttpRequest.h"
#include "Service/QuickRedirect.h"
#include <fstream>
namespace{

	const std::string ServerWebPath = SERVER_RESOURCE_DIR "web/page/";

	std::string _404File;

	std::string readFile(const std::string& path) {
		std::fstream file(path);
		if (!file.is_open()) return {};
		file.seekg(0, std::ios::end);
		int length = file.tellg();
		file.seekg(0, std::ios::beg);
		std::string ret;
		ret.resize(length+1);
		file.read(ret.data(), length);
		return ret;
	}

}
bool Blog::filterAll(const FHTTP::FHttpRequest& request, FHTTP::FHttpResponse& response)
{
	if (request.getPath() == "/404" || request.getPath() == "/file/favicon.ico")
		return false;
	response = Redirector::RedirectTo("/404");
	return true;
}
