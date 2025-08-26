#include "Http/FHttp2Helper.h"
#include "Http/FHttpDef.h"
#include "Http/FHttpRequest.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <fstream>
#ifdef _WIN32
#define NGHTTP2_NO_SSIZE_T
#endif
#include "nghttp2/nghttp2.h"
#include "FLogger.h"
#include "FBuffer.h"
#include "Http/FHttpResponse.h"
#include "Http/FHttpRequestBuilder.h"

#include "FConfigReader.h"

#define MODULE_NAME "[Http2]"



/*

/index {
    xxx.css,
    xxx.js,
}

*/
using PushRequestMap = std::unordered_map<std::string, std::vector<Fei::Http::FHttpRequest>>;
using PushMap = std::unordered_map<std::string, std::vector<std::string>>;

PushRequestMap parse_push_map_file(const std::string &filename);

PushRequestMap s_pushRequestMap;
std::vector<Fei::Http::FHttpRequest> s_emptyPushVec;
bool s_enablePush = true;
uint32_t s_maxConcurrentStream = 5;
namespace Fei{

    namespace {
        nghttp2_error_code toNgH2Err(Http::H2StreamErr err) {
            switch (err)
            {
            case Fei::Http::H2StreamErr::internal_error:
                return NGHTTP2_INTERNAL_ERROR;
                break;
            case Fei::Http::H2StreamErr::refused_stream:
                return NGHTTP2_REFUSED_STREAM;
                break;
            case Fei::Http::H2StreamErr::cancel:
                return NGHTTP2_CANCEL;
                break;
            case Fei::Http::H2StreamErr::no_error:
                return NGHTTP2_NO_ERROR;
                break;
            default:
                return NGHTTP2_NO_ERROR;
                break;
            }
        }

        void makeNVPair(const char* name,
            int len,
            const char* value,
            int vlen,
            std::vector<uint8_t>& strVec,
            std::vector<nghttp2_nv>& nva,bool zeroCopy = true)
        {
            if (len < 0) len = 0;

            const size_t valueLen = vlen;
            const size_t add = static_cast<size_t>(len) + 1 + valueLen + 1;

            const size_t oldSize = strVec.size();
            strVec.resize(oldSize + add);

            uint8_t* base = strVec.data();
            const size_t nameOffset = oldSize;
            const size_t valueOffset = nameOffset + static_cast<size_t>(len) + 1;
            if(len > 0)
            {
                memcpy(base + nameOffset, name, len);
            }

            base[nameOffset + static_cast<size_t>(len)] = 0;

            if (valueLen > 0) {
                memcpy(base + valueOffset, value, valueLen);
            }
            base[valueOffset + valueLen] = 0;

            nghttp2_nv nv;
            nv.namelen = static_cast<size_t>(len);
            nv.valuelen = valueLen;
            nv.flags =zeroCopy ? (NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE) : 0;
            nva.push_back(nv);
        }

        void makeNVPairToLower(const char* name,
            int len,
            const char* value,
            int vlen,
            std::vector<uint8_t>& strVec,
            std::vector<nghttp2_nv>& nva, bool zeroCopy = true)
        {
            // sanitize lengths
            if (len < 0) len = 0;
            if (vlen < 0) vlen = 0;

            // treat null pointers as empty
            if (name == nullptr) len = 0;
            if (value == nullptr) vlen = 0;

            const size_t nameLen = static_cast<size_t>(len);
            const size_t valueLen = static_cast<size_t>(vlen);

            // bytes to append: name + '\0' + value + '\0'
            const size_t add = nameLen + 1 + valueLen + 1;

            // resize (may reallocate => see note below)
            const size_t oldSize = strVec.size();
            strVec.resize(oldSize + add);

            uint8_t* base = strVec.data();
            const size_t nameOffset = oldSize;
            const size_t valueOffset = nameOffset + nameLen + 1; // +1 for name's '\0'

            // write name in lowercase (ASCII-safe)
            for (size_t i = 0; i < nameLen; ++i) {
                unsigned char uc = static_cast<unsigned char>(name[i]);
                base[nameOffset + i] = static_cast<uint8_t>(std::tolower(uc));
            }
            base[nameOffset + nameLen] = 0; // name terminating '\0'

            // write value raw
            if (valueLen > 0) {
                memcpy(base + valueOffset, value, valueLen);
            }
            base[valueOffset + valueLen] = 0; // value terminating '\0'

            // construct nghttp2_nv (lengths exclude terminating NULL)
            nghttp2_nv nv;
            nv.namelen = nameLen;
            nv.valuelen = valueLen;
            nv.flags = zeroCopy ? (NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE) : 0;
            //nv.flags = 0;
            nva.push_back(nv);
        }

