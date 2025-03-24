#ifndef ORM_H
#define ORM_H

#include "reflect"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "DataBaseOperation.h"
namespace Blog {

template <class T>
concept StringLike = std::is_convertible_v<T, std::string_view>;
template <class T>
concept NotStringLike = !std::is_convertible_v<T, std::string_view>;

template <typename T> class Condition {
public:
struct WithoutBracket{};

template <StringLike U>
  constexpr Condition(const std::string &field, const std::string &op,
                      U &&val) {
    _expr = field + op + safeStr(std::string(val));
  }

  template <NotStringLike U>
  constexpr Condition(std::string field, const std::string &op, U val) {
    _expr = field + op + std::to_string(val);
  }

  constexpr Condition(const std::string &field, const std::string &op) {
    _expr = field + op;
  }

  constexpr Condition(Condition<T> a, Condition<T> b, const std::string &op,WithoutBracket) {
    _expr =  a.toStr() + op + b.toStr() ;
  }

  constexpr Condition(Condition<T> a, Condition<T> b, const std::string &op) {
    _expr = "(" + a.toStr() + op + b.toStr() + ")";
  }

  constexpr Condition(Condition<T> &rhs, bool) {
    _expr = "NOT " + rhs.toStr() + "";
  }

  Condition<T> operator!() { return Condition<T>(*this, false); }

  Condition<T> operator&&(Condition<T> rhs) {
    return Condition(*this, rhs, " AND ");
  }

  Condition<T> operator-(const Condition<T> & rhs) {
    return Condition(*this, rhs, " , ",WithoutBracket{});
  }

  Condition<T> operator||(Condition<T> rhs) {
    return Condition(*this, rhs, " OR ");
  }
  std::string toStr()const { return _expr; }

private:
  std::string _expr;
};

struct FieldParameter {};

template <typename Tuple, typename Func, std::size_t... Indices>
void for_each_with_index_impl(Tuple& t, Func&& f, std::index_sequence<Indices...>) {
    // 使用折叠表达式，将每个元素的索引传递给lambda
    (std::forward<Func>(f)(std::get<Indices>(t), Indices), ...);
}

template <typename... Ts, typename Func>
void for_each_with_index(std::tuple<Ts...>& t, Func&& f) {
    for_each_with_index_impl(t, std::forward<Func>(f), std::index_sequence_for<Ts...>{});
}

// 判断类型T是否为tuple
template <typename T> struct is_tuple : std::false_type {};

template <typename... Ts>
struct is_tuple<std::tuple<Ts...>> : std::true_type {};

// 合并两个tuple类型的辅助结构体
template <typename TA, typename TB> struct merge_tuples;

// 当TA和TB都是tuple时
template <typename... Ts1, typename... Ts2>
struct merge_tuples<std::tuple<Ts1...>, std::tuple<Ts2...>> {
  using type = std::tuple<Ts1..., Ts2...>;
};

// 当TA和TB都是非tuple类型
template <typename T1, typename T2> struct merge_tuples {
  using type = std::tuple<std::enable_if_t<!is_tuple<T1>::value,T1>, std::enable_if_t<!is_tuple<T2>::value,T2>>;
};

// 当TA是非tuple类型，TB是tuple时
template <typename T, typename... Ts>
struct merge_tuples<T, std::tuple<Ts...>> {
  using type = std::tuple<std::enable_if_t<!is_tuple<T>::value,T>, Ts...>;
};

// 当TA是tuple类型，TB是非tuple时
template <typename... Ts, typename T>
struct merge_tuples<std::tuple<Ts...>,T> {
  using type = std::tuple<Ts...,std::enable_if_t<!is_tuple<T>::value,T>>;
};
template <typename T, typename U> class Field {
private:
  constexpr void conditionStaticAssert() {
    static_assert(!is_tuple<T>::value || !is_tuple<U>::value,
                  "Cannot use Field select as a condition.");
  }

public:
  static constexpr Field<T, U> GetField(uint64_t fieldOffset) {
    T tmp;
    std::string str;
    reflect::for_each(
        [&tmp, fieldOffset, &str](auto i) {
          bool isMatch = reflect::offset_of<i>(tmp) == fieldOffset;
          if (isMatch) {
            str = reflect::member_name<i>(tmp);
            return;
          }
        },
        tmp);
    return Field<T, U>(str, fieldOffset);
  }

  template <typename RT, typename RU>
  constexpr auto operator-(const Field<RT, RU> &fr) {
    using TupleRet = merge_tuples<U, RU>::type;
    std::string _TA = fieldSV;
    std::string _TB = fr.getFieldName();
    if constexpr (!std::is_same_v<RT, T>) {
      if constexpr (!is_tuple<T>::value) {
        _TA = std::string(reflect::type_name<T>()) + "." + _TA;
      }
      if constexpr (!is_tuple<RT>::value) {
        _TB = std::string(reflect::type_name<RT>()) + "." + _TB;
      }
      return Field<merge_tuples<T, RT>, TupleRet>(_TA + "," + _TB);
    } else {
      return Field<T, TupleRet>(_TA + "," + _TB);
    }
  }

  constexpr std::string toFieldSelect() const { return fieldSV; }

  constexpr Condition<T> operator=(U &&val) { return (*this) == std::forward<U>(val); }

  constexpr Condition<T> operator=(std::nullptr_t) {
    return (*this) == nullptr;
  }

  constexpr Condition<T> operator==(std::nullptr_t) {
    conditionStaticAssert();
    return Condition<T>(fieldSV, " NOT ", "NULL");
  }

  constexpr Condition<T> operator!=(std::nullptr_t) {
    conditionStaticAssert();
    return Condition<T>(fieldSV, " IS NOT ", "NULL");
  }

  constexpr Condition<T> operator==(U &&val) {
    conditionStaticAssert();
    return Condition<T>(fieldSV, " = ", std::forward<U>(val));
  }

  constexpr Condition<T> operator!=(U &&val) {
    conditionStaticAssert();
    return Condition<T>(fieldSV, " != ", std::forward<U>(val));
  }

  constexpr Condition<T> operator<(U &&val) {
    conditionStaticAssert();
    return Condition<T>(fieldSV, " < ", std::forward<U>(val));
  }

  constexpr Condition<T> operator<=(U &&val) {
    conditionStaticAssert();
    return Condition<T>(fieldSV, " <= ", std::forward<U>(val));
  }

  constexpr Condition<T> operator>(U &&val) {
    conditionStaticAssert();
    return Condition<T>(fieldSV, " > ", std::forward<U>(val));
  }

  constexpr Condition<T> operator>=(U &&val) {
    conditionStaticAssert();
    return Condition<T>(fieldSV, " >= ", std::forward<U>(val));
  }

  constexpr Condition<T> Like(U&& val) {
      conditionStaticAssert();
      return Condition<T>(fieldSV, " LIKE ", std::forward<U>(val));
  }

  constexpr Condition<T> operator==(FieldParameter) {
    conditionStaticAssert();
    return Condition<T>(fieldSV, " = ? ");
  }

  constexpr Condition<T> operator!=(FieldParameter) {
    conditionStaticAssert();
    return Condition<T>(fieldSV, " != ? ");
  }

  constexpr Condition<T> operator<(FieldParameter) {
    conditionStaticAssert();
    return Condition<T>(fieldSV, " < ?");
  }

  constexpr Condition<T> operator<=(FieldParameter) {
    conditionStaticAssert();
    return Condition<T>(fieldSV, " <= ? ");
  }

  constexpr Condition<T> operator>(FieldParameter) {
    conditionStaticAssert();
    return Condition<T>(fieldSV, " > ? ");
  }

  constexpr Condition<T> operator>=(FieldParameter) {
    conditionStaticAssert();
    return Condition<T>(fieldSV, " >= ? ");
  }

  constexpr Condition<T> Like(FieldParameter) {
      conditionStaticAssert();
      return Condition<T>(fieldSV, " LIKE ? ");
  }

  constexpr const std::string &getFieldName() const { return fieldSV; }
  constexpr uint64_t getFieldOffset() const { return offset; }

  constexpr Field(const std::string &view, uint64_t off)
      : fieldSV(view), offset(off) {}
  constexpr Field(const std::string &view) : fieldSV(view), offset(0) {}

protected:
  const std::string fieldSV;
  const uint64_t offset;
};

class SelectCount {
  public:
    constexpr SelectCount(){}

