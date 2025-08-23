
#include <cassert>
#include <memory>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <string>
#include <utility>

#include "FBuffer.h"
#include "FBufferReader.h"
#include "FException.h"
#include "FLogger.h"
#include "FSSLHelper.h"
#include "FTCPConnection.h"

#include "openssl/opensslv.h"

#include "openssl/rand.h"
#include <openssl/hmac.h>
#include <openssl/md5.h>


#include "Http/FHttp2Helper.h"

#define MODULE_NAME "SSLHelper"

static int alpn_select_proto_cb(SSL* ssl, const unsigned char** out,
    unsigned char* outlen, const unsigned char* in,
    unsigned int inlen, void* arg) {
    int rv;

    rv = Fei::Http::FHttp2Context::select_alpn(out, outlen, in, inlen);

    if (rv == -1) {
        return SSL_TLSEXT_ERR_NOACK;
    }

    return SSL_TLSEXT_ERR_OK;
}

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
  SSL_CTX_set_min_proto_version((SSL_CTX*)SSLContext, TLS1_2_VERSION);
  // optional: SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);
  SSL_CTX_set_options((SSL_CTX*)SSLContext, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1);
  SSL_CTX_set_alpn_select_cb((SSL_CTX*)SSLContext, alpn_select_proto_cb, NULL);
  loadCertFiles(certificateFile, privateKeyFile);
}

void FSSLEnv::loadCertFiles(const std::string &certificateFile,
                            const std::string &privateKeyFile) {
  if (!SSLContext) {
    auto errCode = ERR_get_error();
    auto reason = ERR_GET_REASON(errCode);
    Logger::instance()->log(MODULE_NAME, lvl::err,
                            "Unable to create SSL context, reason: \"{}\"",
                            reason);
    return;
  }
  SSL_CTX *ctx = (SSL_CTX *)SSLContext;
  /* Set the key and cert */
  if (SSL_CTX_use_certificate_chain_file(ctx, certificateFile.c_str()) <= 0) {
    auto errCode = ERR_get_error();
    auto reason = ERR_GET_REASON(errCode);
    Logger::instance()->log(
        MODULE_NAME, lvl::err,
        "Unable to load SSL certificate file, reason: \"{}\"", reason);
    return;
  }

  if (SSL_CTX_use_PrivateKey_file(ctx, privateKeyFile.c_str(),
                                  SSL_FILETYPE_PEM) <= 0) {
    auto errCode = ERR_get_error();
    auto reason = ERR_GET_REASON(errCode);
    Logger::instance()->log(
        MODULE_NAME, lvl::err,
        "Unable to load SSL private key file, reason: \"{}\"", reason);
    return;
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
  bool mIsH2 = false;
  bool mSelectedH2 = false;
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
    int size = 0;
    auto data = reader.peekAll(size);

    int read = BIO_write(in_bio, (const void *)data, size);
    if (read > 0) {
      reader.expireSize(read);
    }
  }

  r = SSL_do_handshake(ssl);
  if (r < 0) {
    auto pending = BIO_ctrl_pending(out_bio);
    std::string dataToSend;
    dataToSend.resize(pending);
    pending = BIO_read(out_bio, dataToSend.data(), dataToSend.size());
    ptr->send(std::move(dataToSend));
  } else {
    SSL_read(ssl, 0, 0);
    if (SSL_pending(ssl) > 0) {
      return true;
    }
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
    int size = 0;
    auto data = reader.peekAll(size);
    readSize = SSL_write(dp->sslHandler, (const void *)data, size);
    if (readSize >= 0) {
      reader.expireSize(readSize);
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

  // 1. Pull encrypted bytes from reader and feed into SSL BIO
  int encryptedSize = 0;
  const unsigned char *encryptedData =
      reinterpret_cast<const unsigned char *>(reader.peekAll(encryptedSize));

  if (encryptedSize > 0) {
    int written = BIO_write(ioIn, encryptedData, encryptedSize);
    reader.expireSize(written);
  }

  // 2. Read all decrypted data from SSL into outBuffer
  const int defaultBufSize = 4096;
  std::unique_ptr<char[]> buffer(new char[defaultBufSize]);

  while (true) {
    // Determine how many bytes we can read without blocking
    int pending = SSL_pending(ssl);
    int toRead = (pending > 0 ? pending : defaultBufSize);
    toRead = std::min(toRead, defaultBufSize);
    // Perform the SSL read
    int readBytes = SSL_read(ssl, buffer.get(), toRead);
    if (readBytes > 0) {
      // Append decrypted bytes to the output buffer
      dp->outBuffer.Append(buffer.get(), readBytes);
      continue; // keep reading until no more pending data
    }

    // Handle zero or error return
    int err = SSL_get_error(ssl, readBytes);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE ||
        err == SSL_ERROR_ZERO_RETURN) {
      // No more data available now or clean shutdown
      break;
    }

    // Unexpected error: log and throw
    char errBuf[256] = {0};
    ERR_error_string_n(ERR_get_error(), errBuf, sizeof(errBuf));
    Logger::instance()->log(MODULE_NAME, lvl::warn, "SSL_read failed: {}",
                            errBuf);
    throw SSLDataException("SSL read decrypt data error.");
  }
  return dp->outBuffer;
}

bool FSSLHelper::isHttp2() const
{
  SSL *ssl = dp->sslHandler;
  if(!dp->mSelectedH2)
  {
      const unsigned char* alpn = NULL;
      unsigned int alpnlen = 0;
      SSL_get0_alpn_selected(ssl, &alpn, &alpnlen);
      if (alpn && alpnlen == 2 && memcmp("h2", alpn, 2) == 0) {
          dp->mIsH2 = true;
      }
      dp->mSelectedH2 = true;
  }
  return dp->mIsH2;
}

void FSSLUtils::randomBytes(unsigned char *data, uint32 num) {
  RAND_bytes(data, (int)num);
}

std::string FSSLUtils::hmac_sha1(const char *data, size_t dataSize,
                                 const char *key, size_t keySize) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digest_len = 0;
  HMAC(EVP_sha1(), key, keySize, (const unsigned char *)data, dataSize, digest,
       &digest_len);
  return std::string((char *)digest, digest_len);
}

std::string FSSLUtils::base64(const char *data, size_t dataSize) {
  size_t enc_len = 4 * ((dataSize + 2) / 3);
  std::vector<unsigned char> buf(enc_len + 1, '\0');
  int out_len = EVP_EncodeBlock((unsigned char *)buf.data(),
                                (const unsigned char *)data, dataSize);
  return std::string((char *)buf.data(), out_len);
}

} // namespace Fei