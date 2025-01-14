#ifndef FHTTPARSER_H
#define FHTTPARSER_H
#include "FHttpDef.h"
#include "../FBufferReader.h"

#include <string>
#include <map>

namespace Fei::Http {

using HttpQueryMap = std::map<std::string, std::string>;

class F_API FHttpContext {
    public:
    private:
    std::map<std::string,std::string> mHeaders;
};

class FHttpParser {
public:
    FHttpParser(FBufferReader& buffer):mBuffer(buffer) {
    }

     const std::string& MethodToString(Method method);
     Method StringToMethod(const std::string&);
public:
   FHttpContext parse();
private:
   FBufferView parseHost(FBufferReader& inReader);
   FBufferView parseUserAgent(FBufferReader& inReader);

  //Method and method data.
   Http::Method parseMethod(FBufferView& inView, uint32& cursor);
   Http::Version parseVersion(FBufferReader& inReader);
   HttpQueryMap parseQueryInPath(FBufferReader& inReader);
   FBufferView parseBody(FBufferReader& inReader);

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