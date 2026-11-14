#include "Http/FHttpResponseParser.h"
#include <stdexcept>
#include <string>

namespace {
using Fei::Http::FHttpResponseParser;
using Fei::Async::ErrorCode;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void expectError(const std::string& wire, const char* message,
                 std::size_t bodyLimit = 4096, std::size_t headerLimit = 4096) {
    FHttpResponseParser parser("GET", bodyLimit, headerLimit);
    parser.feed(wire);
    if (!parser.hasError()) parser.finishEof();
    require(parser.hasError(), message);
    require(!parser.response().keepAlive, "erroneous response must not be reused");
}
void splitAtEveryBoundary(const std::string& wire, const std::string& body) {
    for (std::size_t cut = 0; cut <= wire.size(); ++cut) {
        FHttpResponseParser parser;
        require(parser.feed(std::string_view(wire).substr(0, cut)), "first fragment rejected");
        require(parser.feed(std::string_view(wire).substr(cut)), "second fragment rejected");
        require(parser.isComplete(), "split response incomplete");
        require(parser.response().body == body, "split response body differs");
    }
}
void everyTruncatedPrefixFails(const std::string& wire) {
    for (std::size_t count = 0; count < wire.size(); ++count) {
        FHttpResponseParser parser;
        parser.feed(std::string_view(wire).substr(0, count));
        require(!parser.isComplete(), "truncated response completed before its framing terminator");
        require(!parser.finishEof() && parser.hasError(), "truncated response prefix accepted at EOF");
    }
}
} // namespace

