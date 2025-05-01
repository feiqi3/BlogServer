#include "QiNiuSSLCertUpdate.h"
#include "Core/JsonTool.h"
#include "FConfigReader.h"
#include "FDef.h"
#include "FLogger.h"
#include "FSocket.h"
#include "Http/FHttpDef.h"
#include "Http/FHttpRequestBuilder.h"
#include "nlohmann/json_fwd.hpp"
#include <chrono>
#include <cstring>
#include <exception>
#include <sstream>
#include <string>
#include <vector>

#include <FSSLHelper.h>

#define CERT_DOMAIN "pic.feiqi3.cn"

namespace Blog::thrird_party {
using namespace Fei;

struct QiNiuAuth {
  std::string outAuthToken;
};

QiNiuAuth *genQiNiuAuth(const char *Ak, const char *Sk, const char *path,
                        const char *query, const char *body) {
  // https://developer.qiniu.com/kodo/6671/historical-document-management-certificate
  auto ret = new QiNiuAuth;
  std::stringstream ss;
  ss << path;
  if (query) {
    ss << "?" << query << "\n";
  }
  if (body) {
    ss << body;
  }

  auto _str = ss.str();
  auto hmac_sha1 = FSSLUtils::hmac_sha1(_str.c_str(), _str.size(), Sk, strlen(Sk));
  auto base64 = FSSLUtils::base64(hmac_sha1.data(), hmac_sha1.size());
  auto ak_len = strlen(Ak);
  int write_pos = ak_len + 1;
  std::vector<char> buf(ak_len + 1 + base64.size());
  for (int i = 0; i < base64.size(); ++i) {
    auto c = base64[i];
    if (c == '+')
      buf[write_pos++] = '-';
    else if (c == '/')
      buf[write_pos++] = '_';
    else if (c == '=')
      break; // Stop writting
    else
      buf[write_pos++] = c;
  }
  memcpy(buf.data(), Ak, ak_len);
  buf[ak_len] = ':';
  ret->outAuthToken =
      std::string((buf.data()), write_pos);
  return ret;
}

void destroyQiNiuAuth(QiNiuAuth **auth) {
  delete *auth;
  *auth = nullptr;
}

bool updateQiNiuSSLCert(const std::string &pri, const std::string &ca,
                        const std::string &domain) {

  auto ak = FConfigReader::instance()->getCfg("QiNiu_Ak");
  if (!ak.has_value()) {
    Logger::instance()->log(
        lvl::info,
        "Try Update Qiniu SSL Cert But No Access Key was set in config");
  }

  auto sk = FConfigReader::instance()->getCfg("QiNiu_Sk");
  if (!sk.has_value()) {
    Logger::instance()->log(
        lvl::info,
        "Try Update Qiniu SSL Cert But No Secret Key was set in config");
    return false;
  }

  const std::string QiNiuCertApi = "http://api.qiniu.com/";
  std::vector<FSocketAddr> addrs;
  Fei::ResolveHost(QiNiuCertApi, 8080, addrs);
  if (addrs.size() < 0) {
    Logger::instance()->log(lvl::err, "Resolve address: {} error.",
                            QiNiuCertApi);
    return false;
  }

  Http::FHttpRequestBuilder builder;
  builder.setMethod(Fei::Http::Method::POST)
      .setUrl("/sslcert")
      .addHeader("Content-Type", "application/json");
  nlohmann::json j;
  j["name"] = domain + "_server_ssl_" +
              std::to_string(
                  std::chrono::system_clock::now().time_since_epoch().count());
  j["common_name"] = domain;
  j["pri"] = pri;
  j["ca"] = ca;
  auto body = j.dump();
  auto auth =
      genQiNiuAuth(ak->c_str(), sk->c_str(), "/sslcert", 0, body.c_str());

  builder.addHeader("Authorization", auth->outAuthToken);
  destroyQiNiuAuth(&auth);

  auto &addr = addrs[0];
  Socket s;
  Create(s);
  if (Connect(s, addr) != SocketStatus::Success) {
    Logger::instance()->log(lvl::err, "Connect to {} error.", QiNiuCertApi);
    Close(s);
    return false;
  }
  SetSockOpt(s, SockOpt::SendTimeOut, 1000 * 1000 * 5);
  SetSockOpt(s, SockOpt::ReadTimeOut, 1000 * 1000 * 5);
  // Send Request
  auto req = builder.build();
  int writeLen;
  Logger::instance()->log(lvl::info, "Try Update Qiniu SSL Cert");
  if (Send(s, req.c_str(), req.length(), writeLen) != SocketStatus::Success) {
    Close(s);

    return false;
  }
  std::vector<char> recvBuf(1024);
  int recv_len;
  Recv(s, recvBuf.data(), recvBuf.size() - 1, RecvFlag::None, recv_len);
  Logger::instance()->log(lvl::info, "Get Message from qiniu: {}",
                          recvBuf.data());
  Close(s);
  return true;
}

} // namespace Blog::thrird_party