        void collectAndReasignNva(uint8_t* base,std::vector<nghttp2_nv>& nva){
            size_t offset = 0;
            for (auto& nv : nva) {
                nv.name = base + offset;
                offset += nv.namelen + 1;
                nv.value = base + offset;
                offset += nv.valuelen + 1;
            }
        }

        //(optional) for push promise, there should have a pre-send header frame contains persudo headers
        void generatePushPromiseHeaderFromRequest(const Http::FHttpRequest& requset, const std::string& host, std::vector<uint8_t>& headerStringDataOut, std::vector< nghttp2_nv>& nva) {
            //1. method = ...      
            {
                auto method = requset.getMethod();
                auto methodStr = Http::methodToStr(method);
                auto methodLen = strlen(methodStr);
                makeNVPair(":method", 7, Http::methodToStr(method), methodLen, headerStringDataOut, nva);
            }

            //2. scheme = https   
            {
                makeNVPair(":scheme", 7, "https", 5, headerStringDataOut, nva);
            }

            //3. authority = <same as request>
            {
                makeNVPair(":authority", 10, host.c_str(), host.size(), headerStringDataOut, nva);
            }
            //4. path = ...    
            {
                makeNVPair(":path", 5, requset.getPath().c_str(), requset.getPath().size(), headerStringDataOut, nva);
            }

            auto toHeadersHttp2 = [&headerStringDataOut, &nva](const std::pair<std::string, std::string>& pair) {
                makeNVPairToLower(pair.first.c_str(), pair.first.size(), pair.second.c_str(), pair.second.size(), headerStringDataOut, nva);
                return true;
            };
            requset.traverseHeaders(toHeadersHttp2);
            collectAndReasignNva(headerStringDataOut.data(), nva);
        }
    }

    //
    class FHttp2Response {
    public:
        //This will remove some headers from response
        FHttp2Response() {}
        FHttp2Response(Http::FHttpResponse& response):mBody(std::move(response.getBody())) {
            addPersudoHead(response);
            addCookies(response);
            transformToLowerCaseAndStore(response);
            collectAndReasignNva(mOutDataString.data(), mNghttp2NVA);
        }
        bool hasBody()const { return !mBody.empty(); }
        const std::string& getBody()const { return mBody; }
        std::vector<nghttp2_nv>& getNgHttp2NameValueArray() { return mNghttp2NVA; }
        uint32_t getDataSendedSize()const { return mDataSendSize; }
        void peakDataSize(uint32_t dataSize) { mDataSendSize += dataSize; }
    private:
        //0. Add persudo head
        void addPersudoHead(const Http::FHttpResponse& response) {
            //":status: xxx"
            makeNVPair(":status", 7, statusCodeToStr2(response.getStatusCode()),3, mOutDataString, mNghttp2NVA);
        }

        //1. Transform Headers to all lower case, and store it inside data.
        void transformToLowerCaseAndStore(const Http::FHttpResponse& response) {
            auto storeInToData = [this](const std::pair<std::string, std::string>& pair) {
                makeNVPairToLower(pair.first.c_str(), pair.first.size(), pair.second.c_str(), pair.second.size(), mOutDataString, mNghttp2NVA);
                return true;
            };
            response.traversalHeader(storeInToData);
            setContentLength(response);
        }
        //3. Add Cookies 
        void addCookies(const Http::FHttpResponse& response) {
            size_t oldSize = mOutDataString.size();
            const auto& cookies = response.getCookies();
            constexpr const char* setCookieStr = "set-cookie";
            constexpr const auto cookieHeaderNameLen = 10;
            for (const auto& cookie : cookies) {
                auto str = cookie.outSetCookieNoHeader();
                makeNVPair("set-cookie", 10, str.c_str(), str.size(), mOutDataString, mNghttp2NVA);
            }
        }

