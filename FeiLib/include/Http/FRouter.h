#ifndef FROUTER_H
#define FROUTER_H
#include <string>
#include <queue>
#include <functional>

#include "FPathVar.h"
#include "FSingleton.h"
#include "FDef.h"
#include "FHTTPDef.h"
#include "FNoCopyable.h"
#include "FController.h"
namespace Fei::Http {
class FRouterPrivate;
class FPathMatcher;
class __FRouterInner;
class FHttpResponse;
class FHttpRequest;

class F_API FRouter : public FSingleton<FRouter>,public FNoCopyable{
public:
  static void RegisterController(const std::string& controllerName, FControllerPtr controller);
  static void RegisterControllerFuncs(const std::string& pathPattern,Method mapMethod,const std::string& controllerName, FControllerFunc func);
  static void UnRegisterController(const std::string& controllerName);

  struct RouteResult {
	  FPathVar pathVariable;
	  FControllerFunc controllerFunc;
	  bool isvalid() const{
		  return controllerSave != nullptr;
	  }
  private:
	  FControllerPtr controllerSave;
	  friend FRouter;
  };

public:
	FRouter();
	RouteResult route(Method method, const std::string& path);
private:
	void regController(const std::string& controllerName, FControllerPtr controller);
	void regControllerFunc(const std::string& pathPattern, Method mapMethod, const std::string& controllerName, FControllerFunc& func);
	static uint64 calcPathPatternPriority(FPathMatcher* matcher);
	void unregController(const std::string& controllerName);
private:
	__FRouterInner* _dp = 0;
};
}; // namespace Fei::Http

#endif