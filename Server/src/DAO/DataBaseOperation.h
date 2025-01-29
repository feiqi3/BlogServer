#ifndef DATABASEOPERATION_H
#define DATABASEOPERATION_H
#include <string>
#include <vector>
#include <memory>
#include "FException.h"
#include "Utils/Singleton.h"

class DatabaseOperationPrivate;
namespace Blog {

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

	class DatabaseOperation : public Singleton<DatabaseOperation> {
	public:
		DatabaseOperation();
		~DatabaseOperation();
		void LoadDB(const std::string& databaseName);
		std::shared_ptr<DBResult> Exec(const std::string& sql);
	private:
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
	private:
		void innerCheck(uint32_t col)const;
		void* mData;
		std::vector<DataType> mResultTypeByCol;
		int cols = 0;
	};
}
#endif