        void setContentLength(const Http::FHttpResponse& response) {
            if (mBody.size() == 0)return;
            auto contentSizeStr = std::to_string(mBody.size());
            makeNVPair("content-length", 14, contentSizeStr.c_str(), contentSizeStr.size(), mOutDataString, mNghttp2NVA);
        }

        //2. Remove specific headers.   ---> not common in server, like "Connection" , "Keep Alive" ""

    public:
        uint32_t mDataSendSize = 0;
        std::vector<uint8_t> mOutDataString;
        std::vector<nghttp2_nv> mNghttp2NVA;

        std::string mBody;
        public:
            nghttp2_data_provider2 dataProvider;
        };

    class FHttp2PushPromise {
    public:
        FHttp2PushPromise() {}
        struct promiseHeaders {
            std::vector<uint8_t> mOutDataString;
            std::vector<nghttp2_nv> mNghttp2NVA;
        }pushPromiseHeader;
    };

    struct Http2SessionSettings {
        uint32_t maxConcurrentStreams = 100;
        uint32_t initialWindowSize = 65535; //default
        uint32_t maxFrameSize = 16384; //default
        uint32_t headerTableSize = 4096; //default
        uint32_t enablePush = 1; //default
    };

    struct Http2StreamData {
        Fei::Http::FHttp2Parser parser;
        std::unique_ptr<FHttp2Response> returnResponse = 0;
        std::unique_ptr<FHttp2PushPromise> pushPromise = 0;
        bool isStreamRecvFinish = false;
        bool isServerOpen = false;
        bool processed = false;
        uint32_t parentStreamId = 0;
        uint32_t streamId;
    };

    struct Http2SessionData {
        Http2SessionData(uint32_t bufferSize):mOutDataBuffer(bufferSize) {
            lastStreamId = 2 * s_maxConcurrentStream - 1; //For client initiated streams, they are odd numbers
        }
        Fei::FBuffer mOutDataBuffer;
        bool mFirstFrame = true;
        bool mHasGoawaySent = false;
        uint32_t openedStreams = 0;
        uint32_t lastStreamId = 0;
        std::map<uint32_t, Http2StreamData> mStreamDataMap;
        Http2SessionSettings settings;
#ifdef HTTP2_DEBUG
        Fei::FSocketAddr addr;
#endif
    };

	class Http2Callbacks {
	public:
        static Http2StreamData* getStreamUserData(nghttp2_session* s, int32_t streamId) {
            Http2StreamData* ret = (Http2StreamData*)nghttp2_session_get_stream_user_data(s, streamId);
            return ret;
        }

        static nghttp2_ssize send_callback(nghttp2_session* s, const uint8_t* data, size_t len, int f, void* ud) {
            Http2SessionData* sd = (Http2SessionData*)ud;
            sd->mOutDataBuffer.Append((const char*)data, len);
            return (nghttp2_ssize)len;
        }

        static int on_begin_headers_callback(nghttp2_session* session,
            const nghttp2_frame* frame,
            void* user_data) {
            Http2SessionData* session_data = (Http2SessionData*)user_data;

            if (frame->hd.type != NGHTTP2_HEADERS ||
                frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
                return 0;
            }
            auto stream_id = frame->hd.stream_id;
            if(stream_id > session_data->lastStreamId){
                if(!session_data->mHasGoawaySent){
                    uint32_t last = nghttp2_session_get_last_proc_stream_id(session);
                    session_data->lastStreamId = std::min(last, session_data->lastStreamId);
                    nghttp2_submit_goaway(session, 0,session_data->lastStreamId , NGHTTP2_NO_ERROR, NULL, 0);
                    session_data->mHasGoawaySent = true;
                }
                nghttp2_submit_rst_stream(session, 0, stream_id, NGHTTP2_REFUSED_STREAM);
                //ignore all headers after goaway sent
                return 0;
            }

            //if (session_data->openedStreams >= 4) {
            //    /* send RST_STREAM to refuse. REFUSED_STREAM means server refuse to serve. */
            //    nghttp2_submit_rst_stream(session,0, frame->hd.stream_id, NGHTTP2_REFUSED_STREAM);
            //    return 0;
            //}

            Http2StreamData* stream_data;

            auto itor = session_data->mStreamDataMap.find(frame->hd.stream_id);
            if (itor == session_data->mStreamDataMap.end()) {
                auto newIt = session_data->mStreamDataMap.insert({ frame->hd.stream_id, Http2StreamData{} });
                stream_data = &newIt.first->second;
            }
            else {
                stream_data = &itor->second;
            }
            stream_data->streamId = frame->hd.stream_id;
            if (!stream_data) {
                nghttp2_submit_rst_stream(session, 0, frame->hd.stream_id, NGHTTP2_INTERNAL_ERROR);
                return 0;
            }
            session_data->openedStreams++;
            nghttp2_session_set_stream_user_data(session, frame->hd.stream_id, stream_data);
            return 0;
        }