    template<class T, class U>
    constexpr SelectCount(const Field<T,U>& field){
      str = field.toFieldSelect();
    }

    template<class T, class U>
    constexpr SelectCount(const Condition<T>& cond){
      str = cond.toStr();
    }

    constexpr const std::string& toStr()const{
      return str;
    }
    private:
    std::string str = "*";
  };

template <typename T> class Query {
public:

  constexpr Query() : selectTableName(), whereStr(""), mskip(0), mlimit(0) {}

  constexpr Query &Select(const SelectCount& count) {
      static_assert(std::is_integral_v<T>, "T must be int when query.");
      op = " SELECT COUNT("+count.toStr()+ ") FROM ";
      return *this;
  }

  constexpr Query &Select() {
    op = " SELECT * FROM ";
    selectTableName = std::string(getTableName());
    return *this;
  }

  // Use field return type as query type
  template <class FT> constexpr Query(const Field<FT, T> &queryField) {
    op = "SELECT " + queryField.toFieldSelect() + " FROM ";
  }

  constexpr auto getTableName() {
    static_assert(!(is_tuple<T>::value || std::is_integral_v<T> ||
                    std::is_floating_point_v<T> ||
                    std::is_convertible_v<T, std::string>),
                  "This function can only be called for queries that are for Entity rather than multi cols or specific col.");

    return (reflect::type_name<typename T::_ENTITY_TABLE_NAME::type>());
  }

  constexpr Query &From(const std::string &table) {
    static_assert(is_tuple<T>::value || std::is_integral_v<T> ||
                      std::is_floating_point_v<T> ||
                      std::is_convertible_v<T, std::string>,
                  "This function can only be called for queries that select multi cols or specific col. Like \"Select user,id from Users\", not \"Select * from Users\"");
    selectTableName = table;
    return *this;
  }

  constexpr Query &Delete() {
    op = " DELETE FROM ";
    selectTableName = std::string(getTableName());
    return*this;
  }

  constexpr Query &Insert(const T &cls) {
    op = " INSERT INTO ";
    selectTableName = std::string(getTableName()) + getClsDefineList();
    insertValues = " VALUES " + getClsValueList(cls);
    return *this;
  }

  constexpr Query &update(const Condition<T> &setVal) {
    op = "  UPDATE ";
    selectTableName = std::string(getTableName());
    set = " SET " + setVal.toStr();
    return *this;
  }

  template<class U>
  constexpr Query &Where(const Condition<U> &whereCondition) {
    //if is multi-col select condition, no check here
    if constexpr(is_tuple<T>::value || std::is_integral_v<T> ||
      std::is_floating_point_v<T> ||
      std::is_convertible_v<T, std::string>){
    }else{
      //if this is a Entity query, do check Entity type.
      static_assert(std::is_same_v<T, U>,"Field must from the same table as query.");
    }
    whereStr = whereCondition.toStr();
    return *this;
  }
  template <class TU,class U> constexpr Query &OrderBy(Field<TU, U> field) {
    order = "ORDER BY " + field.getFieldName();
    return *this;
  }

  template <class TU,class U> constexpr Query &OrderByDesc(Field<TU, U> field) {
    order = "ORDER BY " + field.getFieldName() + " DESC ";
    return *this;
  }

  constexpr Query &skip(int s) {
    mskip = s;
    return *this;
  }

  constexpr Query &limit(int s) {
    mlimit = s;
    return *this;
  }

  constexpr Query& skip(FieldParameter) {
      mskip = -1;
      return *this;
  }

  constexpr Query& limit(FieldParameter) {
      mlimit = -1;
      return *this;
  }

  constexpr std::string toSql() const {
    std::string ret = op;
    ret += selectTableName;
    if (!set.empty()) {
      ret += set;
    }

    if (!insertValues.empty()) {
      ret += insertValues;
    }
    if (!whereStr.empty()) {
      ret += " WHERE " + whereStr;
    }
    if (!order.empty()) {
      ret += " " + order;
    }
    if (mlimit > 0) {
      ret += " LIMIT " + std::to_string(mlimit);
    }

    if (mlimit < 0) {
        ret += " LIMIT ? ";
    }

    if (mskip  > 0) {
      ret += " OFFSET " + std::to_string(mskip);
    }
    if (mskip < 0) {
        ret += " OFFSET ? ";
    }
    ret += ";";
    return ret;
  }

public:
    Query& prepare() {
        result = DatabaseOperation::instance()->Prepare(toSql());
        return *this;
    }

  std::vector<T> getVector() const {
    std::vector<T> ret;
    while (result->step()) {
      ret.push_back(ToEntity());
    }
    return ret;
  }
  DBResultPtr getResult() const { return result; }

  template <typename... Args> Query &exec(Args &&...arg) {
      if (result) {
          DatabaseOperation::instance()->ReExec(result, std::forward<Args>(arg)...);
      }
      else {
          result = DatabaseOperation::instance()->Exec(toSql(), std::forward<Args>(arg)...);
      }
      return *this;
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
    T t;
    reflect::for_each(
        [&ret](auto i) {
          if (i != 0) {
            ret = ret + " , ";
          }
          ret = ret + std::string(reflect::member_name<i, T>());
        },
        t);
    ret += ") ";
    return ret;
  }

  constexpr std::string getClsValueList(const T &t) {
    std::string ret = "";
    ret += "(";
    reflect::for_each(
        [&ret, &t](auto i) {
          if (i != 0) {
            ret = ret + ",";
          }
          auto val = reflect::get<i>(t);
          ret += to_string(val);
        },
        t);
    ret += ") ";
    return ret;
  }

private:
  std::string selectTableName;
  std::string insertValues;
  std::string whereStr;
  std::string order;
  std::string set;
  int mskip = 0;
  int mlimit = 0;
  std::string op;
  DBResultPtr result;

private:
  T ToEntity() const {
    if constexpr (is_tuple<T>::value) {
      T temp;
      for_each_with_index(temp,[this](auto& element,auto idx){
        result->setByCol(element, idx);
      });
      
      return temp;
    } else if constexpr (std::is_integral_v<T> || std::is_floating_point_v<T> ||
                         std::is_convertible_v<std::remove_cv_t<T>,
                                               std::string>) {
      T t;
      result->setByCol(t, 0);
      return t;
    } else {
      T temp{};
      assert(reflect::size<T>() == result->getCols());
      reflect::for_each(
          [&temp, this](auto I) { result->setByCol(reflect::get<I>(temp), I); },
          temp);
      return temp;
    }
  };
};
}; // namespace Blog

#define PARAM                                                                  \
  ::Blog::FieldParameter {}

#define FIELD(CLS, MEMBER)                                                     \
  ::Blog::Field<CLS, decltype(((CLS *)0)->MEMBER)>::GetField(                  \
      offsetof(CLS, MEMBER))

#define ENTITY_TABLE(TB_NAME)                                                  \
  struct _ENTITY_TABLE_NAME {                                                  \
    struct TB_NAME {};                                                         \
    using type = typename _ENTITY_TABLE_NAME::TB_NAME;                         \
  };

#endif