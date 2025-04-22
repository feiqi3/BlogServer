#include <sqlite3.h>
#include <vector>
#include <memory>

#include "DataBaseOperation.h"
#include "FLogger.h"

#define SQL_SINGLE_CONNECTION 1
#define SQL_MAKE_NEW 1

#define MODULE_NAME "DatabaseOperation"
#define SQL_CHECK(x,s) if(x != SQLITE_OK){s}

#define SQL_STRICT_TYPE_CHECK 1

using namespace Fei;
using namespace Blog;

namespace {
	thread_local sqlite3* _db;

	int toSqlite3Type(DataType dtp) {
		switch (dtp)
		{
		case Blog::DataType::_NULL:
			return SQLITE_NULL;
			break;
		case Blog::DataType::_INT:
			return SQLITE_INTEGER;
		case Blog::DataType::_FLOAT:
			return SQLITE_FLOAT;
		case Blog::DataType::_TEXT:
			return SQLITE_TEXT;
		case Blog::DataType::_BLOB:
			return SQLITE_BLOB;
		default:
			return SQLITE_TEXT;
			break;
		}
	}
	DataType toBlogType(int sqlite3Type) {
		switch (sqlite3Type)
		{
		case SQLITE_INTEGER:
			return DataType::_INT;
		case SQLITE_FLOAT:
			return DataType::_FLOAT;
		case SQLITE_TEXT:
			return DataType::_TEXT;
		case SQLITE_NULL:
			return DataType::_NULL;
		case SQLITE_BLOB:
			return DataType::_BLOB;
		default:
			return DataType::_TEXT;
			break;
		}
	}

}

class DatabaseOperationPrivate {
public:
	std::string dbPath;
	std::vector<sqlite3**> dbConnPerThread;
	std::mutex mLock;
public:
	const char* getErrMsg()const {
		if(_db != 0)
			return sqlite3_errmsg(_db);
		return 0;
	};

	bool exec(const std::string& sql) {
		auto conn = getDbConn();
		assert(conn != 0);
		char* errMessage = 0;
		SQL_CHECK(sqlite3_exec(conn, sql.c_str(), 0, 0, &errMessage), {
			Logger::instance()->log(MODULE_NAME,lvl::err,"SQL \"{}\" execute error,reason: \"{}\"",sql,errMessage);
			sqlite3_free(errMessage);
			});
	}

	bool localDbConnValid()const {
		return _db != nullptr;
	}

