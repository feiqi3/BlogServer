#ifndef FCONTROLLER_H
#define FCONTROLLER_H
#include <functional>
#include <string>
#include "FHttpResponse.h"
#include "FHttpDef.h"

namespace Fei::Http{
    class FHttpRequest;
    using FControllerFunc = std::function<FHttpResponse(FHttpRequest*)>;
    
    class FControllerBase{
        public:
        FControllerFunc route(std::string& path,Method method);

        public:
        virtual ~FControllerBase(){};

        protected:

    
    };


}

#endif