        static int on_data_chunk_recv_callback(nghttp2_session* session, uint8_t flags, int32_t stream_id, const uint8_t* data, size_t len, void* user_data) {
            Http2StreamData* streamUD = getStreamUserData(session, stream_id);
            Http2SessionData* sessionData = (Http2SessionData*)user_data;
            
            if(!streamUD){
                return 0;
            }
            
            auto& parser = streamUD->parser;
            parser.appendData((char*)data, len);
            if (flags & NGHTTP2_FLAG_END_STREAM) {
                streamUD->isStreamRecvFinish = true;
            }
            return 0;
        }

        static int on_stream_close_callback(nghttp2_session* session, int32_t stream_id,
            uint32_t error_code, void* user_data) {
            Http2SessionData* session_data = (Http2SessionData*)user_data;
            Http2StreamData* stream_data = (Http2StreamData*)nghttp2_session_get_stream_user_data(session, stream_id);
            if (stream_data) {
                stream_data->returnResponse= 0;
                stream_data->pushPromise = 0;
                session_data->mStreamDataMap.erase(stream_id);
                session_data->openedStreams--;
            }
            return 0;
        }


        static int on_header_callback(nghttp2_session* session, const nghttp2_frame* frame, const uint8_t* name, size_t namelen, const uint8_t* value, size_t valuelen, uint8_t flags, void* user_data)
        {
            auto streamId = frame->hd.stream_id;
            auto sessionData = (Http2SessionData*)user_data;
            Http2StreamData* streamUD = getStreamUserData(session, streamId);
            if(!streamUD)return 0;

            auto& parser = streamUD->parser;
            parser.addHeader(std::string((char*)name, namelen), std::string((char*)value, valuelen));
            
            return 0;

        }

        static int on_settings(Http2SessionData* sessionData,const nghttp2_settings& settingsFrame) {
            auto settings = &sessionData->settings;
            for (size_t i = 0; i < settingsFrame.niv; ++i) {
                auto settingId = settingsFrame.iv[i].settings_id;
                auto settingVal = settingsFrame.iv[i].value;
                switch (settingId) {
                case NGHTTP2_SETTINGS_HEADER_TABLE_SIZE:
                    settings->headerTableSize = settingVal;
                    break;
                case NGHTTP2_SETTINGS_ENABLE_PUSH:
                    settings->enablePush = settingVal;
                    break;
                case NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS:
                    settings->maxConcurrentStreams = settingVal;
                    break;
                case NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE:
                    settings->initialWindowSize = settingVal;
                    break;
                case NGHTTP2_SETTINGS_MAX_FRAME_SIZE:
                    settings->maxFrameSize = settingVal;
                    break;
                case NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE:
                    break;
                default:
                    break;
                }
            }
            return 0;
        }


