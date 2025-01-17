#pragma once

#include "nlohmann/json.hpp"
#include "reflect"
#include <string>
#include <utility>

namespace Fei::Http{


class FReflect{
    public:
    template<typename T>
    static nlohmann::json fromClass(T&& cls){
        using namespace nlohmann;
        json ret_json;
        ret_json["__cls"] = reflect::type_name(cls);
        reflect::for_each([&ret_json,&cls](auto i){
            //only POD and string
            ret_json[reflect::member_name(i)] = reflect::get<i>(cls);
        },cls);
        return ret_json;
    }

    template<typename T>
    static std::string toString(T&& cls){
        return fromClass(std::forward(cls)).dump();
    }
};

}