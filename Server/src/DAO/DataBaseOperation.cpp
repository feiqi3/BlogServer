#include <sqlite3.h>
#include <vector>

#include "DataBaseOperation.h"
#include "FLogger.h"
#define SQL_SINGLE_CONNECTION 1
#define SQL_MAKE_NEW 1

#define MODULE_NAME "DatabaseOperation"
#define SQL_CHECK(x,s) if(x != SQLITE_OK){s}


using namespace Fei;
using namespace Blog;

void Blog::DatabaseOperation::LoadDB(const std::string& databaseName)
{
	//Try to link sqlite and store info.
	sqlite3* dbConn = 0;
	int flag = 0;
#ifdef SQL_MAKE_NEW
	flag |= SQLITE_OPEN_CREATE;
#endif

#ifdef SQL_SINGLE_CONNECTION 
	flag |= SQLITE_OPEN_SHAREDCACHE;
#endif // SQL_SINGLE_CONNECTION 

	SQL_CHECK(sqlite3_open_v2(databaseName.c_str(), &dbConn, flag, nullptr), {
		Logger::instance()->log(MODULE_NAME,lvl::critical,"Try open database \"{}\" failed. reason \"{}\"",databaseName, sqlite3_errmsg(dbConn));
		});

	SQL_CHECK(sqlite3_close_v2(dbConn), {
		Logger::instance()->log(MODULE_NAME,lvl::err,"Try close database failed. reason \"{}\"", sqlite3_errmsg(dbConn));
	});
}

void Blog::DatabaseOperation::Exec(const std::string& sql)
{

}

namespace {
	thread_local sqlite3* _db;
}

class DatabaseOperationPrivate {
public:
	bool exec(const std::string& sql) {
		auto conn = getDbConn();
		assert(conn != 0);
		char* errMessage = 0;
		SQL_CHECK(sqlite3_exec(conn, sql.c_str(), 0, 0, &errMessage), {
			Logger::instance()->log(MODULE_NAME,lvl::err,"SQL \"{}\" execute error,reason: \"{}\"",sql,errMessage);
			sqlite3_free(errMessage);
		});
	};

	bool localDbConnValid()const {
		return _db != nullptr;
	}

	void makeConThreadLocal() {
		if (_db != nullptr)return;
		int flag = 0;
#ifdef SQL_SINGLE_CONNECTION 
		flag |= SQLITE_OPEN_SHAREDCACHE;
#endif // SQL_SINGLE_CONNECTION 
		std::lock_guard<std::mutex> guard(mLock);
		SQL_CHECK(sqlite3_open_v2(dbPath.c_str(), &_db, flag, nullptr), {
		Logger::instance()->log(MODULE_NAME,lvl::critical,"Try open database \"{}\" failed. reason \"{}\"",dbPath, sqlite3_errmsg(_db));
		});
		dbConnPerThread.push_back(&_db);
	}

	sqlite3* getDbConn() {
		if (localDbConnValid()) {
			return _db;
		}
		else {
			makeConThreadLocal();
			return _db;
		}
	}



	~DatabaseOperationPrivate() {
		std::lock_guard<std::mutex> guard(mLock);
		for(auto&& i : dbConnPerThread){
			sqlite3_close_v2(*i);
			i = 0;
		}
	}

public:
	std::string dbPath;
	std::vector<sqlite3**> dbConnPerThread;
	std::mutex mLock;
};