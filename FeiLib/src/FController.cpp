#include "Http/FController.h"
#include "Http/FRouter.h"
namespace Fei::Http{
    FControllerBase::FControllerBase(const std::string& controllerName) :mControllerName(controllerName) {
    }
    FControllerBase::~FControllerBase()
    {
    }

    void FControllerBase::registerMappingFunction(const std::string& pathPattern, Method mapMethod, FControllerFunc func)
    {
        using namespace ::Fei::Http;
         for (auto&& i : mControllerMappingPaths) {
             FRouter::RegisterControllerFuncs(i + pathPattern, mapMethod, mControllerName, func);
        }
    }
    
    void ControllerRegisterHelper::registerController(FControllerPtr& ptr)
    {
        FRouter::RegisterController(ptr->getControllerName(), ptr);
    }
}