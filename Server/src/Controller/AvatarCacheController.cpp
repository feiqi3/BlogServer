#ifdef FEILIB_ENABLE_SCRIPT
#include "Http/FController.h"
#include "Http/FHttpRequest.h"
#include "Http/FPathVar.h"
#include "ScriptRuntime/AvatarCache.h"
#include <algorithm>
namespace Blog {
class AvatarCacheController : public Fei::Http::FControllerBase {
public:
    AvatarCacheController():FControllerBase("AvatarCache"){}
    Fei::Http::FHttpResponse get(const Fei::Http::FHttpRequest& request,const Fei::Http::FPathVar&){
        std::string source,etag;request.getQuery("source",source);
        request.traverseHeaders([&](const auto& header){
            auto name=header.first;for(char& c:name)if(c>='A'&&c<='Z')c+='a'-'A';
            if(name=="if-none-match"){etag=header.second;return false;}return true;
        });
        if(auto cache=Script::AvatarCache::current())return cache->image(std::move(source),std::move(etag));
        Fei::Http::FHttpResponse response;return response.setStatusCode(Fei::Http::StatusCode::_404).addHeader("Cache-Control","no-store");
    }
    REGISTER_MAPPING_BEGIN("/avatar-cache")
        REGISTER_MAPPING_FUNC(Fei::Http::Method::GET,"",AvatarCacheController,get);
    REGISTER_MAPPING_END
};
REGISTER_CONTROLLER_CLASS(AvatarCacheController)
}
#endif