	void makeConThreadLocal() {
		if (_db != nullptr)return;
		int flag = SQLITE_OPEN_READWRITE | SQLITE_OPEN_SHAREDCACHE;
		std::lock_guard<std::mutex> guard(mLock);
		SQL_CHECK(sqlite3_open_v2(dbPath.c_str(), &_db, flag, nullptr), {
		Logger::instance()->log(MODULE_NAME,lvl::critical,"Try open database \"{}\" failed. reason \"{}\"",dbPath, sqlite3_errmsg(_db));
			});
		sqlite3_busy_timeout(_db,5000);
		sqlite3_exec(_db, "PRAGMA foreign_keys = ON;", 0, 0, 0);
		sqlite3_exec(_db, "PRAGMA journal_mode=WAL;", 0, 0, 0);
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

	DBResultPtr exec(sqlite3_stmt* stmt){
		int columnCount = sqlite3_column_count(stmt);
		//For insert/delete/udate
		return std::make_shared<DBResult>(stmt,columnCount);
	}

	sqlite3_stmt* prepare(const std::string& sql){
		auto conn = getDbConn();
		sqlite3_stmt* stmt = 0;
		const char* unknownSql = 0;
		SQL_CHECK(sqlite3_prepare_v2(conn, sql.c_str(), sql.size(), &stmt, &unknownSql), {
			Logger::instance()->log(lvl::critical,"SQL error, sql: \"{}\",error part: \"{}\", reason \"{}\"",sql,unknownSql == 0 ? "" : unknownSql,sqlite3_errmsg(conn));
			return 0;
			});
		if(stmt == 0){
			Fei::Logger::instance()->log(Fei::lvl::critical,"SQL error");
		}
		return stmt;
	}

	DBResultPtr execWithResult(const std::string& sql,const std::string* strs = 0,uint32 length = 0) {
		auto conn = getDbConn();
		sqlite3_stmt* stmt = 0;
		const char* unknownSql = 0;
		SQL_CHECK(sqlite3_prepare_v2(conn, sql.c_str(), sql.size(), &stmt, &unknownSql), {
			Logger::instance()->log(lvl::err,"SQL error, sql: \"{}\",error part: \"{\"}, reason \"{}\"",sql,unknownSql == 0 ? "" : unknownSql,sqlite3_errmsg(conn));
			return 0;
			});
		for (auto i = 0; i < length; ++i) {
			sqlite3_bind_text(stmt, i+1, strs[i].c_str(), -1, SQLITE_STATIC);
		}
		int columnCount = sqlite3_column_count(stmt);
		return std::make_shared<DBResult>(stmt, columnCount);
	}

	static int getInteger(sqlite3_stmt* stmt, uint32 col, DataType type) {
#ifdef SQL_STRICT_TYPE_CHECK
		if (type != DataType::_INT) {
			Logger::instance()->log(MODULE_NAME, lvl::warn, "Implicit data cast may happen.");
		}
#endif // SQL_STRICT_TYPE_CHECK
		return sqlite3_column_int(stmt, col);
	}

	static int64_t getInteger64(sqlite3_stmt* stmt, uint32 col, DataType type) {
#ifdef SQL_STRICT_TYPE_CHECK
		if (type != DataType::_FLOAT && type != DataType::_INT) {
			Logger::instance()->log(MODULE_NAME, lvl::warn, "Implicit data cast may happen.");
		}
#endif // SQL_STRICT_TYPE_CHECK
		return sqlite3_column_int64(stmt, col);
	}

	static double getFloat(sqlite3_stmt* stmt, uint32 col, DataType type) {
#ifdef SQL_STRICT_TYPE_CHECK
		if (type != DataType::_FLOAT && type != DataType::_INT) {
			Logger::instance()->log(MODULE_NAME, lvl::warn, "Invalid data cast may happen.");
		}
#endif // SQL_STRICT_TYPE_CHECK
		return sqlite3_column_double(stmt, col);
	}

	static const unsigned char* getString(sqlite3_stmt* stmt, uint32 col, DataType type) {
#ifdef SQL_STRICT_TYPE_CHECK
		if (type != DataType::_TEXT && type != DataType::_BLOB) {
			Logger::instance()->log(MODULE_NAME, lvl::warn, "Implicit data cast may happen.");
		}
#endif // SQL_STRICT_TYPE_CHECK
		return sqlite3_column_text(stmt, col);
	}

	~DatabaseOperationPrivate() {
		std::lock_guard<std::mutex> guard(mLock);
		for (auto&& i : dbConnPerThread) {
			sqlite3_close_v2(*i);
			i = 0;
		}
	}

};
void Blog::DatabaseOperation::bind(void* stmt,int i,std::nullptr_t nullable){
	auto _s = (sqlite3_stmt*) stmt;
	sqlite3_bind_null(_s, i);
}

void Blog::DatabaseOperation::bind(void* stmt,int i,int v){
	auto _s = (sqlite3_stmt*) stmt;
	 sqlite3_bind_int(_s, i, v);
}
void Blog::DatabaseOperation::bind(void* stmt,int i,int64_t v){
	auto _s = (sqlite3_stmt*) stmt;
	sqlite3_bind_int64(_s, i, v);
}

void Blog::DatabaseOperation::bind(void* stmt,int i,double v){
	auto _s = (sqlite3_stmt*) stmt;
	sqlite3_bind_double(_s, i, v);

}
void Blog::DatabaseOperation::bind(void* stmt,int i,const std::string& v){
	auto _s = (sqlite3_stmt*) stmt;
	sqlite3_bind_text(_s, i, v.c_str(),v.size(),SQLITE_TRANSIENT);
}
void Blog::DatabaseOperation::bind(void *stmt, int i, const char *v) {
	auto _s = (sqlite3_stmt*) stmt;
	sqlite3_bind_text(_s, i, v,-1,SQLITE_TRANSIENT);
}

Blog::DatabaseOperation::DatabaseOperation():dp(new DatabaseOperationPrivate)
{
	sqlite3_initialize();
}

Blog::DatabaseOperation::~DatabaseOperation()
{
	sqlite3_shutdown();

	delete dp;
	dp = 0;
}

void Blog::DatabaseOperation::LoadDB(const std::string& databaseName)
{
	//Try to link sqlite and store info.
	sqlite3* dbConn = 0;
	int flag = SQLITE_OPEN_READWRITE;

	SQL_CHECK(sqlite3_open_v2(databaseName.c_str(), &dbConn, flag, nullptr), {
		Logger::instance()->log(MODULE_NAME,lvl::critical,"Try open database \"{}\" failed. reason \"{}\"",databaseName, sqlite3_errmsg(dbConn));
		});
	dp->dbPath = databaseName;
	SQL_CHECK(sqlite3_close_v2(dbConn), {
		Logger::instance()->log(MODULE_NAME,lvl::err,"Try close database failed. reason \"{}\"", sqlite3_errmsg(dbConn));
	});
}

DBResultPtr Blog::DatabaseOperation::exec(void* stmt){
	auto _s = (sqlite3_stmt*)stmt;
	return dp->exec(_s);
}

int Blog::DatabaseOperation::getParameterIndex(const DBResultPtr& ptr, const std::string& name){
	auto idx = sqlite3_bind_parameter_index((sqlite3_stmt*)ptr->mData,name.data());
	return idx;
}

void Blog::DatabaseOperation::resetStmt(void* stmt)
{
	auto _s = (sqlite3_stmt*)stmt;
	sqlite3_clear_bindings(_s);
	sqlite3_reset(_s);
}

void* Blog::DatabaseOperation::stmtPrepare(const std::string& sql){
	return dp->prepare(sql);
}

std::shared_ptr<DBResult> Blog::DatabaseOperation::Exec(const std::string& sql)const
{
	return dp->execWithResult(sql);
}

DBResultPtr Blog::DatabaseOperation::Exec(const std::string& sqlFmt, const std::vector<std::string>& userInParameter)const
{
	return dp->execWithResult(sqlFmt, userInParameter.data(), userInParameter.size());
}

Blog::DBResult::DBResult(void *data, int cols) : mData(data), cols(cols) {}

const char* Blog::DatabaseOperation::getErrMsg() const
{
	return dp->getErrMsg();
}

const char* Blog::DBResult::getString(uint32_t col) const
{
	innerCheck(col);
	return (const char*)DatabaseOperationPrivate::getString((sqlite3_stmt*)mData, col, getType(col));
}

int Blog::DBResult::getInteger(uint32_t col) const
{
	innerCheck(col);
	return DatabaseOperationPrivate::getInteger((sqlite3_stmt*)mData, col, getType(col));
}

int64_t Blog::DBResult::getInteger64(uint32_t col) const
{
	innerCheck(col);
	return DatabaseOperationPrivate::getInteger64((sqlite3_stmt*)mData, col, getType(col));
}

double Blog::DBResult::getFloat(uint32_t col) const
{
	innerCheck(col);
	return DatabaseOperationPrivate::getFloat((sqlite3_stmt*)mData, col, getType(col));
}

bool Blog::DBResult::step()
{
	auto res = sqlite3_step((sqlite3_stmt*)mData);
	if(res == SQLITE_ROW || res == SQLITE_DONE){
		return res == SQLITE_ROW;
	}
	this->errReason = DatabaseOperation::instance()->getErrMsg();
	if(errReason != 0){
		Logger::instance()->log(MODULE_NAME,lvl::err,"SQL error, reason \"{}\"",this->errReason);
	}
	return false;
}

bool Blog::DBResult::excute(){
	auto ret = sqlite3_step((sqlite3_stmt*)mData);
	while(ret!=SQLITE_DONE){
		auto t = sqlite3_step((sqlite3_stmt*)mData);;
		if(t!= SQLITE_ROW){
			ret = t;
			break;
		}
	}
	if(ret == SQLITE_DONE){
		return true;
	}
	else{
		this->errReason = DatabaseOperation::instance()->getErrMsg();
		return false;
	}
}

Blog::DBResult::~DBResult()
{
	sqlite3_finalize((sqlite3_stmt*)this->mData);
}
DataType Blog::DBResult::getType(int col)const{
	return toBlogType(sqlite3_column_type((sqlite3_stmt*)mData, col));
}

void Blog::DBResult::innerCheck(uint32_t col) const
{
	if (col >= cols)
		throw DatabaseExcceptionColOutofRange();
}

std::string Blog::safeStr(const std::string &in) {
	char * str = sqlite3_mprintf("%Q", in.c_str());
	std::string ret(str);
	sqlite3_free(str);
	return ret;
}