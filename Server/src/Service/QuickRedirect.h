#ifndef QUICK_REDIRECT_H
#define QUICK_REDIRECT_H
#include "Http/FHttpResponse.h"
namespace Blog {
	class Redirector {
	public:
		static Fei::Http::FHttpResponse RedirectTo(std::string path,bool permanent = false);
	};
}
#endif