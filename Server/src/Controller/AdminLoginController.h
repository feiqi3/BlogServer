#pragma once
#include "FDef.h"
#include "Http/FController.h"
#include "Http/FHttpRequest.h"
#include "Http/FHttpResponse.h"
#include "Http/FPathVar.h"
#include <string>
#include <chrono>

namespace Blog{
    class AdminLoginController : public Fei::Http::FControllerBase {
        public:
		AdminLoginController();

/*
        inRequest:
        {
        'Content-Type': 'application/json',
        body: {
            'username': ' ',
            'password': ' '
            }
        }

        return json: 
        reuslt:
        msg: //error reason
*/

		Fei::Http::FHttpResponse Login(const Fei::Http::FHttpRequest& req, const Fei::Http::FPathVar& var);

		REGISTER_MAPPING_BEGIN("api/")
			REGISTER_MAPPING_FUNC(Fei::Http::Method::POST, "login", AdminLoginController, Login);
		REGISTER_MAPPING_END

        private:
        std::string mUserName;
        std::string mPassword;
        bool mIsLocked = false;
        uint32_t mFailedCount = 0;
        std::chrono::system_clock::time_point mLockTime;
        std::chrono::system_clock::time_point mLastLoginTime;
    };
}
