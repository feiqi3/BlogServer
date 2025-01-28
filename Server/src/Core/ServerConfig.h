#pragma once
#include <string>
#include "nlohmann/json.hpp"

namespace Blog {
	class ServerConfig{
	public:
		ServerConfig(const std::string& configDir);
		template<class T>
		bool get(const std::string& key,T& out)const;
	private:
		nlohmann::json mConfigJson;
	};

	template<class T>
	inline bool ServerConfig::get(const std::string& key, T& out) const
	{
		auto itor = mConfigJson.find(key);
		if (itor == mConfigJson.end())return false;

		try {
			out = mConfigJson.get<T>(key);
		}
		catch (...) {
			return false;
		}
		return true;
	}

};