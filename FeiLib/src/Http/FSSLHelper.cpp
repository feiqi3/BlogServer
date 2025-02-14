
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
#include "FTCPConnection.h"
#include "Http/FSSLHelper.h"

#define MODULE_NAME "SSLHelper"

class SSLNotPreparedException : public Fei::FException {
public:
  std::string reason() const override {
    return "Read or Write After SSL Shake Hand Finish.";
  }
};

namespace Fei::Http {
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
  dp->wbio = BIO_new(BIO_s_mem()); // 写 BIO，模拟发送数据
  SSL_set_bio(dp->sslHandler, dp->rbio, dp->wbio);
  SSL_set_accept_state(dp->sslHandler);
}
bool FSSLHelper::shakeHand(const FTcpConnPtr &ptr, FBufferReader &reader) {
  SSL *ssl = dp->sslHandler;
  if (SSL_is_init_finished(ssl))
    return true;

  BIO *out_bio = dp->rbio;
  BIO *in_bio = dp->wbio;
  auto r = SSL_do_handshake(ssl);
  if (r < 0) {
    r = SSL_get_error(ssl, r);
    if (SSL_ERROR_WANT_READ == r) {
      // Get data from reader
      auto view = reader.peekAll();
      auto pending = BIO_ctrl_pending(out_bio);
      if (pending > 0) {
        int read = BIO_read(out_bio, (void *)view.get(), view.size());
        if (read > 0) {
          view.resetSize(read);
          reader.expireView(view);
        }
      }
    } else if (r == SSL_ERROR_WANT_WRITE) {
      auto pending = BIO_ctrl_pending(out_bio);
      std::string dataToSend;
      dataToSend.resize(pending);
      pending = BIO_write(in_bio, dataToSend.data(), dataToSend.size());
      assert(pending >= 0);
      ptr->send(std::move(dataToSend));
    }
  }
  return false;
}

FBufferReader FSSLHelper::EncryptSendingData(const std::string &inData) {
  SSL *ssl = dp->sslHandler;
  if (!SSL_is_init_finished(ssl)) {
    throw SSLNotPreparedException();
  }
  BIO *ioIn = dp->rbio;

  int readSize = 0;
  if (dp->inBuffer.getReadableSize() > 0) {

    dp->inBuffer.Append(inData.data(), inData.size());
    FBufferReader reader(dp->inBuffer);
    auto view = reader.peekAll();
    readSize = BIO_read(ioIn, (void *)view.get(), view.size());
    if (readSize >= 0) {
      view.resetSize(readSize);
      reader.expireView(view);
    }
  } else {
    readSize = BIO_read(ioIn, (void *)inData.data(), inData.size());
    if (readSize >= 0 && readSize < (int)inData.size()) {
      dp->inBuffer.Append(inData.data() + readSize, inData.size() - readSize);
    }
  }
  if (readSize < 0) {
    auto r = SSL_get_error(ssl, readSize);
    auto err = ERR_error_string(r,0);
    Logger::instance()->log(MODULE_NAME, lvl::warn,
                            "SSL read error, reason \"{}\"", err);
    return FBufferReader(dp->outBuffer);
  }
  BIO *ioOut = dp->wbio;
  auto pending = BIO_ctrl_pending(ioOut);
  std::unique_ptr<char[]> temp(new char[pending]);
  // Copy 1.
  int writeSize = BIO_write(ioOut, (void *)temp.get(), pending);
  if (writeSize < 0) {
    auto r = SSL_get_error(ssl, writeSize);
    auto err = ERR_error_string(r,0);
    Logger::instance()->log(MODULE_NAME, lvl::warn,
                            "SSL write error, reason \"{}\"", err);
    return FBufferReader(dp->outBuffer);
  }
  // Copy 2.
  dp->outBuffer.Append(temp.get(), writeSize);
  return FBufferReader(dp->outBuffer);
  // And for send --> Copy 3 may happen QAQ
}

FBufferReader FSSLHelper::DecryptRecvingData(FBufferReader &reader) {
  SSL *ssl = dp->sslHandler;
  if (!SSL_is_init_finished(ssl)) {
    throw SSLNotPreparedException();
  }
  auto view = reader.peekAll();
  BIO *ioIn = dp->rbio;
  BIO *ioOut = dp->wbio;
  auto read = BIO_read(ioIn, (void *)view.get(), view.size());
  if (read < 0) {
    int r = SSL_get_error(ssl, read);
    auto err = ERR_error_string(r,0);
    Logger::instance()->log(MODULE_NAME, lvl::warn,
                            "SSL read error, reason \"{}\"", err);
    return dp->outBuffer;
  }
  view.resetSize(read);
  reader.expireView(view);
  auto pending = BIO_ctrl_pending(ioOut);
  std::unique_ptr<char[]> temp(new char[pending]);
  // Copy 1.
  int writeSize = BIO_write(ioOut, (void *)temp.get(), pending);
  if (writeSize < 0) {
    int r = SSL_get_error(ssl, writeSize);
    auto err = ERR_error_string(r,0);
    Logger::instance()->log(MODULE_NAME, lvl::warn,
                            "SSL write error, reason \"{}\"", err);
    return dp->outBuffer;
  }
  // Copy 2.
  dp->outBuffer.Append(temp.get(), writeSize);
  return dp->outBuffer;
}

} // namespace Fei::Http