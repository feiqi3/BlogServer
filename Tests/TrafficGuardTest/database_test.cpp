#include "DAO/DataBaseOperation.h"
#include "Server/server.h"
#include "FLogger.h"
#include <sqlite3.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
void Blog::Server::CurThreadCleanCallback(std::function<void()>) {}
void require(bool result, const char* message) { if (!result) throw std::runtime_error(message); }
int main() {
    auto root = std::filesystem::temp_directory_path() /
        ("blog-visit-db-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto file = (root / "test.db").string();
    require(sqlite3_initialize() == SQLITE_OK, "initialize bundled SQLite (OMIT_AUTOINIT)");
    sqlite3* db = nullptr;
    require(sqlite3_open(file.c_str(), &db) == SQLITE_OK, "open fixture");
    require(sqlite3_exec(db, "CREATE TABLE Posts(id INTEGER PRIMARY KEY,view_times INTEGER);"
        "INSERT INTO Posts VALUES(1,10)", nullptr, nullptr, nullptr) == SQLITE_OK, "create fixture");
    sqlite3_close(db);
    Fei::LoggerConfig log; log.saveFileName = (root / "test.log").string();
    Fei::Logger logger(log);
    Blog::DatabaseOperation database;
    database.LoadDB(file);
    database.registerArticleVisit("first", 1);
    require(database.confirmArticleVisit("first"), "first confirmation");
    require(database.confirmArticleVisit("first"), "idempotent confirmation");
    database.registerArticleVisit("second", 1);
    auto confirm = [&] {
        database.usePrivateCacheForCurrentThread();
        require(database.confirmArticleVisit("second"), "concurrent database confirmation");
    };
    std::thread a(confirm), b(confirm);
    a.join(); b.join();
    require(sqlite3_open(file.c_str(), &db) == SQLITE_OK, "reopen fixture");
    sqlite3_stmt* query = nullptr;
    sqlite3_prepare_v2(db, "SELECT view_times FROM Posts WHERE id=1", -1, &query, nullptr);
    require(sqlite3_step(query) == SQLITE_ROW && sqlite3_column_int(query, 0) == 12, "exactly once article count");
    sqlite3_finalize(query); sqlite3_close(db);
    std::cout << "Real SQLite confirmed article visits: retry and concurrent exactly-once passed\n";
}