void runResponseParserTests() {
    const std::string binary("a\0b\xffz", 5);
    const std::string fixed = "HTTP/1.1 200 OK\r\ncOnTeNt-LeNgTh: 5\r\nX-Test: yes\r\n\r\n" + binary;
    splitAtEveryBoundary(fixed, binary);
    everyTruncatedPrefixFails(fixed);
    {
        FHttpResponseParser parser;
        for (char byte : fixed) require(parser.feed(std::string_view(&byte, 1)), "bytewise feed failed");
        require(parser.isComplete(), "bytewise response incomplete");
        require(parser.response().status == 200 && parser.response().body == binary, "binary content differs");
        require(parser.response().header("CONTENT-LENGTH") == "5", "case-insensitive header lookup failed");
        require(parser.response().keepAlive, "HTTP/1.1 fixed-length keepalive lost");
    }
    const std::string chunked = "HTTP/1.1 200 OK\r\nTransfer-Encoding: Chunked\r\n\r\n2;foo=bar\r\nhe\r\n3\r\nllo\r\n0\r\nX-Checksum: done\r\n\r\n";
    splitAtEveryBoundary(chunked, "hello");
    everyTruncatedPrefixFails(chunked);
    {
        FHttpResponseParser parser;
        for (char byte : chunked) require(parser.feed(std::string_view(&byte, 1)), "bytewise chunked feed failed");
        require(parser.isComplete() && parser.response().body == "hello", "bytewise chunked response incomplete");
    }
    {
        FHttpResponseParser parser;
        require(parser.feed(chunked), "chunked response rejected");
        require(parser.response().header("X-Checksum") == "done", "trailer missing");
        require(parser.response().keepAlive, "chunked keepalive lost");
    }
    {
        FHttpResponseParser parser;
        require(parser.feed("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 103 Early Hints\r\nLink: </a>\r\n\r\n" + fixed), "interim responses failed");
        require(parser.isComplete() && parser.response().status == 200 && parser.response().body == binary, "interim response delivered as final");
        require(parser.response().header("Link").empty(), "interim headers leaked into final response");
    }
    {
        FHttpResponseParser parser("HEAD", 0);
        require(parser.feed("HTTP/1.1 200 OK\r\nContent-Length: 999999\r\n\r\n"), "HEAD metadata length rejected");
        require(parser.isComplete() && parser.response().body.empty(), "HEAD waited for body");
    }
    {
        FHttpResponseParser parser;
        require(parser.feed("HTTP/1.1 204 No Content\r\n\r\n"), "204 rejected");
        require(parser.isComplete() && parser.response().body.empty(), "204 waited for body");
    }
    {
        FHttpResponseParser parser("GET", 0);
        require(parser.feed("HTTP/1.1 304 Not Modified\r\nContent-Length: 999999\r\n\r\n"), "304 metadata rejected");
        require(parser.isComplete(), "304 waited for body");
    }
    {
        FHttpResponseParser parser("CONNECT");
        parser.feed("HTTP/1.1 200 Connection Established\r\n\r\n");
        require(parser.hasError(), "unsupported tunnel accepted as reusable HTTP response");
    }
    {
        FHttpResponseParser parser;
        require(parser.feed("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: keep-alive, CLOSE\r\n\r\n"), "connection token list rejected");
        require(parser.isComplete() && !parser.response().keepAlive, "close must win over keep-alive");
        require(parser.finishEof(), "EOF after complete response incorrectly failed");
    }
    {
        const std::string wire = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";
        FHttpResponseParser exact("GET", 0, wire.size());
        require(exact.feed(wire) && exact.isComplete(), "exact header limit rejected");
        FHttpResponseParser shortLimit("GET", 0, wire.size() - 1);
        require(!shortLimit.feed(wire) && shortLimit.error().code == ErrorCode::LimitExceeded, "header limit off by one");
        FHttpResponseParser noHeaders("GET", 0, 0);
        require(noHeaders.hasError() && !noHeaders.feed(wire), "zero header limit should fail before parsing");
    }
    {
        FHttpResponseParser parser;
        require(parser.feed("HTTP/1.1 404 Missing\r\nContent-Length: 3\r\n\r\nbad"), "HTTP error body rejected");
        require(parser.isComplete() && parser.response().status == 404 && parser.response().body == "bad", "HTTP error body lost");
    }
    {
        FHttpResponseParser parser;
        require(parser.feed("HTTP/1.0 200 OK\r\n\r\nhello"), "EOF body rejected");
        require(!parser.isComplete(), "EOF body completed before EOF");
        require(parser.finishEof() && parser.isComplete(), "EOF did not complete body");
        require(parser.response().body == "hello" && !parser.response().keepAlive, "EOF body framing incorrect");
    }
    for (const std::string connection : {"", "Connection: close\r\n", "Connection: Keep-Alive\r\n"}) {
        FHttpResponseParser parser;
        require(parser.feed("HTTP/1.0 200 OK\r\nContent-Length: 0\r\n" + connection + "\r\n"), "HTTP/1.0 response rejected");
        require(parser.response().keepAlive == (connection.find("Keep-Alive") != std::string::npos), "HTTP/1.0 persistence incorrect");
    }
    {
        FHttpResponseParser parser;
        require(parser.feed("HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\naEXTRA"), "response with remaining bytes rejected");
        require(parser.isComplete() && parser.response().body == "a" && parser.remainingBytes() == 5, "remaining bytes not preserved");
        require(!parser.response().keepAlive, "extra bytes allow unsafe reuse");
        require(parser.feed("more") && parser.remainingBytes() == 9, "late remaining bytes not counted");
    }
    {
        const std::string body(100000, '\0');
        const std::string extra(200000, 'x');
        FHttpResponseParser parser;
        require(parser.feed("HTTP/1.1 200 OK\r\nContent-Length: 100000\r\nContent-Encoding: gzip\r\n\r\n" + body + extra), "large feed rejected");
        require(parser.response().body == body, "content decoding unexpectedly changed raw binary bytes");
        require(parser.remainingBytes() == extra.size(), "large remaining feed not accounted correctly");
    }
    {
        FHttpResponseParser parser;
        require(parser.feed("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1 ; foo = \"a;\\\"b\" ;flag\r\nx\r\n0\r\n\r\n"), "quoted chunk extension rejected");
        require(parser.isComplete() && parser.response().body == "x", "quoted chunk extension corrupted body");
    }
    {
        FHttpResponseParser parser;
        bool threw = false;
        try { parser.takeResponse(); } catch (const std::logic_error&) { threw = true; }
        require(threw, "incomplete takeResponse did not reject misuse");
        require(parser.feed("HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n"), "zero body rejected");
        require(parser.takeResponse().status == 200, "takeResponse lost response");
    }
    for (const auto* method : {"head", "connect", "Head"}) {
        FHttpResponseParser parser(method);
        require(parser.feed("HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n"), "custom method headers rejected");
        require(!parser.isComplete(), "case-sensitive custom method treated as HEAD/CONNECT");
        require(parser.feed("error") && parser.isComplete() && parser.response().body == "error", "custom method body lost");
    }
    expectError("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nab", "truncated fixed body accepted");
    expectError("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2\r\nab\r\n", "missing terminal chunk accepted");
    expectError("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nX-T: a\r\n", "truncated trailer accepted");
    expectError("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nTransfer-Encoding: chunked\r\n\r\n", "CL+TE ambiguity accepted");
    expectError("HTTP/1.1 200 OK\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\na", "duplicate CL accepted");
    expectError("HTTP/1.1 200 OK\r\nContent-Length: 1, 1\r\n\r\na", "CL list accepted");
    expectError("HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n", "negative CL accepted");
    expectError("HTTP/1.1 200 OK\r\nContent-Length: +1\r\n\r\na", "signed CL accepted");
    expectError("HTTP/1.1 200 OK\r\nContent-Length: 1\r\ncontent-length: 2\r\n\r\nab", "case-variant duplicate CL accepted");
    expectError("HTTP/1.1 200 OK\r\nContent-Length: 999999999999999999999999\r\n\r\n", "overflow CL accepted");
    expectError("HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n", "unsupported transfer coding accepted");
    expectError("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nZ\r\n", "invalid hex chunk accepted");
    expectError("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF\r\n", "chunk size integer overflow accepted");
    expectError("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1;\r\na\r\n0\r\n\r\n", "empty chunk extension accepted");
    expectError("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1;foo=\"unterminated\r\na\r\n0\r\n\r\n", "unterminated quoted chunk extension accepted");
    expectError("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n 1\r\na\r\n0\r\n\r\n", "leading chunk size whitespace accepted");
    expectError("HTTP/1.0 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n", "HTTP/1.0 transfer coding accepted");
    expectError("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\naXX", "invalid chunk delimiter accepted");
    expectError("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nContent-Length: 1\r\n\r\n", "framing trailer accepted");
    expectError("HTTP/1.1 101 Switching Protocols\r\n\r\n", "unsupported upgrade accepted");
    expectError("HTTP/1.1 100 Continue\r\n\r\n", "EOF after interim accepted");
    expectError("HTTP/1.1 204 No Content\r\nContent-Length: 1\r\n\r\na", "204 framing accepted");
    expectError("GET / HTTP/1.1\r\n\r\n", "request parsed as response");
    expectError("HTTP/1.1 200 OK\nContent-Length: 0\n\n", "bare LF accepted");
    expectError("HTTP/1.1 200 OK\rX", "bare CR accepted");
    expectError("HTTP/1.1 200 OK\r\nBad Name: value\r\n\r\n", "invalid header name accepted");
    expectError(std::string("HTTP/1.1 200 OK\r\nX-Test: a") + '\0' + "b\r\n\r\n", "NUL header value accepted");
    expectError("HTTP/1.1 200 OK\r\n folded: value\r\n\r\n", "obsolete folded header accepted");
    expectError("HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\nabcdef", "fixed body limit ignored", 5);
    expectError("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n6\r\nabcdef\r\n0\r\n\r\n", "chunk body limit ignored", 5);
    expectError("HTTP/1.1 200 OK\r\n\r\nabcdef", "EOF body limit ignored", 5);
    expectError("HTTP/1.1 200 OK\r\nX-Test: aaaaaaaaaaaaaaaaa\r\n\r\n", "header limit ignored", 4096, 25);
    expectError("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n", "interim aggregate header limit ignored", 4096, 40);
    expectError("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nX-T: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n\r\n", "trailer limit ignored", 4096, 65);
    {
        FHttpResponseParser parser("GET", 0);
        parser.feed("HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\na");
        require(parser.error().code == ErrorCode::LimitExceeded, "limit error code lost");
        require(!parser.feed("more"), "feed after error succeeded");
    }
}
