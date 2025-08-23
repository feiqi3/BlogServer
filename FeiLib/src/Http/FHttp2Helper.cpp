#include "Http/FHttp2Helper.h"
#ifdef _WIN32
#define NGHTTP2_NO_SSIZE_T
#endif
#include "nghttp2/nghttp2.h"
#include "FLogger.h"
#include "FBuffer.h"
#include "Http/FHttpResponse.h"
#include "Http/FHttpRequestBuilder.h"
#define MODULE_NAME "[Http2]"

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

            for (int i = 0; i < len; ++i) {
                unsigned char uc = static_cast<unsigned char>(name[i]);
                base[nameOffset + static_cast<size_t>(i)] =
                    static_cast<uint8_t>(std::tolower(uc));
            }
            base[nameOffset + static_cast<size_t>(len)] = 0;

            if (valueLen > 0) {
                memcpy(base + valueOffset, value, valueLen);
            }
            base[valueOffset + valueLen] = 0;

            nghttp2_nv nv;
            nv.name = base + nameOffset;
            nv.namelen = static_cast<size_t>(len);
            nv.value = base + valueOffset;
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
            nv.name = base + nameOffset;
            nv.namelen = nameLen;
            nv.value = base + valueOffset;
            nv.valuelen = valueLen;
            nv.flags = zeroCopy ? (NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE) : 0;

            nva.push_back(nv);
        }

        //(optional) for push promise, there should have a pre-send header frame contains persudo headers
        void generatePushPromiseHeaderFromRequestBuilder(const Http::FHttpRequestBuilder& builder, const std::string& host, std::vector<uint8_t>& headerStringDataOut, std::vector< nghttp2_nv>& nva) {
            //1. method = ...     
            {
                auto method = builder.getMethod();
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
                makeNVPair(":path", 5, builder.getUrl().c_str(), builder.getUrl().size(), headerStringDataOut, nva);
            }

            auto toHeadersHttp2 = [&headerStringDataOut, &nva](const std::pair<std::string, std::string>& pair) {
                makeNVPairToLower(pair.first.c_str(), pair.first.size(), pair.second.c_str(), pair.second.size(), headerStringDataOut, nva);
                return true;
            };
            builder.traversalHeaders(toHeadersHttp2);
        }
    }

    //
    class FHttp2Response {
    public:
        //This will remove some headers from response
        FHttp2Response() {}
        FHttp2Response(const Http::FHttpResponse& response):mDatView(response.getBody()) {
            addPersudoHead(response);
            addCookies(response);
            transformToLowerCaseAndStore(response);
        }
        bool hasBody()const { return !mDatView.empty(); }
        const std::string_view& getBody()const { return mDatView; }
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
            if (mDatView.size() == 0)return;
            makeNVPair("content-length", 14, mDatView.data(), mDatView.size(), mOutDataString, mNghttp2NVA);
        }

        //2. Remove specific headers.   ---> not common in server, like "Connection" , "Keep Alive" ""

    public:
        uint32_t mDataSendSize = 0;
        std::vector<uint8_t> mOutDataString;
        std::vector<nghttp2_nv> mNghttp2NVA;

        std::string_view mDatView;
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

    struct Http2StreamData {
        Fei::Http::FHttp2Parser parser;
        FHttp2Response* returnResponse = 0;
        std::unique_ptr<FHttp2PushPromise> pushPromise = 0;
        bool isEnd = false;
        bool isServerOpen = false;
        bool processed = false;
        uint32_t parentStreamId = 0;
        uint32_t streamId;
    };

    struct Http2SessionData {
        Http2SessionData(uint32_t bufferSize):mOutDataBuffer(bufferSize) {
        }
        Fei::FBuffer mOutDataBuffer;
        uint32_t openedStreams = 0;
        std::map<uint32_t, Http2StreamData> mStreamDataMap;
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
            auto& parser = streamUD->parser;
            parser.appendData((char*)data, len);
            if (flags & NGHTTP2_FLAG_END_STREAM) {
                streamUD->isEnd = true;
            }
            return 0;
        }

        static int on_stream_close_callback(nghttp2_session* session, int32_t stream_id,
            uint32_t error_code, void* user_data) {
            Http2SessionData* session_data = (Http2SessionData*)user_data;
            Http2StreamData* stream_data = (Http2StreamData*)nghttp2_session_get_stream_user_data(session, stream_id);
            if (stream_data) {
                delete stream_data->returnResponse;
                stream_data->pushPromise = 0;
                session_data->mStreamDataMap.erase(stream_id);
                session_data->openedStreams--;
            }
            return 0;
        }


        static int on_header_callback(nghttp2_session* session, const nghttp2_frame* frame, const uint8_t* name, size_t namelen, const uint8_t* value, size_t valuelen, uint8_t flags, void* user_data)
        {
            auto streamId = frame->rst_stream.hd.stream_id;
            Http2StreamData* streamUD = getStreamUserData(session, streamId);
            auto& parser = streamUD->parser;
            parser.addHeader(std::string((char*)name, namelen), std::string((char*)value, valuelen));
            
            return 0;

        }

        //When header/data frames are fully received
        static int on_frame_recv_callback(nghttp2_session* session, const nghttp2_frame* frame, void* user_data) {
            auto streamId = frame->rst_stream.hd.stream_id;
            Http2StreamData* streamUD = getStreamUserData(session, streamId);
            Http2SessionData* sessionData = (Http2SessionData*)user_data;
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
            memcpy(buf, dataPtr, toSendDataSize);
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

    FHttp2Context::FHttp2Context(FSocketAddr addr):mDp(new FHttp2Private(1024,addr)){

    }

    FHttp2Context::~FHttp2Context()
    {
        mDp = 0;
    }

    void FHttp2Context::http2SubmitResponseStream(uint32_t streamId, const FHttpResponse& response, bool closeStream)
    {

        FHttp2Response* h2Response = 0;
        Http2StreamData* streamUD = 0;
        Http2SessionData& sessionData = mDp->sessionData;
        auto session = mDp->session;
//        if (!pushPromise) {
        streamUD = (Http2StreamData*)nghttp2_session_get_stream_user_data(mDp->session, streamId);

        //}
        //else {
        //    if (sessionData.openedStreams == 0) {
        //        Logger::instance()->log(lvl::err, MODULE_NAME"Submit a push promise need an existed stream.");
        //        return;
        //    }
        //    auto parentStreamId = sessionData.mStreamDataMap.begin()->first;
        //}

        streamUD->returnResponse = new FHttp2Response(response);
        h2Response = streamUD->returnResponse;

        //if (!pushPromise) {
        auto& h2NVA = h2Response->getNgHttp2NameValueArray();
        //DO NOT USE SUBMIT HEADER AND SUBMIT DATA
        auto res = nghttp2_submit_response2(session, streamId, h2NVA.data(), h2NVA.size(), &streamUD->returnResponse->dataProvider);
        if (res != 0) {
            Logger::instance()->log(lvl::err, MODULE_NAME"Submit response error.");
        }
        //}
        //else {
        //   //1. push promise need to use an exsited stream 

        //    nghttp2_submit_push_promise(session,0,)
        //        streamUD = &(sessionData.mStreamDataMap.insert({ streamId,{} }).first->second);
        //}
    }

    void FHttp2Context::http2SubmitPushPromise(uint32_t streamId, FHttpRequestBuilder& pushRequest, const FHttpResponse& pushResponse, bool autoHostSet)
    {
        Http2StreamData* streamUD = 0;
        Http2SessionData& sessionData = mDp->sessionData;
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
            pushRequestNew.findHeader(":authority", hostVal);
            pushRequestNew.removeHeader(":authority");
        }
        std::unique_ptr<FHttp2PushPromise> pushPromise = std::make_unique<FHttp2PushPromise>();

        auto& pushPromiseHeader = pushPromise->pushPromiseHeader;
        generatePushPromiseHeaderFromRequestBuilder(pushRequest, hostVal, pushPromiseHeader.mOutDataString, pushPromiseHeader.mNghttp2NVA);
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
        const uint8_t* dataPtr = 0;
        auto needToSendSize = nghttp2_session_mem_send2(mDp->session,& dataPtr);
        this->mDp->sessionData.mOutDataBuffer.Append((char*)dataPtr, needToSendSize);
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

    void FHttp2Context::http2SubmitGoaway()
    {
        auto session = mDp->session;
        int32_t last = nghttp2_session_get_last_proc_stream_id(session);
        nghttp2_submit_goaway(session, NGHTTP2_FLAG_NONE, last, NGHTTP2_NO_ERROR, NULL, 0);
    }

    uint32_t FHttp2Context::http2RecvProcess(FBufferReader& reader)
    {
        int peakNum = 0;
        auto dataPtr = reader.peekAll(peakNum);

        nghttp2_ssize recvLen = nghttp2_session_mem_recv2(mDp->session,(uint8_t*)dataPtr, peakNum);
        reader.expireSize(recvLen);
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

        itor = mHeaders.find("content_length");
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