#include "Session.h"
#include "FConfigReader.h"
#include "FDef.h"
#include "tbb/concurrent_map.h"
#include <chrono>
#include <mutex>
#include <string>
#include "FSSLHelper.h"
namespace Blog {
struct SessionData {
  std::chrono::system_clock::time_point lastTime;
  std::map<std::string, std::string> data;
};

namespace {
std::string generateSessionId(int length = 32) {
  std::string sessionId;
  sessionId.resize(length);
  Fei::FSSLUtils::randomBytes((unsigned char *)sessionId.data(), length);
  return sessionId;
}
} // namespace

class SessionManagerPrivate {
public:
  // <session id,session data>
  tbb::concurrent_map<std::string, SessionData> mSessionMap;
  std::mutex mEraseMutex;
  int64_t sessionHoldTime = 1000 * 60 * 60 * 24 * 7;
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
      {sessionId,
       SessionData{.lastTime = std::chrono::system_clock::now(), .data = {}}});
  return sessionId;
}

bool SessionManager::hasSession(const std::string &sessionId) {
  if (mDp->mSessionMap.find(sessionId) != mDp->mSessionMap.end()) {
    return true;
  }
  return false;
}

bool SessionManager::addDataToSession(const std::string &sessionId,
                                      const std::string &key,
                                      const std::string &data) {
  auto itor = mDp->mSessionMap.find(sessionId);
  // Not Found.
  if (itor == mDp->mSessionMap.end())
    return false;
  itor->second.data[key] = data;
  return true;
}

bool SessionManager::getDataFromSession(const std::string &sessionId,
                                        const std::string &key,
                                        std::string &out) {
  auto itor = mDp->mSessionMap.find(sessionId);
  // Not Found.
  if (itor == mDp->mSessionMap.end())
    return false;
  out = itor->second.data[key];
  return true;
}

void SessionManager::deleteSession(const std::string &sessionId) {
  auto itor = mDp->mSessionMap.find(sessionId);
  auto &mutex = mDp->mEraseMutex;
  FAUTO_LOCK(mutex);
  mDp->mSessionMap.unsafe_erase(itor);
}

void SessionManager::checkOverdue(uint64_t time_ms) {
  for (auto itor = mDp->mSessionMap.begin(); itor != mDp->mSessionMap.end();) {
    auto timeLast = std::chrono::duration_cast<std::chrono::milliseconds>(
        itor->second.lastTime.time_since_epoch());
    if (time_ms - (uint64_t)timeLast.count() > (uint64_t)mDp->sessionHoldTime) {
      auto &mutex = mDp->mEraseMutex;
      FAUTO_LOCK(mutex);
      itor = mDp->mSessionMap.unsafe_erase(itor);
    } else {
      itor++;
    }
  }
}

} // namespace Blog