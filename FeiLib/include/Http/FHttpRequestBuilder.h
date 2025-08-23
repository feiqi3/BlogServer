#ifndef FHTTPBUILDER_H_
#define FHTTPBUILDER_H_

#include "FHttpDef.h"
#include "FHttpDef.h"
#include <sstream>
namespace Fei::Http{
    class FHttpRequestBuilder{
    public:
        inline FHttpRequestBuilder& setMethod(Method method) {
            method_ = method;
            return *this;
        }

        inline FHttpRequestBuilder& setUrl(const std::string& url) {
            url_ = url;
            return *this;
        }

        inline FHttpRequestBuilder& addHeader(const std::string& key, const std::string& value) {
            headers_[key] = value;
            return *this;
        }

        inline FHttpRequestBuilder& setBody(const std::string& body) {
            body_ = body;
            return *this;
        }

        inline FHttpRequestBuilder& findHeader(const std::string& name, std::string& out) {
            auto itor = headers_.find(name);
            if (itor != headers_.end()) {
                out = itor->second;
            }
            return *this;
        }

        inline FHttpRequestBuilder& removeHeader(const std::string& name) {
            headers_.erase(name);
            return *this;
        }

        inline Method getMethod()const { return method_; }
        inline const std::string& getUrl() const{ return url_; }


        // Build and return the HTTP request as a string
        inline std::string build() {
            std::ostringstream oss;
            // Start-line: "METHOD URL HTTP/1.1"
            oss << methodToStr(method_) << " " << url_ << " HTTP/1.1\r\n";
            // Headers
            for (const auto& header : headers_) {
                oss << header.first << ": " << header.second << "\r\n";
            }
            // Add Content-Length header for body
            oss << "Content-Length: " << body_.size() << "\r\n";
            // End of headers
            oss << "\r\n";
            // Body
            oss << body_;
            return oss.str();
        }

        inline void traversalHeaders(
            std::function<bool(const std::pair<std::string, std::string>&)> func
        )const {
            for (auto itor = headers_.begin(); itor != headers_.end(); ++itor) {
                bool shouldCon = func(*itor);
            }
        }

    private:
        Method method_;
        std::string url_;
        std::map<std::string, std::string> headers_;
        std::string body_;
    };
}

#endif