        //When header/data frames are fully received
        static int on_frame_recv_callback(nghttp2_session* session, const nghttp2_frame* frame, void* user_data) {
            Http2SessionData* sessionData = (Http2SessionData*)user_data;
            Http2StreamData* streamUD = getStreamUserData(session, frame->hd.stream_id);

            if(frame->hd.type == NGHTTP2_SETTINGS){
                on_settings(sessionData,frame->settings);
                return 0;
            }

            if(!streamUD){
                return 0;
            }



            auto streamId = frame->rst_stream.hd.stream_id;
            auto& parser = streamUD->parser;
            auto frameType = frame->hd.type;

#ifdef HTTP2_DEBUG
            char addrChar[IPV6_ADDR_CH_LEN] = {};
            sessionData->addr.toHumanFriendyType(addrChar, IPV6_ADDR_CH_LEN, 0);
#endif

            if (frameType == NGHTTP2_HEADERS) {
                //End of Header Frame
                parser.mHeaderFinish = true;
                parser.onHeaderFinish();

            }
            
            if (frameType == NGHTTP2_DATA) {
                if (parser.mData.size() != parser.mDataLength) {
                    Logger::instance()->log(lvl::warn, MODULE_NAME"Stream {}: Data recv size mismatch with content_length", addrChar);
                }
            }

            return 0;
        }

        static nghttp2_ssize submit_data_read_callback(
            nghttp2_session* session, int32_t stream_id, uint8_t* buf, size_t length,
            uint32_t* data_flags, nghttp2_data_source* source, void* user_data) 
        {
            auto streamUD = getStreamUserData(session, stream_id);
            auto& response = *(streamUD->returnResponse);
            auto dataPtr = response.getBody().data();
            auto dataHasSend = response.getDataSendedSize();
            auto dataNeedToSend = response.getBody().size() - dataHasSend;
            uint32_t toSendDataSize = 0;
            if (dataNeedToSend < length) {
                toSendDataSize = dataNeedToSend;
                *data_flags |= NGHTTP2_DATA_FLAG_EOF;
            }
            else {
                toSendDataSize = length;
            }
            memcpy(buf, dataPtr + dataHasSend, toSendDataSize);
            response.peakDataSize(toSendDataSize);
            return toSendDataSize;
        }
	};
}

namespace Fei::Http {
    class FHttp2Private {
    public:
        FHttp2Private(int sendBufferSize, FSocketAddr addr) :sessionData(sendBufferSize) {
            nghttp2_session_callbacks_new(&cb);
            nghttp2_session_callbacks_set_send_callback2(cb, Http2Callbacks::send_callback);
            nghttp2_session_callbacks_set_on_frame_recv_callback(cb, Http2Callbacks::on_frame_recv_callback);
            nghttp2_session_callbacks_set_on_header_callback(cb, Http2Callbacks::on_header_callback);
            nghttp2_session_callbacks_set_on_stream_close_callback(cb, Http2Callbacks::on_stream_close_callback);
            nghttp2_session_callbacks_set_on_begin_headers_callback(cb, Http2Callbacks::on_begin_headers_callback);
            nghttp2_session_callbacks_set_on_data_chunk_recv_callback(cb, Http2Callbacks::on_data_chunk_recv_callback);
            nghttp2_session_server_new(&session, cb, &sessionData);

#ifdef HTTP2_DEBUG
            sessionData.addr = addr;
#endif
        }

        ~FHttp2Private() {
            nghttp2_session_del(session);
            session = 0;
            nghttp2_session_callbacks_del(cb);
            cb = 0;
        }
        Http2SessionData sessionData;
        nghttp2_session* session = 0;
        nghttp2_session_callbacks* cb;
    };

    int FHttp2Context::select_alpn(const unsigned char** out, unsigned char* outlen, const unsigned char* in, unsigned int inlen)
    {
        return nghttp2_select_alpn(out, outlen, in, inlen);
    }

    void FHttp2Context::loadConfig(){
        auto config = Fei::FConfigReader::instance();
        auto enablePush = config->getCfg("H2EnablePush");
        if(enablePush.has_value()){
            if(enablePush.value()=="0"){
                s_enablePush = false;
            }
            else{
                s_enablePush = true;
            }
        }
        
        auto maxStreamNum = config->getCfg("H2MaxStreamNum");
        if(maxStreamNum.has_value()){
            try{
                auto num = std::stoul(maxStreamNum.value());
                if(num >= 1){
                    s_maxConcurrentStream = num;
                }
            }
            catch(...){
                Logger::instance()->log(lvl::warn, MODULE_NAME"Invalid H2MaxStreamNum config value: {}", maxStreamNum.value());
            }
        }
        Logger::instance()->log(lvl::info, MODULE_NAME"Load Config: H2EnablePush={}, H2MaxStreamNum={}", s_enablePush ? 1 : 0, s_maxConcurrentStream);
    }

