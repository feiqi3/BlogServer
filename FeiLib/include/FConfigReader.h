#ifndef FCONFIGREADER_H_
#define FCONFIGREADER_H_
#include "FDef.h"
#include "FSingleton.h"
#include <charconv>
#include <map>
#include <optional>
#include <algorithm>
#include <string>
#include <sstream>
#include <set>

namespace Fei {

class F_API FConfigReader : public FSingleton<FConfigReader> {
public:
  enum class Env {
    Test,
    Prod,
    Invalid,
  };

  FConfigReader(const std::string &cfgPath = "fei.cfg");
  Env getCurrentEnv() const { return mCurEnv; }
  std::optional<std::string> getCfg(const std::string &term) const {
	mUsedConfigs.insert(term);
    auto itor = mCfgMap.find(term);
    if (itor == mCfgMap.end())
      return std::nullopt;
    return itor->second;
  }

private:
  std::string mCfgPath;
  Env mCurEnv = Env::Test;
  std::map<std::string, std::string> mCfgMap;
  mutable std::set<std::string> mUsedConfigs;
};
}; // namespace Fei

/*
cfg file format:
```
env: prod
[[prod]]
a: 10
b: 100

[[test]]
a: 10
b: 200

```

*/

class F_API FCfgUtils {
	public:
	  template <class T> inline static bool toNumber(const std::string &str, T &out) {
		static_assert(std::is_arithmetic_v<T>,
					  "Invalid Type(NaN)");
	
		if constexpr (std::is_integral_v<T>) {
		  auto first = str.data();
		  auto last = str.data() + str.size();
		  auto result = std::from_chars(first, last, out);
		  return result.ec == std::errc() && result.ptr == last;
		} else if constexpr (std::is_floating_point_v<T>) {
		  std::istringstream iss(str);
		  iss >> std::ws >> out >> std::ws;
		  return !iss.fail() && iss.eof();
		} else {
		  return false;
		}
	  }
	
	inline static bool toBool(const std::string &str, bool &out){
			const auto ws = " \t\n\r\f\v";
			auto first = str.find_first_not_of(ws);
			if (first == std::string::npos) return false;  // 全是空白
			auto last = str.find_last_not_of(ws);
			std::string s = str.substr(first, last - first + 1);
	
			std::transform(s.begin(), s.end(), s.begin(),
						[](unsigned char c){ return std::tolower(c); });
	
			if (s == "true"  || s == "1") {
				out = true;
				return true;
			}
			if (s == "false" || s == "0") {
				out = false;
				return true;
			}
			return false;
		}
	};

#endif