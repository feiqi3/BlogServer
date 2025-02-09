#include "Err404Controller.h"
#include "Utils/FileReader.h"
#include <string>

const std::string ServerWebPath = SERVER_RESOURCE_DIR "web/page/";

Fei::Http::FHttpResponse Blog::Err404Controller::Err404(const Fei::Http::FHttpRequest&, const Fei::Http::FPathVar&)
{
    MemoryMappedFile file(ServerWebPath + "error/404/baisc404.html", Mode::ReadOnly, 0);
    auto d = file.data();
    auto ret = Fei::Http::FHttpResponse();
    ret.setBody(std::string((char*)d, file.size()));
    return ret;
}
