#ifndef FHTTPDEF_H
#define FHTTPDEF_H
#include "../FDef.h"

namespace Fei {
namespace Http {
enum class Method {
  GET = 0,
  POST = 1,
  HEAD = 2,
  PUT = 3,
  DELETE = 4,
  PATCH = 5,
  CONNECT = 6,
  OPTIONS = 7,
  TRACE = 8,
  Invalid = 9,
  MAX_SIZE = Invalid,
};

const char* methodToStr(Method method);

enum class Version {
  Unknown,
  Http10,
  Http11,
};
const char* versionToStr(Version version);

enum class StatusCode{
//1xx
    _100,
//2xx
    _200,_201,_202,_204,
//3xx
    _301,_302,_303,_304,_307,_308,
//4xx
    _400,_401,_403,_404,_405,_408,_418,_429,
//5xx    
    _500,_501,_502,_505,
};

const char* statusCodeToStr(StatusCode code);

const uint32 HttpMaxRequestPathLen = 4096;

} // namespace Http
} // namespace Fei

#endif