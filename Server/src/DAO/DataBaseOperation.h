#ifndef DATABASEOPERATION_H
#define DATABASEOPERATION_H
#include <cstddef>
#include <string>
#include <utility>
#include <vector>
#include <memory>
#include "FException.h"
#include "Utils/Singleton.h"

class DatabaseOperationPrivate;
namespace Blog {
template <typename T>
concept IsInteger = std::is_integral_v<T>;

template <typename T>
concept IsFloat = std::is_floating_point_v<T>;

template <typename T>
concept IsStringLike = std::is_convertible_v<T, std::string>;

	class DatabaseExcceptionColOutofRange : public ::Fei::FException {
		public:
			virtual std::string reason()const override{ return "Column out of range!"; }
	};

	enum class DataType {
		_NULL,
		_INT,
		_FLOAT,
		_TEXT,
		_BLOB,
	};
	class DBResult;
	using DBResultPtr = std::shared_ptr<DBResult>;
	class DatabaseOperation : public Singleton<DatabaseOperation> {
	public:
		DatabaseOperation();
		~DatabaseOperation();
		void LoadDB(const std::string& databaseName);
		DBResultPtr Exec(const std::string& sql)const;
		DBResultPtr Exec(const std::string& sqlFmt,const std::vector<std::string>& userInParameter)const;

		template<typename... Args>
		DBResultPtr Exec(const std::string& sqlFmt,Args&&...arg){
			auto stmt = stmtPrepare(sqlFmt);
			bindArgs(stmt, std::forward(arg)...);
			return exec(stmt);
		};

	private:
		void* stmtPrepare(const std::string& sql);
		DBResultPtr exec(void* stmt);

		template<typename... Args>
		void bindArgs(void* stmt,Args&&...arg){
			bind(stmt,1,std::forward(arg)...);
		};

		template<typename... Args,typename T>
		void bind(void* stmt,int i,T&& t,Args&&...arg){
			bind(stmt,i,t);
			bind(stmt,i+ 1,std::forward(arg)...);
		};
		void bind(void*){}
		void bind(void* stmt,int i,std::nullptr_t nullable);
		void bind(void* stmt,int i,int v);
		void bind(void* stmt,int i,int64_t v);
		void bind(void* stmt,int i,double v);
		void bind(void* stmt,int i,const std::string& v);

		DatabaseOperationPrivate* dp = nullptr;
	};

	class DBResult {
	public:
		DBResult(void* data, std::vector<DataType> datatypes):mData(data),mResultTypeByCol(datatypes),cols(datatypes.size()) {}

		uint32_t getCols()const { return cols; }
		//the returned ptr will be invalid after step()
		const char* getString(uint32_t col)const;
		int getInteger(uint32_t col)const;
		int64_t getInteger64(uint32_t col)const;
		double getFloat(uint32_t col)const;
		DataType getType(uint32_t col)const {
			innerCheck(col);
			return mResultTypeByCol[col];
		}
		bool step();
		~DBResult();

	public:
		template<IsInteger T>
		void setByCol(T& t,int col){
			t = getInteger(col);
		}

		template<IsFloat T>
		void setByCol(T& t,int col){
			t = getFloat(col);
		}

		template<IsStringLike T>
		void setByCol(T& t,int col){
			t = getString(col);
		}
	private:
		void innerCheck(uint32_t col)const;
		void* mData;
		std::vector<DataType> mResultTypeByCol;
		int cols = 0;
	};

	std::string safeStr(const std::string& in);
}
#endif