#include "Session.h"
#include "FConfigReader.h"
#include "FDef.h"

#include "FConcurrentMap.h"

#include <array>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>
#include "FSSLHelper.h"
namespace Blog {
struct SessionData {
  std::chrono::system_clock::time_point lastTime;
  std::map<std::string, std::string> data;
};

namespace {
std::string generateSessionId(int length = 32) {
  std::vector<char> buffer(length);
  Fei::FSSLUtils::randomBytes((unsigned char *)buffer.data(), length);
  std::ostringstream oss;
  for (auto byte : buffer) {
      oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
  }
  return oss.str();
}
} // namespace

class SessionManagerPrivate {
public:
  // <session id,session data>
  Fei::FConcurrentHashMap<std::string, SessionData> mSessionMap;
  std::mutex mEraseMutex;
  int64_t sessionHoldTime = 1000 * 60 * 60;
};

SessionManager::SessionManager() {
  mDp = new SessionManagerPrivate();
  auto time = Fei::FConfigReader::instance()->getCfg("SessionHoldTime");
  if (time.has_value()) {
    mDp->sessionHoldTime = std::stoll(time.value());
  }
}

SessionManager::~SessionManager() {
  delete mDp;
  mDp = 0;
}

std::string SessionManager::addSession() {
  std::string sessionId = generateSessionId();
  // Get a unique session id
  while (hasSession(sessionId)) {
    sessionId = generateSessionId();
  };

  mDp->mSessionMap.insert(
      sessionId,
       SessionData{.lastTime = std::chrono::system_clock::now(), .data = {}});
  return sessionId;
}

bool SessionManager::hasSession(const std::string &sessionId) {
  return mDp->mSessionMap.find(sessionId);
}

bool SessionManager::addDataToSession(const std::string &sessionId,
                                      const std::string &key,
                                      const std::string &data) {

  bool isExist = mDp->mSessionMap.findAndModifyLocked(sessionId, [&data,&key](auto &in) {
    in.data[key] = data;
  });

  return isExist;
}

bool SessionManager::getDataFromSession(const std::string &sessionId,
                                        const std::string &key,
                                        std::string &out) {
  bool hasData = false;                                         
  bool isExist = mDp->mSessionMap.findAndModifyLocked(sessionId, [&](auto &in) {
    auto itor = in.data.find(key);
    if (itor != in.data.end()) {
      out = itor->second;
      hasData = true;
    }else{
      hasData = false;
    }
  });

  return hasData && isExist;
}

void SessionManager::deleteSession(const std::string &sessionId) {
  //TODO: erase
}

uint32_t SessionManager::getSessionExpireTimeMins()const{
  return (Fei::uint32)(mDp->sessionHoldTime / 1000);;
}

void SessionManager::checkOverdue(uint64_t time_ms) {
  std::vector<std::string> toerase;
 
  mDp->mSessionMap.traversal([this, time_ms, &toerase](const auto &key,
                                                        const auto &val) {
    auto timeLast = std::chrono::duration_cast<std::chrono::milliseconds>(
        val.lastTime.time_since_epoch());
    if (time_ms - (uint64_t)timeLast.count() > (uint64_t)mDp->sessionHoldTime) {
      toerase.push_back(key);
    }
    return true;
  });

  for(auto&& i :  toerase){
    mDp->mSessionMap.erase(i);
  }
}

} // namespace Blog