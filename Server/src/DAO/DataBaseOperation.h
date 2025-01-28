#ifndef DATABASEOPERATION_H
#define DATABASEOPERATION_H
#include <string>

namespace Blog {
	class DatabaseOperationPrivate;
	class DatabaseOperation : FSingleton<DatabaseOperation> {
	public:
		static void LoadDB(const std::string& databaseName);
		static void Exec(const std::string& sql);
	private:
		DatabaseOperationPrivate* dp = nullptr;
	};

	class DBResult {
	
	private:
		void** mData;
	};
}
#endif