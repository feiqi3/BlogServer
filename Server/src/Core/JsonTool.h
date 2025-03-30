#ifndef JSONTOOL_H
#define JSONTOOL_H
#include "nlohmann/json.hpp"

namespace Blog{
    class JsonTool{
        public:

        static nlohmann::json ToJson(const std::string_view& sv){
            nlohmann::json j;
            try {
                j = nlohmann::json::parse(sv);
            }
            catch (nlohmann::json::parse_error& ex) {
                // Handle parse error
                return nullptr;
            }
            return j;
        }


        static nlohmann::json ToJson(const std::string& str){
            return ToJson(std::string_view(str));
        }
        static std::string ToString(const nlohmann::json& j) {
            return j.dump();
        }
    };
}

#endif