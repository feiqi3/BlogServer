#include "FileController.h"
#include "Service/QuickRedirect.h"
#include "Service/FileReader.h"
const std::string ServerWebImagePath = SERVER_RESOURCE_DIR "web/img/";

Fei::Http::FHttpResponse Blog::FileController::getFile(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var)
{
    auto str = var.get("name");
    if (str.empty()) {
        return Redirector::RedirectTo("/404");
    }
    MemoryMappedFile file(ServerWebImagePath + str, Mode::ReadOnly, 0);
    if(!file.data())
        return Redirector::RedirectTo("/404");
    std::string filedata((char*)file.data(), file.size());
    Fei::Http::FHttpResponse res;
    res.setBody(filedata);
    return res;
}
