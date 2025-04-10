#pragma once
#include <algorithm>
#include <cctype>
#include <string>
namespace Blog{
    class Digital{
        public:
        static bool isNumber(const std::string& str){
            return std::all_of(str.begin(), str.end(), ::isdigit);
        }
        static bool isNumber(char c){
            return ::isdigit(c);
        }
        static bool isNumber(int c){
            return ::isdigit(c);
        }
    };
};