    void FHttp2Context::loadPushPromiseData(const std::string& path){
        s_pushRequestMap = parse_push_map_file(path);
    }

    const std::vector<Fei::Http::FHttpRequest>& FHttp2Context::getPushPromise(const std::string& getPushPromisePath){
        auto itor = s_pushRequestMap.find(getPushPromisePath);
        if(itor != s_pushRequestMap.end()){
            return itor->second;
        }
        return s_emptyPushVec;
    }

    FHttp2Context::FHttp2Context(FSocketAddr addr):mDp(new FHttp2Private(1024,addr)){
    }

    FHttp2Context::~FHttp2Context()
    {
        mDp = 0;
    }

    void FHttp2Context::http2SubmitResponseStream(uint32_t streamId, FHttpResponse& response, bool closeStream)
    {

        Http2StreamData* streamUD = 0;
        Http2SessionData& sessionData = mDp->sessionData;
        auto session = mDp->session;

        streamUD = (Http2StreamData*)nghttp2_session_get_stream_user_data(mDp->session, streamId);

        streamUD->returnResponse =std::make_unique<FHttp2Response>(response);
        auto& h2Response = streamUD->returnResponse;
        auto & dataProvider = streamUD->returnResponse->dataProvider;
        dataProvider.source.ptr = nullptr;
        dataProvider.read_callback = Http2Callbacks::submit_data_read_callback;
        auto& h2NVA = h2Response->getNgHttp2NameValueArray();
        //DO NOT USE SUBMIT HEADER AND SUBMIT DATA
        auto res = nghttp2_submit_response2(session, streamId, h2NVA.data(), h2NVA.size(), &streamUD->returnResponse->dataProvider);
        if (res != 0) {
            Logger::instance()->log(lvl::err, MODULE_NAME"Submit response error.");
        }
    }

    void FHttp2Context::http2SubmitPushPromise(uint32_t streamId, FHttpRequest& pushRequest, FHttpResponse& pushResponse, bool autoHostSet)
    {
        Http2SessionData& sessionData = mDp->sessionData;
        if(!sessionData.settings.enablePush || !s_enablePush){
            Logger::instance()->log(lvl::info, MODULE_NAME"Push Promise Error cause client disabled push");
            return;
        }
        Http2StreamData* streamUD = 0;
        
        auto session = mDp->session;
        //1. find a parent stream
        const Http2StreamData* parStreamData = 0;
        parStreamData = (Http2StreamData*)nghttp2_session_get_stream_user_data(session, streamId);
        if (!parStreamData) {
            Logger::instance()->log(lvl::err, MODULE_NAME"Push Promise Error cause couldn't find a parent stream");
            return;
        }

        auto pushRequestNew = pushRequest;

        std::string hostVal;
        if (autoHostSet) {
            const auto& headers = parStreamData->parser.mHeaders;
            auto itor = headers.find(":authority");
            if (itor != headers.end()) {
                hostVal = itor->second;
            }
        }
        else {
            pushRequestNew.getHeader(":authority", hostVal);
            pushRequestNew.eraseHeader(":authority");
        }
        std::unique_ptr<FHttp2PushPromise> pushPromise = std::make_unique<FHttp2PushPromise>();

        auto& pushPromiseHeader = pushPromise->pushPromiseHeader;
        generatePushPromiseHeaderFromRequest(pushRequest, hostVal, pushPromiseHeader.mOutDataString, pushPromiseHeader.mNghttp2NVA);
        auto newStreamId = nghttp2_submit_push_promise(session, NGHTTP2_FLAG_END_HEADERS, parStreamData->streamId, pushPromiseHeader.mNghttp2NVA.data(), pushPromiseHeader.mNghttp2NVA.size(), 0);
        if (newStreamId > 0) {
            streamUD = (Http2StreamData*)nghttp2_session_get_stream_user_data(session, newStreamId);
            streamUD->pushPromise = std::move(pushPromise);
            streamUD->isServerOpen = true;
            http2SubmitResponseStream(newStreamId, pushResponse, true);
        }
    }

