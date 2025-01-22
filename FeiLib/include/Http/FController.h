#ifndef FCONTROLLER_H
#define FCONTROLLER_H
#include <functional>
#include <string>
#include <vector>
#include <memory>

#include "FHttpDef.h"
#include "FNoCopyable.h"
#include "FHttpResponse.h"
#include "FHttpRequest.h"

namespace Fei::Http{
    class FHttpRequest;
    
    using FControllerFunc = std::function<FHttpResponse(const FHttpRequest&, const PathVarMap&)>;
    using FControllerPtr = std::shared_ptr<class FControllerBase>;

    class F_API FControllerBase :public std::enable_shared_from_this<FControllerBase>, public FNoCopyable {
        public:
            FControllerBase(const std::string& controllerName);
        public:
        virtual void registerMapping() = 0;
        virtual ~FControllerBase();

        public:
        //Register Method and mapping in this function implementation
        void addControllerMappingPath(const std::string& path) {
             mControllerMappingPaths.push_back(path);
        }
        const std::string& getControllerName()const { return mControllerName; }
 
        void registerMappingFunction(const std::string& pathPattern, Method mapMethod, FControllerFunc func);
    private:
        std::vector<std::string> mControllerMappingPaths;
        std::string mControllerName;
    };

    class F_API ControllerRegisterHelper {
    public:
        ControllerRegisterHelper() = delete;
        template<typename T,typename... Args>
        ControllerRegisterHelper(T*,Args&&... args) {
            using namespace ::Fei::Http;
            FControllerPtr ptr = std::make_shared<T>(std::forward<Args>(args)...);
            registerController(ptr);
            ptr->registerMapping();
            delete this;
        }
    private:
        void registerController(FControllerPtr& ptr);
    };

}


#define REGISTER_CONTROLLER_CLASS(CLS_NAME,...) {auto __##CLS_NAME = new ::Fei::Http::ControllerRegisterHelper((CLS_NAME*)nullptr  ,##__VA_ARGS__);}
#define REGISTER_MAPPING_FUNC(MAPPING_METHOD,MAPPING_PATTERN,CLS_NAME,MAPPING_FUNC) registerMappingFunction(MAPPING_PATTERN,MAPPING_METHOD,std::bind(&##CLS_NAME##::##MAPPING_FUNC,this,std::placeholders::_1,std::placeholders::_2));

#endif