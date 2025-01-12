#ifndef FHTTPDEF_H
#define FHTTPDEF_H

namespace Fei {
namespace Http {
enum class Method {
  GET,
  POST,
  HEAD,
  PUT,
  DELETE,
  PATCH,
  CONNECT,
  OPTIONS,
  TRACE,
  Invalid,
};

enum class Version {
  Unknown,
  Http10,
  Http11,
};

enum StatusCode{
    
};
} // namespace Http
} // namespace Fei

#endif