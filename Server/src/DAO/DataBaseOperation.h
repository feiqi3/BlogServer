#ifndef DATABASEOPERATION_H
#define DATABASEOPERATION_H
#include "FException.h"
#include "Utils/Singleton.h"
#include "reflect"
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
  virtual std::string reason() const override { return "Column out of range!"; }
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

class DBResult {
public:
    DBResult(void* data, int cols);
    const char* getErrMsg() const { return errReason; }
    uint32_t getCols() const { return cols; }
    // the returned ptr will be invalid after step()
    const char* getString(uint32_t col) const;
    int getInteger(uint32_t col) const;
    int64_t getInteger64(uint32_t col) const;
    double getFloat(uint32_t col) const;
    bool step();
    bool excute();
    ~DBResult();

public:
    template <IsInteger T> void setByCol(T& t, int col) { t = getInteger(col); }

    template <IsFloat T> void setByCol(T& t, int col) { t = getFloat(col); }

    template <IsStringLike T> void setByCol(T& t, int col) { t = getString(col); }

    friend class DatabaseOperation;
private:
    DataType getType(int col) const;
    void innerCheck(uint32_t col) const;
    void* mData;
    int cols = 0;
    const char* errReason = 0;
};

class DatabaseOperation : public Singleton<DatabaseOperation> {
public:
  DatabaseOperation();
  ~DatabaseOperation();
  void LoadDB(const std::string &databaseName);
  DBResultPtr Exec(const std::string &sql) const;
  DBResultPtr Exec(const std::string &sqlFmt,
                   const std::vector<std::string> &userInParameter) const;
  const char* getErrMsg() const ;
  template <typename... Args>
  DBResultPtr Exec(const std::string &sqlFmt, Args &&...arg) {
    auto stmt = stmtPrepare(sqlFmt);
    bindArgs(stmt, std::forward<Args>(arg)...);
    return exec(stmt);
  };

  DBResultPtr Prepare(const std::string& sqlFmt) {
      auto stmt = stmtPrepare(sqlFmt);
      return exec(stmt);
  }

  template <typename... Args>
  void ReExec(const DBResultPtr& ptr, Args &&...arg) {
      auto stmt = ptr->mData;
      resetStmt(stmt);
      bindArgs(stmt, std::forward<Args>(arg)...);
  }

private:

  void *stmtPrepare(const std::string &sql);
  DBResultPtr exec(void *stmt);

  template <typename... Args> void bindArgs(void *stmt, Args &&...arg) {
    _bind(stmt, 1, std::forward<Args>(arg)...);
  };

  void resetStmt(void* stmt);
  void bindArgs(void *stmt) { (void)(stmt); };
  template <typename... Args, typename T>
  void _bind(void *stmt, int i, T &&t, Args &&...arg) {
    bind(stmt, i, t);

    _bind(stmt, i + 1, std::forward<Args>(arg)...);
  };
  void _bind(void *) { return; }
  void _bind(void *, int) { return; }

  void bind(void *stmt, int i, std::nullptr_t nullable);
  void bind(void *stmt, int i, int v);
  void bind(void *stmt, int i, int64_t v);
  void bind(void *stmt, int i, double v);
  void bind(void *stmt, int i, const std::string &v);
  void bind(void *stmt, int i, const char *v);
  void bind(void *stmt, int i, uint32_t v) { bind(stmt, i, (int)v); }
  void bind(void *stmt, int i, uint64_t v) { bind(stmt, i, (int64_t)v); }

  //Return bind parameter count;
  template <typename T>
  int bind(void* stmt, int offset, const T& cls ){
    reflect::for_each(
        [&](auto j) {
          auto val = reflect::get<j>(cls);
          bind(stmt, offset + j, val);
        },
        cls);
        return reflect::size(cls);
  }
  DatabaseOperationPrivate *dp = nullptr;
};

std::string safeStr(const std::string &in);
} // namespace Blog
#endif