
#include <cassert>
#include <memory>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/types.h>
#include <string>
#include <utility>

#include "FBuffer.h"
#include "FBufferReader.h"
#include "FException.h"
#include "FLogger.h"
#include "FSSLHelper.h"
#include "FTCPConnection.h"
#include "openssl/rand.h"

#define MODULE_NAME "SSLHelper"

class SSLNotPreparedException : public Fei::FException {
public:
  std::string reason() const override {
    return "Read or Write After SSL Shake Hand Finish.";
  }
};

class SSLDataException : public Fei::FException {
public:
  SSLDataException(const std::string &res) : mreason(res) {}
  std::string reason() const override { return mreason; }

private:
  std::string mreason;
};

namespace Fei {

// SetUp SSL Context
FSSLEnv::FSSLEnv(const std::string &certificateFile,
                 const std::string &privateKeyFile) {
  OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CRYPTO_STRINGS, NULL);
  OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS, NULL);
  SSLContext = (void *)SSL_CTX_new(TLS_server_method());
  if (!SSLContext) {
    auto errCode = ERR_get_error();
    auto reason = ERR_GET_REASON(errCode);
    Logger::instance()->log(MODULE_NAME, lvl::critical,
                            "Unable to create SSL context, reason: \"{}\"",
                            reason);
  }
  SSL_CTX *ctx = (SSL_CTX *)SSLContext;
  /* Set the key and cert */
  if (SSL_CTX_use_certificate_file(ctx, certificateFile.c_str(),
                                   SSL_FILETYPE_PEM) <= 0) {
    auto errCode = ERR_get_error();
    auto reason = ERR_GET_REASON(errCode);
    Logger::instance()->log(
        MODULE_NAME, lvl::critical,
        "Unable to load SSL certificate file, reason: \"{}\"", reason);
  }

  if (SSL_CTX_use_PrivateKey_file(ctx, privateKeyFile.c_str(),
                                  SSL_FILETYPE_PEM) <= 0) {
    auto errCode = ERR_get_error();
    auto reason = ERR_GET_REASON(errCode);
    Logger::instance()->log(
        MODULE_NAME, lvl::critical,
        "Unable to load SSL private key file, reason: \"{}\"", reason);
  }
}
// Destroy SSL Context
FSSLEnv::~FSSLEnv() {
  SSL_CTX_free((SSL_CTX *)SSLContext);
  SSLContext = nullptr;
}

class _FSSLHelperPrivate {
public:
  _FSSLHelperPrivate() : inBuffer(128), outBuffer(128) {}
  FBuffer inBuffer;
  FBuffer outBuffer;
  SSL *sslHandler = 0;
  BIO *rbio = 0;
  BIO *wbio = 0;
};

FSSLHelper::~FSSLHelper() {
  // Auto free bio
  SSL_free(dp->sslHandler);
  dp->sslHandler = 0;
  dp->rbio = 0;
  dp->wbio = 0;
}

FSSLHelper::FSSLHelper() : dp(new _FSSLHelperPrivate) {
  auto sslCtx = FSSLEnv::instance()->getSSLContext();
  if (!sslCtx)
    return;
  dp->sslHandler = SSL_new((SSL_CTX *)sslCtx);
  dp->rbio = BIO_new(BIO_s_mem()); // 读 BIO，模拟接收数据
  BIO_set_mem_eof_return(dp->rbio, -1);
  dp->wbio = BIO_new(BIO_s_mem()); // 写 BIO，模拟发送数据
  BIO_set_mem_eof_return(dp->wbio, -1);
  SSL_set_bio(dp->sslHandler, dp->rbio, dp->wbio);
  SSL_set_accept_state(dp->sslHandler);
}

bool FSSLHelper::hasShakeHandFin() const {
  SSL *ssl = dp->sslHandler;
  return SSL_is_init_finished(ssl);
}

