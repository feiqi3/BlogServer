#include "Service/TrafficGuard.h"
#include "FBuffer.h"
#include "FLogger.h"
#include "Http/FHttp2Helper.h"
#include "Http/FHttpRequest.h"
#include "Http/FHttpResponse.h"
#include <nlohmann/json.hpp>
#include "inja/inja.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace Fei::Http;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
FHttpRequest request(const std::string& path, const std::string& ip = "203.0.113.1",
                     const std::string& token = "", const std::string& extra = "") {
    auto wire = std::string(token.empty() ? "GET " : "POST ") + path +
        " HTTP/1.1\r\nHost: test.example\r\nContent-Type: text/plain\r\nX-Blog-Visit: 1\r\n" +
        extra + "Content-Length: " + std::to_string(token.size()) + "\r\n\r\n" + token;
    Fei::FBuffer buffer(wire.size() + 1);
    buffer.Append(wire.data(), wire.size());
    Fei::FBufferReader reader(buffer); FHttpParser parser;
    require(parser.parse(reader), "parse request");
    FHttpRequest result(parser);
    result.setAddrIn(Fei::FSocketAddr(ip.c_str(), 1234));
    return result;
}
std::string page(Blog::TrafficGuard& guard, const std::string& path = "/post/51",
                 const std::string& ip = "203.0.113.1", const std::string& headers = "") {
    auto req = request(path, ip, "", headers);
    FHttpResponse response;
    require(!guard.filter(req, response), "allowed page");
    auto token = Blog::TrafficGuard::visitId(req);
    require(token.size() == 32, "issued token");
    response.setBody("<meta name=\"blog-visit\" content=\"" + token + "\">");
    guard.prepare(req, response);
    return token;
}
int main(int argc, char** argv) {
    require(argc == 2, "supply configuration example");
    const auto root = std::filesystem::temp_directory_path() /
        ("blog-traffic-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / "config");
    {
        Fei::LoggerConfig logging;
        logging.saveFileName = (root / "h2.log").string();
        logging.showLvl = Fei::lvl::off;
        logging.saveLvl = Fei::lvl::off;
        Fei::Logger logger(logging);
        FHttp2Context session(Fei::FSocketAddr("203.0.113.1", 1234));
        std::vector<StatusCode> rejectedStatuses;
        session.setRequestRejectedCallback([&](StatusCode status, FHttpResponse& response) {
            require(response.getStatusCode() == status, "rejection response starts with FeiLib status");
            rejectedStatuses.push_back(status);
            response.setContentType("text/plain").setBody("Request rejected");
        });
        FHttp2Parser* rejected = nullptr;
        session.setHeadersFilter([&](auto& parser, auto stream) {
            if (parser.mPath == "/post/51") {
                rejected = &parser;
                FHttpResponse early;
                early.setStatusCode(StatusCode::_429).setBody("blocked");
                session.http2SubmitResponseStream(stream, early, true);
                return true;
            }
            return false;
        });
        auto frame = [](int type, int flags, int stream, const std::string& data) {
            std::string result;
            for (auto byte : {int(data.size() >> 16), int(data.size() >> 8), int(data.size()),
                              type, flags, stream >> 24, stream >> 16, stream >> 8, stream})
                result.push_back(static_cast<char>(byte & 255));
            return result + data;
        };
        auto headers = [](const std::string& path, const std::string& size) {
            std::string result;
            result.push_back(char(0x82)); // Static HPACK GET.
            result.push_back(char(0x87)); // Static HPACK https.
            result.push_back(char(1)); result.push_back(char(12)); result += "test.example";
            result.push_back(char(4)); result.push_back(char(path.size())); result += path;
            if (!size.empty()) {
                result.push_back(char(15)); result.push_back(char(13)); // Static name 28, content-length.
                result.push_back(char(size.size())); result += size;
            }
            return result;
        };
        auto largeHeaders = headers("/oversized", "");
        for (int i = 0; i < 1600; ++i) {
            largeHeaders += char(15);
            largeHeaders += char(43); // Static name 58, user-agent.
            largeHeaders += char(1);
            largeHeaders += 'a';
        }
        auto wire = std::string("PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n") + frame(4, 0, 0, "") +
            frame(1, 4, 1, headers("/post/51", "1048576")) + frame(0, 0, 1, "abcd") +
            frame(1, 4, 3, headers("/rss.xml", "4")) + frame(0, 1, 3, "abcd") +
            frame(1, 4, 5, headers("/large-body", std::to_string(HttpMaxRequestBodyBytes + 1))) +
            frame(1, 4, 7, headers("/no-content-length", "")) + frame(0, 0, 7, std::string("a\0b", 3)) +
            frame(1, 5, 9, largeHeaders);
        Fei::FBuffer buffer(wire.size() + 1);
        buffer.Append(wire.data(), wire.size());
        Fei::FBufferReader reader(buffer);
        session.http2RecvProcess(reader);
        require(rejected && rejected->mData.empty(), "HTTP/2 rejected DATA is not buffered");
        require(rejectedStatuses == std::vector{StatusCode::_413, StatusCode::_431},
                "HTTP/2 parser rejections invoke callback once and early ban uses its own hook");
        unsigned dispatched = 0;
        const auto dispatch = [&](const auto& req, auto stream) {
            if (stream == 3 && req.getPath() == "/rss.xml" && req.getRequestBody() == "abcd") dispatched |= 1;
            else if (stream == 7 && req.getPath() == "/no-content-length" &&
                     req.getRequestBody() == std::string_view("a\0bc", 4)) dispatched |= 2;
            else throw std::runtime_error("unexpected HTTP/2 request dispatched on stream " + std::to_string(stream));
            return true;
        };
        session.traversalFinishedStreams(dispatch);
        require(dispatched == 1, "HTTP/2 rejects oversized streams and waits for END_STREAM");
        const auto end = frame(0, 1, 7, "c");
        buffer.Append(end.data(), end.size());
        session.http2RecvProcess(reader);
        session.traversalFinishedStreams(dispatch);
        require(dispatched == 3, "HTTP/2 retains bodies without Content-Length");
    }
    {
        const auto check = [](const std::string& wire, StatusCode expected) {
            Fei::FBuffer buffer(wire.size() + 1);
            buffer.Append(wire.data(), wire.size());
            Fei::FBufferReader reader(buffer);
            FHttpParser parser;
            require(!parser.parse(reader) && parser.getState() == FHttpParser::EState::Error &&
                    parser.errorStatus() == expected, "HTTP/1 request limit");
            require(parser.getContext().getRequestBody().empty(), "HTTP/1 rejected body stays empty");
        };
        check("GET / HTTP/1.1\r\nX-Test: " + std::string(HttpMaxRequestHeaderBytes, 'a'), StatusCode::_431);
        check("POST / HTTP/1.1\r\ncontent-length: " + std::to_string(HttpMaxRequestBodyBytes + 1) + "\r\n\r\n", StatusCode::_413);
        check("POST / HTTP/1.1\r\nContent-Length: 99999999999999999999999999\r\n\r\n", StatusCode::_413);
        check("POST / HTTP/1.1\r\nContent-Length: 1\r\ncontent-length: 2\r\n\r\n", StatusCode::_400);
        require(request("/binary", "203.0.113.1", std::string("a\0b", 3)).getRequestBody() == std::string_view("a\0b", 3),
                "HTTP/1 binary body survives bulk parsing");
    }
    std::ifstream input(argv[1]); nlohmann::json config; input >> config;
    auto workspace = std::filesystem::path(argv[1]).parent_path().parent_path().parent_path().parent_path();
    auto photoTemplate = workspace / "blogweb/dist/photos.html";
    if (std::filesystem::exists(photoTemplate)) {
        std::ifstream file(photoTemplate);
        std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        auto rendered = inja::render(text, nlohmann::json{{"visitId", std::string(32, 'a')}});
        require(rendered.find("content=\"" + std::string(32, 'a') + "\"") != std::string::npos, "built template accepts visit metadata");
        require(rendered.find("<script async src=\"/assets/visit-report.js\">") != std::string::npos,
                "standalone browser script loads without waiting for page modules");
        auto legacy = inja::render(text, nlohmann::json::object());
        require(legacy.find("name=\"blog-visit\"") == std::string::npos, "legacy template context remains supported");
        require(legacy.find("visit-report.js") == std::string::npos, "no script request without an issued visit");
    }
    config["mode"] = "enforce";
    config["observability"]["requestSampleRate"] = 0;
    const auto path = root / "config/traffic-guard.json";
    auto save = [&] { std::ofstream out(path); out << config.dump(); };
    save();
    {
        const std::string body = "unread-body";
        const auto wire = "GET /post/51 HTTP/1.1\r\nContent-Length: 1000000000\r\n\r\n" + body;
        Fei::FBuffer buffer(wire.size() + 1);
        buffer.Append(wire.data(), wire.size());
        Fei::FBufferReader reader(buffer);
        FHttpParser parser;
        parser.setHeadersCallback([](const auto&) { return false; });
        require(!parser.parse(reader) && parser.getState() == FHttpParser::EState::Rejected, "header hook rejects before body");
        require(reader.getBufferReadableSize() == static_cast<int>(body.size()), "rejected body remains unread");
        require(parser.getContext().getRequestBody().empty(), "no rejected body allocation");
    }
    std::atomic<std::int64_t> now = 100;
    {
        Blog::TrafficGuard guard(root.string(), [&] { return now.load(); });
        now = 10000; // The original configuration has been active for much longer than its retirement window.
        auto token = page(guard);
        FHttpResponse response;
        guard.filter(request("/_traffic/proof", "203.0.113.2", token), response);
        require(response.getStatusCode() == StatusCode::_403, "IP binding");
        guard.filter(request("/_traffic/proof", "203.0.113.1", token, "Origin: https://evil.example\r\n"), response);
        require(response.getStatusCode() == StatusCode::_403, "origin binding");
        guard.filter(request("/_traffic/proof", "203.0.113.1", token), response);
        require(response.getStatusCode() == StatusCode::_204, "proof accepted");
        guard.filter(request("/_traffic/proof", "203.0.113.1", token), response);
        require(response.getStatusCode() == StatusCode::_204, "retry idempotence");
        for (int i = 0; i < 30; ++i) page(guard);
        now = 10029;
        require(!guard.filter(request("/api/archive"), response), "still loading");
        now = 10030;
        guard.reload();
        const auto early = [&](const char* path) {
            const auto req = request(path);
            return guard.filterHeaders({req.getMethod(), req.getPath(), req.headers(), req.getAddrIn()}, response);
        };
        require(early("/post/51") && response.getStatusCode() == StatusCode::_429, "early ban projection");
        require(!early("/rss.xml"), "early hook preserves RSS");
        require(!early("/assets/main.js"), "early hook preserves static resources");
        require(!early("/_traffic/proof"), "early hook preserves proof endpoint");
        const auto banFile = root / "cache/traffic-guard-bans.json";
        const auto oldStamp = std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);
        std::filesystem::last_write_time(banFile, oldStamp);
        const auto stamp = std::filesystem::last_write_time(banFile);
        now = 10035;
        guard.reload();
        require(std::filesystem::last_write_time(banFile) == stamp, "unchanged ban state does not rewrite file");
        require(guard.filter(request("/api/archive"), response) && response.getStatusCode() == StatusCode::_429, "API blocked");
        std::string retry;
        require(response.getHeader("Retry-After", retry) && retry == "3595", "retry header");
        require(!guard.filter(request("/rss.xml"), response), "RSS exception");
        require(!guard.filter(request("/assets/main.js"), response), "static exception");
        require(guard.filter(request("/", "203.0.113.1", "", "X-Forwarded-For: 127.0.0.1\r\n"), response), "spoofed XFF rejected");
        auto freshToken = page(guard, "/post/51", "203.0.113.2");
        config["mode"] = "observe"; save(); guard.reload();
        require(!guard.filter(request("/api/archive"), response), "observe releases blocking");
        guard.filter(request("/_traffic/proof", "203.0.113.2", freshToken), response);
        require(response.getStatusCode() == StatusCode::_204, "fresh proof survives retirement of an old config");
        { std::ofstream out(path); out << "{broken"; }
        guard.reload();
        require(!guard.filter(request("/api/archive"), response), "invalid reload retains observe");
        config["mode"] = "enforce"; save(); guard.reload();
        require(guard.filter(request("/api/archive"), response), "real ban survives config change");
    }
    config["clientIp"]["source"] = "forwarded";
    config["clientIp"]["trustedProxyCidrs"] = {"192.0.2.0/24"};
    config["articleViews"]["countOn"] = "confirmedReport";
    save();
    {
        Blog::TrafficGuard guard(root.string(), [&] { return now.load(); });
        FHttpResponse response;
        for (const auto& headers : {std::string{}, std::string("X-Forwarded-For: invalid\r\n"),
                std::string("X-Forwarded-For: 192.0.2.2\r\n"),
                std::string("X-Forwarded-For: 203.0.113.2,\r\n"),
                std::string("X-Forwarded-For: ") + std::string(2049, 'x') + "\r\n"}) {
            auto req = request("/", "192.0.2.1", "", headers);
            require(!guard.filter(req, response) && Blog::TrafficGuard::visitId(req).empty(), "unknown proxy client is untracked");
            require(!guard.filterHeaders({req.getMethod(), req.getPath(), req.headers(), req.getAddrIn()}, response),
                    "unknown proxy client bypasses early ban");
        }
        const std::string chain = "X-Forwarded-For: 203.0.113.1, 203.0.113.2, 192.0.2.2\r\n";
        auto token = page(guard, "/", "192.0.2.1", chain);
        guard.filter(request("/_traffic/proof", "192.0.2.1", token, chain), response);
        require(response.getStatusCode() == StatusCode::_204 && !response.isDeferred(),
                "valid proxy client and non-article confirmation stay in memory");
        auto article = page(guard, "/post/51", "192.0.2.1", chain);
        guard.filter(request("/_traffic/proof", "192.0.2.1", article, chain), response);
        require(response.getStatusCode() == StatusCode::_503, "only articles require the database worker");
        guard.filter(request("/_traffic/status", "127.0.0.1"), response);
        require(nlohmann::json::parse(response.getBody()).at("unknownClientAddress") == 5, "proxy failures counted once");
    }
    {
        Blog::TrafficGuard restored(root.string(), [&] { return now.load(); });
        FHttpResponse response;
        require(restored.filter(request("/api/archive"), response), "ban persisted across restart");
        restored.filter(request("/_traffic/unban", "127.0.0.1", "203.0.113.1"), response);
        require(response.getStatusCode() == StatusCode::_200, "local unban");
        require(!restored.filter(request("/api/archive"), response), "unban applies");
    }
    std::filesystem::remove_all(root);
    std::cout << "TrafficGuard protocol, modes, hot reload, persistence and IP checks passed\n";
}
