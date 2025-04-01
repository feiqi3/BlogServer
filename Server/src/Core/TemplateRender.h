#ifndef TEMPLATE_RENDERER_H_
#define TEMPLATE_RENDERER_H_

#include "Utils/Singleton.h"
#include <cstdint>
#include <string>
#include <vector>
#include "Http/FReflect.h"


namespace Blog{
    class TemplateRenderData{
        public:
        void setData(const std::string& key,const std::string& value){
            renderData[key] = value;
        }

        template<typename Tp>
        void setData(const std::string& key,const std::vector<Tp>& val){
            renderData[key] = nlohmann::json::array();
            for(auto& v : val){
                renderData[key].push_back(Fei::Http::FReflect::fromClass(v));
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
}

#endif