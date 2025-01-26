#ifndef FWEAKCALLBACK_H_
#define FWEAKCALLBACK_H_
#pragma once

#include <functional>
#include <memory>

namespace Fei {
	template<typename T, typename... ARGS>
	class FWeakCallback {
	public:
		FWeakCallback(const std::weak_ptr<T>& weak, void (T::* func)(ARGS...))
			:mWeak(weak), mFunction(func) {}

		void operator()(ARGS&&...args) const {
			auto ptr = mWeak.lock();
			if (!ptr)return;
			mFunction(ptr.get(), std::forward<ARGS>(args)...);
		}
	private:
		std::weak_ptr<T> mWeak;
		std::function<void(T*, ARGS...)> mFunction;
	};

	template<typename T, typename... ARGS>
	auto makeWeakFunction(const std::weak_ptr<T>& weak, void (T::* func)(ARGS...)) {
		FWeakCallback callback(weak, func);
		std::function<void(ARGS...)> ret = [callback](ARGS&&... args) {
			callback(std::forward<ARGS>(args)...);
		};
		return ret;
	}

	template<typename T, typename... ARGS>
	auto makeWeakFunction(const std::shared_ptr<T>& ptr, void (T::* func)(ARGS...)) {
		return makeWeakFunction(std::weak_ptr<T>(ptr), func);
	}

}
#endif