    uint32_t FHttp2Context::http2SendProcess()
    {
        uint32_t needToSendSize = 0;
        auto session = mDp->session;
        while(nghttp2_session_want_write(session)){

            const uint8_t* dataPtr = 0;
            auto curNeedToSend = nghttp2_session_mem_send2(mDp->session,& dataPtr);
            needToSendSize += curNeedToSend;
            this->mDp->sessionData.mOutDataBuffer.Append((char*)dataPtr, curNeedToSend);
        }
        return needToSendSize;
    }

    FBufferReader FHttp2Context::getSendBufferReader()
    {
        return FBufferReader(mDp->sessionData.mOutDataBuffer);
    }

    uint32_t FHttp2Context::getOpenedStreams() const
    {
        return mDp->sessionData.openedStreams;
    }
    bool FHttp2Context::enablePush()const{
        return s_enablePush && mDp->sessionData.settings.enablePush;
    }

    void FHttp2Context::http2SubmitGoaway()
    {
        auto session = mDp->session;
        auto sessionData = &mDp->sessionData;
        if(sessionData->mHasGoawaySent)return;
        uint32_t last = nghttp2_session_get_last_proc_stream_id(session);
        sessionData->lastStreamId = std::min(last, sessionData->lastStreamId);
        nghttp2_submit_goaway(session, NGHTTP2_FLAG_NONE, last, NGHTTP2_NO_ERROR, NULL, 0);
        sessionData->mHasGoawaySent = true;
    }

    uint32_t FHttp2Context::http2RecvProcess(FBufferReader& reader)
    {
        auto session = mDp->session;
        uint32_t recvLen = 0;
        
        auto& sessionData = mDp->sessionData;
        
        if(sessionData.mFirstFrame){
            //do some thing?
            sessionData.mFirstFrame = false;
        }

        while(nghttp2_session_want_read(session) ){
            int peakNum = 0;
            auto dataPtr = reader.peekAll(peakNum);
            if(peakNum == 0)break;
            auto curRecvLen = nghttp2_session_mem_recv2(mDp->session,(uint8_t*)dataPtr, peakNum);
            recvLen += curRecvLen;
            reader.expireSize(curRecvLen);
        }
        return recvLen;
    }

    bool FHttp2Context::isConnEnd() const
    {
        return mDp->sessionData.openedStreams == 0;
    }

    void FHttp2Context::endStream(uint32_t streamID, H2StreamErr err)
    {
        auto session = mDp->session;
        auto itor = mDp->sessionData.mStreamDataMap.find(streamID);
        if (itor != mDp->sessionData.mStreamDataMap.end()) {
            auto res = nghttp2_submit_rst_stream(session, 0, streamID, toNgH2Err(err));
            if (res != 0) {
                Logger::instance()->log(lvl::err, MODULE_NAME "Submit rst stream error");
            }
        }
        else {
            Logger::instance()->log(lvl::err, MODULE_NAME "Stream close error for id not found");
        }
    }

    void FHttp2Context::traversalFinishedStreams(std::function<bool(const FHttpRequest& ,uint32_t)> func)
    {
        for (auto& [streamID, streamData] : mDp->sessionData.mStreamDataMap) {
            if (streamData.parser.isFinish() && !streamData.processed) {
                FHttpRequest request(streamData.parser);
                bool shouldCon = func(request, streamID);
                streamData.processed = true;
                if (!shouldCon)return;
            }
        }
    }

    void FHttp2Parser::onHeaderFinish()
    {
        //1. method  
        auto itor = mHeaders.find(":method");
        if (itor != mHeaders.end()) {
            const auto& methodStr = itor->second;
            mMethod = stringToMethod(methodStr);
            mHeaders.erase(itor);
        }

#ifdef HTTP2_DEBUG
        itor = mHeaders.find(":scheme");
        if (itor != mHeaders.end()) {
            if (itor->second != "https") {
                Logger::instance()->log(lvl::warn, MODULE_NAME "None https scheme");
            }
            mHeaders.erase(itor);
        }
#endif // HTTP2_DEBUG

        itor = mHeaders.find(":authority");
        if (itor != mHeaders.end()) {
            mHeaders.insert({ "Host",itor->second });
            mHeaders.erase(itor);
        }

        itor = mHeaders.find(":path");
        if (itor != mHeaders.end()) {
            mPath = itor->second;
            mHeaders.erase(itor);
        }

        itor = mHeaders.find("content-length");
        if (itor != mHeaders.end()) {
            mDataLength = std::stoul(itor->second);
        }
        else {
            mDataFinish = true;
        }
    }

