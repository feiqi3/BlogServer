#pragma once
#include <string>
#include "nlohmann/json.hpp"

using namespace nlohmann;
namespace {
	bool configStrProcess(const std::string& input,json& inJson,bool needCreate) {
		size_t start = 0, end;
		json& j = inJson;
		while ((end = input.find('.', start)) != std::string::npos) {
			std::string_view view(input.begin() + start,input.begin() + end); 
			if (view.size() == 0)return false;
			auto itor = j.find(view);
			if (itor == j.end()) {
				if(!needCreate)return false;
				j += view;
				itor = j.find(view);
				assert(itor != j.end());
			}
			j = *itor;
			start = end + 1;
		}
		std::string_view view(input.begin() + start, input.begin() + end);
		if (view.size() == 0)return false;
		auto itor = j.find(view);
		if (itor == j.end()) {
			if (!needCreate)return false;
			j += view;
			itor = j.find(view);
			assert(itor != j.end());
		}
		j = *itor;
		return true;
	}
};
namespace Blog {
	class ServerConfig{
	public:
		ServerConfig(const std::string& configDir);
		template<class T>
		bool get(const std::string& key,T& out)const;
		template<class T>
		bool set(const std::string& key, T& val);
	private:
		json mConfigJson;
	};

	template<class T>
	inline bool ServerConfig::get(const std::string& key, T& out) const
	{
		auto& j = mConfigJson;
		configStrProcess(key, j, true);

		try {
			out = j.get<T>(key);
		}
		catch (...) {
			return false;
		}
		return true;
	}

	template<class T>
	inline bool ServerConfig::set(const std::string& key, T& val)
	{
		auto& j = mConfigJson;
		configStrProcess(key, j, true);

		try {
			j = val;
		}
		catch (...) {
			return false;
		}
		return true;
	}

};