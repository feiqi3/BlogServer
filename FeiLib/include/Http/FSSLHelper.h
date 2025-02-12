#ifndef FSSLHELPER_H_
#define FSSLHELPER_H_
#include "FBufferReader.h"
#include "FCallBackDef.h"
#include "FSingleton.h"
#include <memory>
#include <string>
namespace Fei::Http {

class _FSSLHelperPrivate;

class FSSLEnv : FSingleton<FSSLEnv> {
public:
  // SetUp SSL Context
  FSSLEnv(const std::string &path);
  // Destroy SSL Context
  ~FSSLEnv();

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
  FBufferView EncryptSendingData(const std::string &inData);
  FBufferView DecryptRecvingData(FBufferReader &reader);

private:
  void *sslHandler = 0;
  void *rbio = 0;
  void *wbio = 0;
  std::unique_ptr<_FSSLHelperPrivate> dp;
};
} // namespace Fei::Http

#endif