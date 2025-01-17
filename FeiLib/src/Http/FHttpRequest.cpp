#include "Http/FHttpRequest.h"
#include "Http/FHttpRequestParser.h"
#include <memory>

namespace Fei::Http {

FHttpRequest::FHttpRequest(FBufferReader &buffer)
    : mHttpCtx(std::make_shared<FHttpContext>()) {
  FHttpParser parser(buffer);
  mIsValid = parser.parse(*mHttpCtx);
}

}; // namespace Fei::Http
