#ifndef DATABASEOPERATION_H
#define DATABASEOPERATION_H
#include <string>
#include <vector>
#include "Utils/Singleton.h"

class DatabaseOperationPrivate;
namespace Blog {
	
	enum class DataType {
		_NULL,
		_INT,
		_FLOAT,
		_TEXT,
		_BLOB,
	};
	
	class DatabaseOperation : public Singleton<DatabaseOperation> {
	public:
		DatabaseOperation();
		~DatabaseOperation();
		static void LoadDB(const std::string& databaseName);
		static void Exec(const std::string& sql);
	private:
		DatabaseOperationPrivate* dp = nullptr;
	};

	class DBResult {
	public:
		DBResult(void* data, std::vector<DataType> datatypes):mData(data),mResultTypeByCol(datatypes) {}
		const char* getString(uint32_t col)const;
		void step();
		~DBResult();
	private:
		int cols = 0, rows = 0;
		uint32_t curRow=0;
		std::vector<DataType> mResultTypeByCol;
		void* mData;
		
	};
}
#endif