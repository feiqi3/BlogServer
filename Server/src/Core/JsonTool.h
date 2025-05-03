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

        static bool check(const nlohmann::json& j, const std::string& x) {
            if (j.find(x) == j.end()) {
                return false;
            }
            return true;
        }

        template<typename T>
        static T get(const nlohmann::json& j, const std::string& x, const T& fallback) {
            auto itor = j.find(x);
            if (itor == j.end()) {
                return fallback;
            }
            return itor->get<T>();
        }

        template<typename T>
        static bool get(const nlohmann::json& j, const std::string& x, const T& fallback,T& out) {
            auto itor = j.find(x);
            if (itor == j.end()) {
                out = fallback;
                return false;
            }
            out = itor->get<T>();
            return true;
        }
    };
}

#endif