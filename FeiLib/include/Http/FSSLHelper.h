#ifndef FSSLHELPER_H_
#define FSSLHELPER_H_
#include "FBufferReader.h"
#include "FCallBackDef.h"
#include "FSingleton.h"
#include <memory>
#include <string>
namespace Fei::Http {

class _FSSLHelperPrivate;

class FSSLEnv :public FSingleton<FSSLEnv> {
public:
  // SetUp SSL Context
  FSSLEnv(const std::string &path);
  // Destroy SSL Context
  ~FSSLEnv();
  void* getSSLContext()const{
    return SSLContext;
  }
private:
  void *SSLContext = 0;
};

class FSSLHelper {
public:
  FSSLHelper();
  ~FSSLHelper();
  // DO Check This excplict
  bool shakeHand(const FTcpConnPtr &ptr, FBufferReader &reader);

  // Will throw FException e
  FBufferReader EncryptSendingData(const std::string &inData);
  FBufferReader DecryptRecvingData(FBufferReader &reader);

private:

  std::unique_ptr<_FSSLHelperPrivate> dp;
};
} // namespace Fei::Http

#endif