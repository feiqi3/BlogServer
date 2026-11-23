#pragma once
#include "Http/FController.h"
#include "Http/FHttpRequest.h"
#include "Http/FHttpResponse.h"
#include "Http/FPathVar.h"
namespace Blog {
class ScriptController : public Fei::Http::FControllerBase {
public:
    ScriptController() : FControllerBase("ScriptController") {}
    Fei::Http::FHttpResponse Status(const Fei::Http::FHttpRequest&, const Fei::Http::FPathVar&);
    Fei::Http::FHttpResponse Upload(const Fei::Http::FHttpRequest&, const Fei::Http::FPathVar&);
    Fei::Http::FHttpResponse Rollback(const Fei::Http::FHttpRequest&, const Fei::Http::FPathVar&);
    Fei::Http::FHttpResponse Job(const Fei::Http::FHttpRequest&, const Fei::Http::FPathVar&);
    REGISTER_MAPPING_BEGIN("/api/scripts")
        REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "", ScriptController, Status);
        REGISTER_MAPPING_FUNC(Fei::Http::Method::POST, "", ScriptController, Upload);
        REGISTER_MAPPING_FUNC(Fei::Http::Method::POST, "/rollback", ScriptController, Rollback);
        REGISTER_MAPPING_FUNC(Fei::Http::Method::GET, "/jobs/{id}", ScriptController, Job);
    REGISTER_MAPPING_END
};
REGISTER_CONTROLLER_CLASS(ScriptController)
}
