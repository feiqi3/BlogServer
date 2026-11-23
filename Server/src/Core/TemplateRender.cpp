#include "FLogger.h"
#include "TemplateRender.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include "inja/inja.hpp"

namespace Blog {

    class TemplateRenderPrivate {
    public:
        struct Entry {
            std::filesystem::file_time_type modified;
            inja::Environment environment;
            inja::Template parsed;

            Entry(std::filesystem::file_time_type modified, const std::string& text)
                : modified(modified), parsed(environment.parse(text)) {}
        };

        std::shared_ptr<Entry> get(const std::string& path) {
            std::error_code error;
            auto modified = std::filesystem::last_write_time(path, error);
            if (error) return {};
            {
                std::shared_lock lock(mutex);
                const auto it = templates.find(path);
                if (it != templates.end() && it->second->modified == modified) return it->second;
            }

            std::unique_lock lock(mutex);
            modified = std::filesystem::last_write_time(path, error);
            if (error) return {};
            const auto it = templates.find(path);
            if (it != templates.end() && it->second->modified == modified) return it->second;
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return {};
            const std::string text((std::istreambuf_iterator<char>(stream)), {});
            // Avoid caching a file that was replaced while it was being read.
            const auto afterRead = std::filesystem::last_write_time(path, error);
            auto entry = std::make_shared<Entry>(modified, text);
            if (!error && afterRead == modified) templates.insert_or_assign(path, entry);
            return entry;
        }

    private:
        std::shared_mutex mutex;
        std::unordered_map<std::string, std::shared_ptr<Entry>> templates;
    };

    void TemplateRender::checkOverdue(uint64_t time_ms){
    }

    TemplateRender::TemplateRender(){
        mDp = new TemplateRenderPrivate();
    }
    TemplateRender:: ~TemplateRender(){
        delete mDp;
        mDp = 0;
    }

    bool TemplateRender::render(const std::string& templateFilePath,TemplateRenderData&data,std::string& out){
        auto entry = mDp->get(templateFilePath);
        if(!entry){
            out = "Template file not found: " + templateFilePath;
            Fei::Logger::instance()->log(Fei::lvl::err, "Template file not found: {}", templateFilePath);
            return false;
        }
        // Inja creates a fresh Renderer per call and reads the parsed template
        // and environment tables without modifying them.
        out = entry->environment.render(entry->parsed, data.renderData);
        return true;
    }

}
