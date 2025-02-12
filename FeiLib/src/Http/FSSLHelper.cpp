
#include <cassert>
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
};

FSSLHelper::~FSSLHelper(){}

FSSLHelper::FSSLHelper() : dp(new _FSSLHelperPrivate) {
  if (!SSLContext)
    return;
  sslHandler = SSL_new((SSL_CTX *)SSLContext);
  rbio = BIO_new(BIO_s_mem()); // 读 BIO，模拟接收数据
  wbio = BIO_new(BIO_s_mem()); // 写 BIO，模拟发送数据
  SSL_set_bio((SSL *)sslHandler, (BIO *)rbio, (BIO *)wbio);
  SSL_set_accept_state((SSL *)sslHandler);
}
bool FSSLHelper::shakeHand(const FTcpConnPtr &ptr, FBufferReader &reader) {
  SSL *ssl = (SSL *)sslHandler;
  if (SSL_is_init_finished(ssl))
    return true;

  BIO *out_bio = (BIO *)rbio;
  BIO *in_bio = (BIO *)wbio;
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

// TODO:
FBufferView FSSLHelper::EncryptSendingData(const std::string &inData) {
  SSL *ssl = (SSL *)sslHandler;
  if (!SSL_is_init_finished(ssl)) {
    throw SSLNotPreparedException();
  }
}
FBufferView FSSLHelper::DecryptRecvingData(FBufferReader &reader) {
  SSL *ssl = (SSL *)sslHandler;
  if (!SSL_is_init_finished(ssl)) {
    throw SSLNotPreparedException();
  }
}

} // namespace Fei::Http