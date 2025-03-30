#include "sqlite3.h"
#include "FLogger.h"
#include <iostream>
int main(){
    sqlite3* db = 0;
    new Fei::Logger({});
    sqlite3_initialize();
    bool isSuccess =SQLITE_OK == sqlite3_open_v2("/home/feiqi3/workspace/BlogServer/Server/resources/database/test.db", &db, SQLITE_OPEN_READWRITE, 0);
    

    
    if(!isSuccess){
        std::cerr<<sqlite3_errmsg(db);
    }
}