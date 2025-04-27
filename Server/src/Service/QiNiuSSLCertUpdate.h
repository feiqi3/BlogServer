#include "FConfigReader.h"
#include <string>

namespace Blog {
namespace thrird_party {
struct QiNiuAuth;
QiNiuAuth *genQiNiuAuth(const char *Ak, const char *Sk, const char *path,
                        const char *query, const char *body);

void destroyQiNiuAuth(QiNiuAuth **auth);
//---------------------------------------//
bool updateQiNiuSSLCert(const std::string& pri,const std::string& ca,const std::string& domain);
} // namespace thrird_party
} // namespace Blog