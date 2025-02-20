#ifndef ORM_H
#define ORM_H
#include "reflect"
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include "DataBaseOperation.h"
namespace Blog {

template <class T>
concept StringLike = std::is_convertible_v<T, std::string_view>;
template <class T>
concept NotStringLike = !std::is_convertible_v<T, std::string_view>;

template <typename T> class Condition {
public:
  template <StringLike U>
  constexpr Condition(const std::string& field,const std::string& op, U &&val) {
    _expr = field + op + safeStr(std::string(val)) ;
  }

  template <NotStringLike U>
  constexpr Condition(std::string field,const std::string& op, U val) {
    _expr = field + op + std::to_string(val);
  }

  constexpr Condition(Condition<T> a, Condition<T> b,const std::string& op) {
    _expr = "(" + a.toStr() + op + b.toStr() + ")";
  }

  constexpr Condition(Condition<T>& rhs, bool) {
    _expr = "NOT " + rhs.toStr() + "";
  }

  Condition<T> operator!() { return Condition<T>(*this, false); }

  Condition<T> operator&&(Condition<T> rhs) {
    return Condition(*this, rhs, " AND ");
  }

  Condition<T> operator||(Condition<T> rhs) {
    return Condition(*this, rhs, " OR ");
  }
  std::string toStr() { return _expr; }

private:
  std::string _expr;
};

template <typename T, typename U> class Field {
public:
  static constexpr Field<T,U> GetField(uint64_t fieldOffset) {
    T tmp;
    std::string str;
    reflect::for_each([&tmp,fieldOffset,&str](auto i) {
      bool isMatch =
          reflect::offset_of<i>(tmp) == fieldOffset;
      if (isMatch) {
        str = reflect::member_name<i>(tmp);
        return;
      }
    },tmp);
    return Field<T, U>(str, fieldOffset);
  }

  constexpr Condition<T> operator==(U&& val) {
    return Condition<T>(fieldSV, " = ", std::forward<U>(val));
  }

  constexpr Condition<T> operator!=(U&& val) {
    return Condition<T>(fieldSV, " != ", std::forward<U>(val));
  }

  constexpr Condition<T> operator<(U&& val) {
    return Condition<T>(fieldSV, " < ", std::forward<U>(val));
  }

  constexpr Condition<T> operator<=(U&& val) {
    return Condition<T>(fieldSV, " <= ", std::forward<U>(val));
  }

  constexpr Condition<T> operator>(U&& val) {
    return Condition<T>(fieldSV, " > ", std::forward<U>(val));
  }

  constexpr Condition<T> operator>=(U&& val) {
    return Condition<T>(fieldSV, " >= ", std::forward<U>(val));
  }

protected:
  constexpr std::string_view getFieldName() const { return fieldSV; }
  constexpr uint64_t getFieldOffset() const { return offset; }
  constexpr Field(const std::string &view, uint64_t off)
      : fieldSV(view), offset(off) {}

private:
  std::string fieldSV;
  uint64_t offset;
};

template <typename T> class Query {
public:
  constexpr Query(const std::string &table)
      : selectTableName(table), whereStr(""), mskip(0), mlimit(0) {}

  constexpr Query &Select() {
    op = " SELECT * FROM " + selectTableName;
    return *this;
  }

  constexpr Query &Delete() { op = " DELETE FROM " + selectTableName; }

  constexpr Query &Insert(const T &cls) {
    op = " INSERT INTO " + getClsDefineList() + " VALUES ";
    op += getClsValueList(cls);
    return *this;
  }

  constexpr Query &Where(Condition<T> whereCondition) {
    whereStr = whereCondition.toStr();
    return *this;
  }
  template <class U> constexpr Query &OrderBy(Field<T, U> field) {
    order = "ORDER BY " + field.getFieldName();
  }

  template <class U> constexpr Query &OrderByDesc(Field<T, U> field) {
    order = "ORDER BY " + field.getFieldName() + " DESC ";
  }

  constexpr Query &skip(int s) { mskip = s; }

  constexpr Query &limit(int s) { mlimit = s; }

  constexpr std::string toSql() const {
    std::string ret = op + " WHERE " + whereStr;
    if (!order.empty()) {
      ret += " " + order;
    }
    return ret;
  }

private:
  template <StringLike Str> static std::string to_string(Str &v) {
    return safeStr(std::string(v));
  }
  template <class V> static std::string to_string(V &v) {
    return std::to_string(v);
  }

  constexpr std::string getClsDefineList() {
    std::string ret = "(";
    int idx = 0;
    reflect::for_each([&idx, &ret](int i) {
      if (idx != 0) {
        ret = ret + ",";
      }
      ret = ret + reflect::member_name<i, T>();
    });
    ret += ") ";
    return ret;
  }

  constexpr std::string getClsValueList(const T &t) {
    std::string ret = "";
    ret += "(";
    int idx = 0;
    reflect::for_each([&idx, &ret, &t](int i) {
      if (idx != 0) {
        ret = ret + ",";
      }
      auto val = reflect::get<i>(t);
      ret += to_string(val);
    });
    ret += ") ";
    return ret;
  }


private:
  std::string selectTableName;
  std::string whereStr;
  std::string order;
  int mskip;
  int mlimit;
  std::string op;
};
}; // namespace Blog

#define FIELD(CLS,MEMBER) ::Blog::Field<CLS,decltype(((CLS*)0)->MEMBER)>::GetField( offsetof(CLS,MEMBER))

#endif