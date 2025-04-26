#ifndef FSSLHELPER_H_
#define FSSLHELPER_H_
#include "FBufferReader.h"
#include "FCallBackDef.h"
#include "FSingleton.h"
#include <memory>
#include <string>
namespace Fei{

class _FSSLHelperPrivate;

class FSSLEnv :public FSingleton<FSSLEnv> {
public:
  // SetUp SSL Context
  //certificateFile --> like "cert.pem"
  //privateKeyFile --> like "key.pem"
  FSSLEnv(const std::string &certificateFile,const std::string &privateKeyFile);
  //Change cert on the fly, for current existed connection
  void loadCertFiles(const std::string &certificateFile,const std::string &privateKeyFile);
  // Destroy SSL Context
  ~FSSLEnv();
  bool isEnvSetup()const{
    return SSLContext != 0;
  }
  
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
  bool shakeHand(FTcpConnection* ptr, FBufferReader &reader);
  bool hasShakeHandFin()const;
  // Will throw FException e
  FBufferReader EncryptSendingData(const char* inData,int len);
  FBufferReader DecryptRecvingData(FBufferReader &reader);

private:

  std::unique_ptr<_FSSLHelperPrivate> dp;
};

class F_API FSSLUtils{
  public:
  static void randomBytes(unsigned char* data,uint32 num);
};

} // namespace Fei::Http

#endif