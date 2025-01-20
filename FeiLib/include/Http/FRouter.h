#ifndef FROUTER_H
#define FROUTER_H
#include <string>
#include <queue>
#include "FSingleton.h"
#include "FDef.h"
#include "FNoCopyable.h"
#include "FController.h"

namespace Fei::Http {
class FRouterPrivate;
class FPathMatcher;
class FRouter :  public FSingleton<FRouter>,public FNoCopyable{
public:
  static void RegisterController(const std::string& pathPattern, FControllerBase *,uint32 priority = 0);
  void route(const std::string &path);

private:
	//High costs
	static uint64 calcPathPatternPriority(FPathMatcher* matcher);
	struct __ControllerAndPattern {
		Method requestMethod = Method::Invalid;
		FPathMatcher* PathMatcher = nullptr;
		FControllerFunc ControllerFunc = nullptr;
	};
	std::mutex m_lock;
};
}; // namespace Fei::Http

#endif