bool FSSLHelper::shakeHand(FTcpConnection *ptr, FBufferReader &reader) {
  SSL *ssl = dp->sslHandler;
  if (SSL_is_init_finished(ssl))
    return true;

  BIO *in_bio = dp->rbio;
  BIO *out_bio = dp->wbio;
  auto r = SSL_do_handshake(ssl);
  if (r < 0) {
    // Get data from reader
    auto view = reader.peekAll();

    int read = BIO_write(in_bio, (void *)view.get(), view.size());
    if (read > 0) {
      view.resetSize(read);
      reader.expireView(view);
    }
  }

  r = SSL_do_handshake(ssl);
  if (r < 0) {
    auto pending = BIO_ctrl_pending(out_bio);
    std::string dataToSend;
    dataToSend.resize(pending);
    pending = BIO_read(out_bio, dataToSend.data(), dataToSend.size());
    assert(pending >= 0);
    ptr->send(std::move(dataToSend));
  }

  return false;
}

FBufferReader FSSLHelper::EncryptSendingData(const char *inData, int len) {
  SSL *ssl = dp->sslHandler;
  if (!hasShakeHandFin()) {
    throw SSLNotPreparedException();
  }

  // write data need encrypt into ssl
  int readSize = 0;
  // If has data not send, append current data back to it.
  if (dp->inBuffer.getReadableSize() > 0) {

    dp->inBuffer.Append(inData, len);
    FBufferReader reader(dp->inBuffer);
    auto view = reader.peekAll();
    readSize = SSL_write(dp->sslHandler, (void *)view.get(), view.size());
    if (readSize >= 0) {
      view.resetSize(readSize);
      reader.expireView(view);
    }
  } else {
    readSize = SSL_write(dp->sslHandler, (void *)inData, len);
    if (readSize >= 0 && readSize < len) {
      dp->inBuffer.Append(inData + readSize, len - readSize);
    }
  }

  if (readSize < 0) {
    auto r = SSL_get_error(ssl, readSize);
    auto err = ERR_error_string(r, 0);
    Logger::instance()->log(MODULE_NAME, lvl::warn,
                            "SSL read error, reason \"{}\"", err);
    throw SSLDataException("SSL Write encrypt data error.");
  }

  // read encrypted data from bio
  BIO *ioOut = dp->wbio;
  auto pending = BIO_ctrl_pending(ioOut);
  std::unique_ptr<char[]> temp(new char[pending]);
  // Copy 1.
  int writeSize = BIO_read(ioOut, (void *)temp.get(), pending);
  // Copy 2.
  dp->outBuffer.Append(temp.get(), writeSize);
  return FBufferReader(dp->outBuffer);
  // And for send --> Copy 3 may happen QAQ
}

FBufferReader FSSLHelper::DecryptRecvingData(FBufferReader &reader) {
  SSL *ssl = dp->sslHandler;
  if (!hasShakeHandFin()) {
    throw SSLNotPreparedException();
  }
  BIO *ioIn = dp->rbio;
  BIO *ioOut = dp->wbio;

  auto view = reader.peekAll();
  // write encrypted data into bio

  auto size = BIO_write(ioIn, (unsigned char *)view.get(), view.size());
  view.resetSize(size);
  reader.expireView(view);

  // read decrypted data out of ssl
  int toReadSize = SSL_pending(dp->sslHandler) + size;

  //To read size --> a predicted reading size.
  std::unique_ptr<char[]> temp(new char[toReadSize]);
  int readSize = SSL_read(dp->sslHandler, temp.get(), toReadSize);
    if (readSize <= 0) {
    auto r = SSL_get_error(ssl, toReadSize);
    auto err = ERR_error_string(r, 0);
    Logger::instance()->log(MODULE_NAME, lvl::warn,
                            "SSL read error, reason \"{}\"", err);
    throw SSLDataException("SSL Write decrypt data error.");
  }
  
  dp->outBuffer.Append(temp.get(), readSize);
  return dp->outBuffer;
}

void FSSLUtils::randomBytes(unsigned char* data,uint32 num){
  RAND_bytes(data,(int)num);
}

} // namespace Fei