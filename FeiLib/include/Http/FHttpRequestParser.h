#ifndef FHTTPARSER_H
#define FHTTPARSER_H
#include "FHttpDef.h"
#include "../FBufferReader.h"
#include "Http/FHttpDef.h"

#include <string>
#include <map>

namespace Fei::Http {

using HttpQueryMap = std::map<std::string, std::string>;
using HeaderMap = std::map<std::string, std::string>;
class F_API FHttpContext {
    public:
    friend class FHttpParser;
    FHttpContext() {}
    public:
    private:
    Method mMethod;
    HeaderMap mHeaders;
};

class F_API FHttpParser {
public:
    FHttpParser(FBufferReader& buffer):mBuffer(buffer) {
    }

     const std::string& MethodToString(Method method);
     Method StringToMethod(const std::string&);
public:
   bool parse(FHttpContext& ctx);
private:
   FBufferView parseHost(FBufferReader& inReader);
   FBufferView parseUserAgent(FBufferReader& inReader);

  //Method and method data.
   Http::Method parseMethod(FBufferView& inView, uint32& cursor);
   bool parseVersion(FBufferView& view,Http::Version &outVersion, uint32& cursor);
   bool parsePath(FBufferView& inView,std::string& outPath,HttpQueryMap& outmap,uint32& cursor);
   HeaderMap parseHeader(FBufferView& oldView);

   FBufferView newLine(FBufferView* lastView);

private:
    struct{
        uint32 line = 0;
        uint32 ch = 0;
        uint32 curLineBeg = 0;
        //TODO: parse phase
    } _parseContext;
    FBufferReader& mBuffer;
};
} // namespace Fei
#endif