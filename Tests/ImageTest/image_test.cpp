#include "test_support.h"
#include "Utils/ImageRequester.h"
#include "Service/ImageService.h"
#include "DAO/PhotoQuery.h"
#include "DAO/DataBaseOperation.h"
#include "Server/server.h"
#include "FLogger.h"
#include "FSocket.h"
#include <sqlite3.h>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <vector>
#ifndef IMAGE_TEST_FULL_SERVER
// The isolated transport build omits absl's diagnostic stack unwinder. The full
// Server/FeiLib build below uses the real implementation.
namespace Fei { std::vector<std::string> getStackTrace(uint32) { return {}; } }
#endif
#ifdef IMAGE_TEST_FULL_SERVER
#include "FTCPServer.h"
#endif

// Use the real DAO, isolated database, and real Loop close callbacks without
// constructing the production Server (ports 80/443, config and database).
void Blog::Server::CurThreadCleanCallback(std::function<void()> callback) {
    if (auto* loop = Fei::FEventLoop::getCurrentLoop()) loop->addLoopCloseCallback(std::move(callback));
}
namespace {
using namespace std::chrono_literals;
using namespace Blog::Utils;
using namespace Fei::Async;
using FeiTest::check;
std::string env(const char* name) {
    const auto* value = std::getenv(name);
    check(value && *value, std::string("Missing fixture environment: ") + name);
    return value;
}
void little(std::string& s, std::size_t pos, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) s[pos + i] = char(value >> (8 * i));
}
void metadata() {
    ImageInfo info;
    std::string gif = "GIF89a";
    gif += std::string("\x0d\0\x07\0\0\0\0", 7);
    check(readImageInfo(gif, info) && info.width == 13 && info.height == 7 && info.format == ImageFormat::Gif, "GIF metadata failed");
    check(!readImageInfo(gif, info, 90) && info.width == 0, "pixel limit not enforced");
    check(!readImageInfo("GIF89a", info), "truncated GIF accepted");
    check(!readImageInfo("<html>error</html>", info), "non-image accepted");
    std::string bmp(54, '\0'); bmp[0] = 'B'; bmp[1] = 'M';
    little(bmp, 2, 54); little(bmp, 10, 54); little(bmp, 14, 40);
    little(bmp, 18, 13); little(bmp, 22, 7); bmp[26] = 1; bmp[28] = 24;
    check(readImageInfo(bmp, info) && info.width == 13 && info.format == ImageFormat::Bmp, "BMP metadata failed");
    for (auto kind : {std::string("VP8X"), std::string("VP8L"), std::string("VP8 ")}) {
        auto payload = kind == "VP8L" ? 6u : 10u;
        std::string webp(20 + payload, '\0'); webp.replace(0, 4, "RIFF"); webp.replace(8, 4, "WEBP");
        little(webp, 4, std::uint32_t(webp.size() - 8)); webp.replace(12, 4, kind); little(webp, 16, kind == "VP8L" ? 5 : 10);
        if (kind == "VP8X") { webp[24] = 12; webp[27] = 6; }
        else if (kind == "VP8L") { webp[20] = 0x2f; little(webp, 21, 12 | (6 << 14)); }
        else { webp[23] = char(0x9d); webp[24] = 1; webp[25] = 0x2a; webp[26] = 13; webp[28] = 7; }
        check(readImageInfo(webp, info) && info.width == 13 && info.height == 7 && info.format == ImageFormat::WebP, kind + " metadata failed");
        auto broken = webp; little(broken, 4, 12);
        check(!readImageInfo(broken, info), "RIFF chunk outside declared size accepted");
        check(!readImageInfo(webp.substr(0, 21), info), "truncated WebP metadata accepted");
    }
}
struct Received { Result<ImageResponse> result; std::thread::id thread; };
struct Call { OperationHandle handle; std::future<Received> future; };
struct Fixture {
    FeiTest::LoopRunner runner;
    JobPool workers;
    Fei::Http::FHttpClient client;
    ImageRequester requester;
    std::vector<OperationHandle> calls;
    explicit Fixture() : client(runner.loop(), [] {
        Fei::Http::ClientOptions options; options.caFile = env("FEILIB_TEST_CA_FILE"); return options;
    }()), requester(client, workers) {}
    ~Fixture() {
        for (const auto& call : calls) call.cancel();
        workers.shutdown();
        std::promise<void> drained; auto future = drained.get_future();
        client.shutdown([&] { drained.set_value(); }); FeiTest::await(future);
        runner.drain(); runner.drain();
    }
    Call start(ImageRequest request, bool dimensions = false) {
        auto promise = std::make_shared<std::promise<Received>>(); auto future = promise->get_future();
        OperationHandle handle;
        if (dimensions) {
            handle = requester.requestDimensions(std::move(request), [promise](Result<ImageInfo> result) {
                Result<ImageResponse> mapped;
                if (result.ok()) { ImageResponse image; image.info = *result.value; mapped = Result<ImageResponse>::success(std::move(image)); }
                else mapped = Result<ImageResponse>::failure(std::move(result.error));
                promise->set_value({std::move(mapped), std::this_thread::get_id()});
            });
        } else handle = requester.requestImage(std::move(request), [promise](Result<ImageResponse> result) { promise->set_value({std::move(result), std::this_thread::get_id()}); });
        calls.push_back(handle); return {handle, std::move(future)};
    }
    Received fetch(std::string url, bool dimensions = false, std::function<void(ImageRequest&)> configure = {}) {
        ImageRequest request; request.url = std::move(url); if (configure) configure(request);
        auto call = start(std::move(request), dimensions);
        auto received = FeiTest::await(call.future);
        check(received.thread == runner.threadId(), "image callback ran outside client Loop");
        return received;
    }
};
void requests() {
    const auto http = env("FEILIB_TEST_HTTP_URL"), https = env("FEILIB_TEST_HTTPS_URL");
    Fixture f;
    auto result = f.fetch(http + "/image/png");
    check(result.result.ok(), "full image request failed");
    auto& image = *result.result.value;
    check(image.info.width == 13 && image.info.height == 7 && image.info.format == ImageFormat::Png &&
          image.bytes && image.bytes->starts_with(std::string("\x89PNG\r\n\x1a\n", 8)) && !image.partial, "image result lost bytes or dimensions");
    check(f.fetch(https + "/image/png").result.ok(), "verified HTTPS image failed");
    for (const auto* endpoint : {"/image/png", "/image/large", "/image/late"}) {
        auto dims = f.fetch(http + endpoint, true, [](auto& request) { request.probeBytes = 64; });
        check(dims.result.ok() && dims.result.value->info.width == 13 && dims.result.value->info.height == 7, std::string("Range/fallback failed: ") + endpoint);
    }
    for (const auto* endpoint : {"/image/redirect", "/image/relative", "/image/queryredirect", "/image/queryrelative", "/image/redirect#ignored"}) {
        auto redirect = f.fetch(http + endpoint, true);
        check(redirect.result.ok() && redirect.result.value->info.width == 17, std::string("image redirect failed: ") + endpoint);
    }
    for (const auto* endpoint : {"/image/loop", "/image/invalid", "/image/truncated", "/image/badrange", "/image/encoded", "/status/404"})
        check(!f.fetch(http + endpoint, true).result.ok(), std::string("invalid image response accepted: ") + endpoint);
    check(!f.fetch(https + "/image/down").result.ok(), "HTTPS downgrade followed");
    check(!f.fetch(http + "/image/redirect", false, [](auto& r) { r.maxRedirects = 0; }).result.ok(), "redirect limit ignored");
    check(f.fetch(http + "/image/png", false, [](auto& r) { r.maxPixels = 90; }).result.error.code == ErrorCode::Protocol, "image pixel cap ignored");
    check(f.fetch(http + "/image/large", false, [](auto& r) { r.maxBytes = 1024; }).result.error.code == ErrorCode::LimitExceeded, "image body cap ignored");
    check(f.fetch(http + "/image/png", false, [](auto& r) { r.timeout = 0ms; }).result.error.code == ErrorCode::InvalidArgument, "invalid image timeout accepted");
    check(f.fetch(http + "/image/slow", true, [](auto& r) { r.timeout = 40ms; }).result.error.code == ErrorCode::Timeout, "image deadline ignored");
    ImageRequest slow; slow.url = http + "/image/slow";
    auto call = f.start(slow); const auto before = std::chrono::steady_clock::now();
    f.runner.run([] {});
    check(std::chrono::steady_clock::now() - before < 200ms, "slow image blocked Loop");
    call.handle.cancel(); auto cancelled = FeiTest::await(call.future);
    check(cancelled.result.error.code == ErrorCode::Cancelled && cancelled.thread == f.runner.threadId(), "image cancellation callback incorrect");
    std::vector<Call> concurrent;
    for (int i = 0; i < 20; ++i) { ImageRequest request; request.url = http + "/image/png?w=" + std::to_string(i + 1); concurrent.push_back(f.start(request, true)); }
    for (int i = 0; i < 20; ++i) { auto value = FeiTest::await(concurrent[i].future); check(value.result.ok() && value.result.value->info.width == i + 1, "concurrent image result mixed"); }
}
struct Database {
    std::filesystem::path path;
    sqlite3* connection = nullptr;
    Database() : path(std::filesystem::path(env("BLOG_TEST_TEMP_DIR")) / "isolated-images.db") {
        sqlite3_initialize(); check(sqlite3_open(path.string().c_str(), &connection) == SQLITE_OK, "create isolated DB failed");
        sqlite3_busy_timeout(connection, 5000);
        execute("CREATE TABLE Photos(id INTEGER PRIMARY KEY, album_id INTEGER, url TEXT, caption TEXT DEFAULT '', sort_order INTEGER DEFAULT 0, title TEXT DEFAULT '', description TEXT DEFAULT '', created_at INTEGER DEFAULT 0, view_times INTEGER DEFAULT 0, width INTEGER DEFAULT 0, height INTEGER DEFAULT 0)");
        new Blog::DatabaseOperation; Blog::DatabaseOperation::instance()->LoadDB(path.string());
    }
    ~Database() { sqlite3_close(connection); delete Blog::DatabaseOperation::instance(); }
    void execute(const std::string& sql) { check(sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK, "isolated DB SQL failed"); }
    void insert(std::uint64_t id, const std::string& url) {
        sqlite3_stmt* statement = nullptr;
        check(sqlite3_prepare_v2(connection, "INSERT INTO Photos(id,album_id,url) VALUES(?,1,?)", -1, &statement, nullptr) == SQLITE_OK, "insert prepare failed");
        sqlite3_bind_int64(statement, 1, id); sqlite3_bind_text(statement, 2, url.c_str(), -1, SQLITE_TRANSIENT);
        auto status = sqlite3_step(statement); sqlite3_finalize(statement); check(status == SQLITE_DONE, "insert failed");
    }
    std::pair<int, int> dims(std::uint64_t id) {
        sqlite3_stmt* statement = nullptr;
        check(sqlite3_prepare_v2(connection, "SELECT width,height FROM Photos WHERE id=?", -1, &statement, nullptr) == SQLITE_OK, "select prepare failed");
        sqlite3_bind_int64(statement, 1, id); check(sqlite3_step(statement) == SQLITE_ROW, "photo missing");
        const auto value = std::pair{sqlite3_column_int(statement, 0), sqlite3_column_int(statement, 1)}; sqlite3_finalize(statement); return value;
    }
    void expect(std::uint64_t id, int w, int h) {
        const auto until = std::chrono::steady_clock::now() + 10s;
        while (dims(id) != std::pair{w, h} && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(10ms);
        check(dims(id) == std::pair{w, h}, "photo dimensions did not converge for id " + std::to_string(id));
    }
};
void service() {
    const auto http = env("FEILIB_TEST_HTTP_URL"); Database db;
    // All real DAO statements/connections stay on this Loop and close with it.
    FeiTest::LoopRunner runner;
    db.insert(1, http + "/image/png"); db.insert(2, http + "/image/slow?w=3");
    db.insert(3, http + "/image/slow");
    Blog::ImageService images(runner.loop());
    images.refreshDimensions(1, http + "/image/png"); db.expect(1, 13, 7);
    images.refreshDimensions(2, http + "/image/slow?w=3");
    runner.run([&] {
        auto photo = Blog::DAO::PhotoQuery::QueryPhotoById(2); check(photo.has_value(), "DAO lookup failed");
        photo->url = http + "/image/png?w=17&h=9";
        check(!Blog::DAO::PhotoQuery::UpdatePhoto(*photo), "photo URL edit failed");
    });
    images.refreshDimensions(2, http + "/image/png?w=17&h=9");
    // Delayed notification for an older edit must not cancel the newest URL.
    images.refreshDimensions(2, http + "/image/slow?w=3");
    db.expect(2, 17, 9);
    runner.run([&] {
        check(!Blog::DAO::PhotoQuery::UpdatePhotoDimsIfUrlMatches(2, http + "/image/slow?w=3", 888, 999), "stale CAS SQL failed");
    });
    check(db.dims(2) == std::pair{17, 9}, "stale URL overwrote current dimensions");
    runner.run([&] {
        auto photo = Blog::DAO::PhotoQuery::QueryPhotoById(2); photo->caption = "metadata edit";
        check(!Blog::DAO::PhotoQuery::UpdatePhoto(*photo), "caption edit failed");
    });
    check(db.dims(2) == std::pair{17, 9}, "caption edit reset dimensions");
    runner.run([&] {
        auto photo = Blog::DAO::PhotoQuery::QueryPhotoById(2); photo->url = http + "/image/png?w=19";
        check(!Blog::DAO::PhotoQuery::UpdatePhoto(*photo), "second URL edit failed");
    });
    check(db.dims(2) == std::pair{0, 0}, "new URL retained old dimensions");
    images.refreshDimensions(2, http + "/image/png?w=19"); db.expect(2, 19, 7);
    // Two editors read X, then commit Y and X. The second edit must request X
    // again even though its pre-update snapshot also contained X.
    const auto x = http + "/image/png?w=23", y = http + "/image/png?w=25";
    db.insert(4, x); images.refreshDimensions(4, x); db.expect(4, 23, 7);
    runner.run([&] {
        auto editorB = *Blog::DAO::PhotoQuery::QueryPhotoById(4);
        auto editorA = editorB; editorA.url = y;
        check(!Blog::DAO::PhotoQuery::UpdatePhoto(editorA), "editor A failed");
        check(!Blog::DAO::PhotoQuery::UpdatePhoto(editorB), "editor B failed");
    });
    check(db.dims(4) == std::pair{0, 0}, "interleaved edit retained obsolete dimensions");
    images.refreshDimensions(4, x); images.refreshDimensions(4, y);
    db.expect(4, 23, 7);
    for (std::uint64_t id = 10; id < 30; ++id) { auto url = http + "/image/png?w=" + std::to_string(id); db.insert(id, url); images.refreshDimensions(id, url); }
    for (std::uint64_t id = 10; id < 30; ++id) db.expect(id, static_cast<int>(id), 7);
    images.refreshDimensions(3, http + "/image/slow"); runner.drain();
    images.shutdown(); images.shutdown();
    check(db.dims(3) == std::pair{0, 0}, "shutdown wrote cancelled dimensions");
    images.refreshDimensions(3, http + "/image/slow"); runner.drain();
}
#ifdef IMAGE_TEST_FULL_SERVER
void sharedLoop() {
    // No listen ports/config/production database. Exercise the actual TCP owner.
    for (int i = 0; i < 3; ++i) {
        Fei::FTcpServer server(1); auto& loop = server.jobLoop();
        check(&loop == &server.jobLoop(), "server did not reuse JobLoop");
        std::promise<bool> ran; auto future = ran.get_future();
        std::atomic_bool cleaned{false};
        loop.AddTask([&] { loop.addLoopCloseCallback([&] { cleaned.store(true); }); ran.set_value(loop.isInLoopThread()); });
        server.run(); check(FeiTest::await(future), "TCP-owned JobLoop ran on wrong thread");
        server.stop(); check(loop.HasStoped() && cleaned.load(), "shared Loop did not drain/close");
    }
}
#endif
}
int main(int argc, char** argv) {
    Fei::FeiInit(); Fei::LoggerConfig config; config.showLvl = Fei::lvl::warn; config.saveLvl = Fei::lvl::warn; config.saveFileName = "image-tests.log";
    new Fei::Logger(config); int status = 0;
    try {
        if (argc > 1 && std::string(argv[1]) == "metadata") metadata();
        else { requests(); service();
#ifdef IMAGE_TEST_FULL_SERVER
            sharedLoop();
#endif
        }
        std::cout << "PASS image tests\n";
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; status = 1; }
    delete Fei::Logger::instance(); Fei::FeiUnInit(); return status;
}