    bool FHttp2Parser::isFinish() const
    {
        return mHeaderFinish && mDataFinish;
    }

}

// Trim helpers
static inline std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b-1])) --b;
    return s.substr(a, b-a);
}

static inline std::string strip_quotes(const std::string &s) {
    if (s.size() >= 2) {
        if ((s.front() == '"' && s.back() == '"') || (s.front() == '\'' && s.back() == '\'')) {
            return s.substr(1, s.size() - 2);
        }
    }
    return s;
}

static inline void split_comma_separated(const std::string &line, std::vector<std::string> &out) {
    std::string cur;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == ',') {
            std::string t = trim(cur);
            if (!t.empty()) out.push_back(strip_quotes(t));
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    // last token (may not end with comma)
    std::string t = trim(cur);
    if (!t.empty()) out.push_back(strip_quotes(t));
}

PushRequestMap parse_push_map_file(const std::string &filename) {
    PushMap map;
    std::ifstream ifs(filename);
    if (!ifs) throw std::runtime_error("cannot open file: " + filename);

    std::string line;
    std::string currentRequest;
    bool inBlock = false;

    while (std::getline(ifs, line)) {
        std::string raw = line;
        std::string s = trim(line);
        if (s.empty()) continue;
        if (s[0] == '#') continue; // comment

        // If not currently in a block, look for "requestPath {"
        if (!inBlock) {
            // find '{' in the line
            auto posBrace = s.find('{');
            if (posBrace == std::string::npos) {
                // ignore malformed lines (or could log a warning)
                continue;
            }
            // requestPath is everything before '{'
            std::string req = trim(s.substr(0, posBrace));
            req = strip_quotes(req);
            currentRequest = req;
            inBlock = true;

            // There might be content after '{' on the same line (e.g. "/req { /a, /b }")
            std::string after = s.substr(posBrace + 1);
            // If after contains '}', process tokens before '}' and close block immediately
            auto posClose = after.find('}');
            if (posClose != std::string::npos) {
                std::string tokenPart = after.substr(0, posClose);
                std::vector<std::string> tokens;
                split_comma_separated(tokenPart, tokens);
                for (auto &t : tokens) {
                    if (!t.empty()) map[currentRequest].push_back(t);
                }
                inBlock = false;
                currentRequest.clear();
            } else {
                // process tokens in 'after' (could be empty) but stay in block
                std::vector<std::string> tokens;
                split_comma_separated(after, tokens);
                for (auto &t : tokens) if (!t.empty()) map[currentRequest].push_back(t);
            }
            continue;
        }

        // inBlock == true: expect push paths or closing brace
        // allow lines like "  /a, /b," or "  /a," or "}" or "/a, }"
        auto posClose = s.find('}');
        if (posClose != std::string::npos) {
            // there's a close brace on this line. handle portion before '}' and finish block.
            std::string before = s.substr(0, posClose);
            std::vector<std::string> tokens;
            split_comma_separated(before, tokens);
            for (auto &t : tokens) if (!t.empty()) map[currentRequest].push_back(t);
            inBlock = false;
            currentRequest.clear();
            continue;
        } else {
            // normal line inside block: may contain commas or single token
            std::vector<std::string> tokens;
            split_comma_separated(s, tokens);
            for (auto &t : tokens) if (!t.empty()) map[currentRequest].push_back(t);
        }
    }
    PushRequestMap requestMap;
    for(auto& [requestPath, pushPaths]: map){
        std::vector<Fei::Http::FHttpRequest> pushRequests;
        for(auto& path : pushPaths){
            Fei::Http::FHttpRequestBuilder builder;
            builder.setMethod(Fei::Http::Method::GET);
            builder.setUrl(path);
            pushRequests.push_back(Fei::Http::FHttpRequest(builder));
        }
        requestMap[requestPath] = std::move(pushRequests);
    }
    return requestMap;
}