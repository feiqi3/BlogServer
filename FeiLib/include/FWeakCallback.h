#ifndef FWEAKCALLBACK_H_
#define FWEAKCALLBACK_H_
#pragma once

#include <functional>
#include <memory>

namespace Fei {
	template<typename T, typename... ARGS>
	class FWeakCallback{
	public:
		FWeakCallback(const std::weak_ptr<T>& weak, std::function<void(T*, ARGS...)> func)
			:mWeak(weak), mFunction(std::move(func)) {}

		void operator()(ARGS&&...args) {
			auto ptr = mWeak.lock();
			if (!ptr)return;
			mFunction(ptr.get(), std::forward<ARGS>(args)...);
		}
	private:
		std::weak_ptr<T> mWeak;
		std::function<void(T*, ARGS...)> mFunction;
	};

	template<typename T, typename... ARGS>
	auto makeWeakFunction(const std::weak_ptr<T>& weak, std::function<void(T*, ARGS...)> func) {
		FWeakCallback callback(weak, func);
		std::function<void(ARGS...)> ret = [=callback](ARGS&&... args) {
			callback(std::forward<ARGS>(args)...);
		};
		return ret;
	}
}
#endif
