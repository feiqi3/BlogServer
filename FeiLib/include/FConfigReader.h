#ifndef FCONFIGREADER_H_
#define FCONFIGREADER_H_
#include "FDef.h"
#include "FSingleton.h"
#include <string>
#include <map>
#include <optional>
namespace Fei {
	class FConfigReader : public FSingleton<FConfigReader> {
	public:
		enum class Env
		{
			Test,
			Prod,
			Invalid,
		};

		FConfigReader(const std::string& cfgPath = "fei.cfg");
		Env getCurrentEnv()const {
			return mCurEnv;
		}
		std::optional<std::string> getCfg(const std::string& term)const {
			auto itor = mCfgMap.find(term);
			if (itor == mCfgMap.end())return std::nullopt;
			return itor->second;
		}
	private:
		std::string mCfgPath;
		Env mCurEnv = Env::Test;
		std::map<std::string, std::string> mCfgMap;
	};
};


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
#endif