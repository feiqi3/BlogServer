#pragma once

#include "FLogger.h"
#include "Http/FController.h"
#include "Http/FHttpDef.h"
#include "Http/FHttpRequest.h"
#include "Http/FHttpResponse.h"
#include "Http/FPathVar.h"
#include "Service/QuickRedirect.h"
#include "Utils/FileReader.h"
#include <string>

const std::string CertFileFolder = SERVER_RESOURCE_DIR "temp/certbot/.well-known/acme-challenge/";

namespace Blog {
class CertBotVerifyController : public Fei::Http::FControllerBase {
public:
  CertBotVerifyController() : FControllerBase("404") {}

  inline Fei::Http::FHttpResponse DoCert(const Fei::Http::FHttpRequest &req,
                                         const Fei::Http::FPathVar &pathVar) {
	Fei::Http::FHttpResponse ret;
    std::string certFile;
    certFile = pathVar.get("filename");
    MemoryMappedFile file(CertFileFolder + certFile, Mode::ReadOnly, 0);
	if(file.data()){
		ret.setBody(std::string((char*)file.data(),file.size()));
	}else{
		Fei::Logger::instance()->log("SSL CertBot",Fei::lvl::warn,"Unable to locate certbot create file: \"{}\".",CertFileFolder + certFile);
		ret.setStatusCode(Fei::Http::StatusCode::_404);
	}
	return ret;
  }

  REGISTER_MAPPING_BEGIN("")
  REGISTER_MAPPING_FUNC(Fei::Http::Method::GET,
                        "/.well-known/acme-challenge/{filename} ",
                        CertBotVerifyController, DoCert);
  REGISTER_MAPPING_END
};

REGISTER_CONTROLLER_CLASS(CertBotVerifyController)
} // namespace Blog
