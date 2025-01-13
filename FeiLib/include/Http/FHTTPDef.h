#ifndef FHTTPDEF_H
#define FHTTPDEF_H
#include "../FDef.h"

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
//1xx
    _100,
//2xx
    _200,_201,_202,_204,
//3xx
    _301,_302,_303,_304,_307,_308,
//4xx
    _400,_401,_403,_404,_405,_408,_429,
//5xx    
    _500,_501,_502,_505,
};
} // namespace Http
} // namespace Fei

#endif