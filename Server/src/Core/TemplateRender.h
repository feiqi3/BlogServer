#ifndef TEMPLATE_RENDERER_H_
#define TEMPLATE_RENDERER_H_

#include "Utils/Singleton.h"
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>
#include "Http/FReflect.h"
#include "reflect"


namespace Blog{

    class TemplateRenderData{
        public:
        void setData(const std::string& key,const std::string& value){
            renderData[key] = value;
        }

        void setData(const std::string& key,std::string&& value){
            renderData[key] = std::move(value);
        }

        template<typename Tp>
        void setData(const std::string& key,const std::vector<Tp>& val){
            renderData[key] = nlohmann::json::array();
            for(auto& v : val){
                renderData[key].push_back(Fei::Http::FReflect::fromClass(v));
            }
        }

        template<typename Tp>
        void setData(const std::string& key,std::vector<Tp>&& val){
            renderData[key] = nlohmann::json::array();
            for(auto&& v : val){
                renderData[key].push_back(Fei::Http::FReflect::fromClass(std::move(v)));
            }
        }

        template<typename Tp>
        void setData(const std::string& key,const Tp& val){
            renderData[key] = Fei::Http::FReflect::fromClass(val);
        }

        nlohmann::json renderData;
    };

    class TemplateRender : public Singleton<TemplateRender>{
        public:
        TemplateRender();
        ~TemplateRender();

        bool render(const std::string& templateFilePath,TemplateRenderData&data,std::string& out);
        void checkOverdue(uint64_t time_ms);

        private:
        class TemplateRenderPrivate* mDp = 0;

    };

    template<class T,class U>
    T fromTuple(U& in){
        constexpr std::size_t tupleSize = std::tuple_size<U>::value;
        constexpr std::size_t classMemSize = reflect::size<T>();
        static_assert(tupleSize == classMemSize, "Not Match Reflect");
        T ret;
        reflect::for_each([&ret,&in](const auto I){
            reflect::get<I>(ret) = std::move(std::get<I>(in));
        },ret);
        return ret;
    }
}

#endif