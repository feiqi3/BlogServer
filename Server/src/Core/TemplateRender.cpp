#include "FLogger.h"
#include "FConfigReader.h"
#include "TemplateRender.h"
#include "Utils/FileCache.h"
#include <string>
#include <algorithm>
#include <memory>

#include "inja/inja.hpp"

namespace Blog {

    class TemplateRenderPrivate{
        public:
        std::unique_ptr<FileCache>  mFileCache;
        TemplateRenderPrivate(){
            int cacheTime = 1000 * 60 * 60 * 24;
            
            auto cfg = Fei::FConfigReader::instance();
            auto cacheTimeOpt = cfg->getCfg("TemplateCacheTime");
            if(cacheTimeOpt.has_value()){
                cacheTime = std::stoi(cacheTimeOpt.value());
            }
            mFileCache = std::make_unique<FileCache>(cacheTime);
        }
    };

    void TemplateRender::checkOverdue(uint64_t time_ms){
        mDp->mFileCache->checkOverdue(time_ms);
    }

    TemplateRender::TemplateRender(){
        mDp = new TemplateRenderPrivate();
    }
    TemplateRender:: ~TemplateRender(){
        delete mDp;
        mDp = 0;
    }

    bool TemplateRender::render(const std::string& templateFilePath,TemplateRenderData&data,std::string& out){
        auto filePtr = mDp->mFileCache->getOrGen(templateFilePath);
        if(!filePtr){
            Fei::Logger::instance()->log(Fei::lvl::err, "Template file not found: {}", templateFilePath);
            return false;
        }
        auto d = filePtr->data();
        std::string_view templateStr((char*)d, filePtr->size());
        out = inja::render(templateStr, data.renderData);
        return true;
    }

}