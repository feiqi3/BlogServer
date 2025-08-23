#ifndef FHTTP2HELPER_H_
#define FHTTP2HELPER_H_

#include "FDef.h"
#include "FException.h"
#include "FHttpDef.h"
#include "FHttpParserHelper.h"
#include "FHttpRequest.h"
#include "FBufferReader.h"
#include <functional>

#define HTTP2_DEBUG



namespace Fei::Http {
    class FHttpRequestBuilder;
    class FHttpResponse;
    class FHttp2Private;

    enum class H2StreamErr{
        internal_error,
        refused_stream,
        cancel,
        no_error
    };

    class FHttp2Parser {
    public:
        void setPath(const std::string& path) {
            ParserUtils::ParsePathLine(path,mPath,mQuery);
        }
        void setMethod(const std::string& method) {
            mMethod = stringToMethod(method);
        }
        void addHeader(const std::string& name, const std::string& val) {
            mHeaders.insert({ name,val });
        }
        
        bool hasPath() const {
            return mPath.empty();
        }
        
        bool hasMethod() const{
            return mMethod != Method::Invalid;
        }

        void appendData(char* data,int len) {
            if (mData.empty()) {
                mData.reserve(mDataLength);
            }
            if (mData.size() >= mDataLength) {
                mDataFinish = true;
                return;
            }
            mData.append(data, len);
            if (mData.size() > mDataLength) {
                mData.resize(mDataLength);
            }
        }

        void onHeaderFinish();
        bool isFinish()const;
        bool mHeaderFinish = false;
        bool mDataFinish = false;
        uint32_t mDataLength = 0;
        Method mMethod = Method::Invalid;
        std::string mPath;
        std::string mData;
        HttpQueryMap mQuery;
        HeaderMap mHeaders;
    };

    class FHttp2Error : FException {

        virtual std::string reason()const { return"Http2 Error"; }
    };
	class FHttp2Context {
	public:
        static int select_alpn(const unsigned char** out, unsigned char* outlen,
            const unsigned char* in, unsigned int inlen);

	public:
        FHttp2Context(FSocketAddr addr);
        ~FHttp2Context();
        FBufferReader getSendBufferReader();
        void http2SubmitResponseStream(uint32_t streamId,const FHttpResponse& response, bool closeStream);
        void http2SubmitPushPromise(uint32_t streamId, FHttpRequestBuilder& pushRequest, const FHttpResponse& pushResponse,bool autoHostSet = true);
        void http2SubmitGoaway();
        uint32_t http2SendProcess();
        uint32_t http2RecvProcess(FBufferReader& reader);
        bool isConnEnd()const;
        uint32_t getOpenedStreams()const;
        void endStream(uint32_t streamID,H2StreamErr err);
        void traversalFinishedStreams(std::function<bool(const FHttpRequest&,uint32)>func);

    private:

        std::unique_ptr<FHttp2Private> mDp = 0;
        FSocketAddr mAddr;
    };

}

#endif