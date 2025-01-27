#ifndef FPATHVAR_H
#define FPATHVAR_H
#include "FDef.h"
#include <string>
#include <map>

namespace Fei::Http {
    class F_API FPathVar {
    public:
        uint32 size()const { return mMap.size(); }
        const std::string& get(const std::string& name)const {
            auto itor = mMap.find(name);
            if (itor == mMap.end()) {
                return nullS;
            }
            return itor->second;
        }

        bool hasVar(const std::string& name) const{
            auto itor = mMap.find(name);
            return itor != mMap.end();
        }

        void setVar(const std::string& name, const std::string& val) {
            mMap[name] = val;
        }

    private:
        inline static const std::string nullS = "";
        std::map<std::string, std::string> mMap;
    };
}

#endif