#include "Http/FRouter.h"
#include "Http/FPathMatcher.h"
#include "FLogger.h"
#include "tbb/concurrent_priority_queue.h"
#include "tbb/concurrent_map.h"

namespace Fei::Http {
	struct __ControllerAndPattern {
		uint64 priority = 0;
		Method requestMethod = Method::Invalid;
		FPathMatcher* PathMatcher = nullptr;
		FControllerFunc ControllerFunc = nullptr;
		FControllerPtr ControllerBase = nullptr;
		std::atomic_bool avtive = true;
		~__ControllerAndPattern() {
			delete PathMatcher;
			PathMatcher = 0;
		}
	};
	using ControllerAndPatternPtr = std::shared_ptr<__ControllerAndPattern>;

	struct __ControllerAndPatternCompare {
		bool operator()(uint64 t1, uint64 t2) const{
			return t1 > t2;
		}
	};

	using PathOrderQueue = tbb::concurrent_map<uint64,ControllerAndPatternPtr, __ControllerAndPatternCompare>;

	class __FRouterInner {
	public:
		__FRouterInner() {
			mControllerOrderQueue.resize((uint32)Method::MAX_SIZE);
			mControllerOrderQueueEraseLocks.reset(new std::mutex[(uint32)Method::MAX_SIZE]);
		}
		std::mutex m_eraseLock;
		tbb::concurrent_map<std::string, FControllerPtr> mControllerMap;
		std::vector<PathOrderQueue> mControllerOrderQueue;
		std::unique_ptr<std::mutex[]> mControllerOrderQueueEraseLocks;
	};

	void FRouter::RegisterController(const std::string& str, FControllerPtr controller)
	{
		if (!FRouter::valid())
			new FRouter();
		auto router = FRouter::instance();
		router->regController(str, controller);
	}

	void FRouter::RegisterControllerFuncs(const std::string& pathPattern, Method mapMethod, const std::string& controllerName, FControllerFunc func)
	{
		if (!FRouter::valid())
			new FRouter();
		auto router = FRouter::instance();
		router->regControllerFunc(pathPattern, mapMethod, controllerName, func);
	}
	void FRouter::UnRegisterController(const std::string& controllerName)
	{
		if (!FRouter::valid()) {
			Logger::instance()->log("FRouter", lvl::err, "Invalid operation, no Router.");
			throw std::exception("Invalid operation");
		}
		FRouter::instance()->unregController(controllerName);
	}

	FRouter::FRouter():_dp(new __FRouterInner)
	{
	}

	FRouter::RouteResult FRouter::route(Method method, const std::string& path)
	{
		RouteResult res{};
		{
			auto& controllers = _dp->mControllerOrderQueue[(uint32)method];
			for (auto&& [key,val] : controllers) {
				if (val->PathMatcher->isMatch(path, res.pathVariable)) {
					Logger::instance()->log("FRouter", lvl::trace, "{} match path pattern {}",path, val->PathMatcher->getOriginPattern());
					res.controllerFunc = val->ControllerFunc;
					res.controllerSave = val->ControllerBase;
					return res;
				}
			}
		}
		return res;
	}
	void FRouter::regController(const std::string& controllerName, FControllerPtr controller)
	{
		assert(controller != nullptr);
		if (_dp->mControllerMap.find(controllerName) != _dp->mControllerMap.end()) {
			Logger::instance()->log("FRouter", lvl::err, "Duplicated Controller Name");
			throw std::exception("Duplicated Controller Name");
		}
		_dp->mControllerMap.insert({ controllerName ,controller });
		Logger::instance()->log("FRouter", lvl::trace, "Register Controller {}", controllerName);

	}
	void FRouter::regControllerFunc(const std::string& pathPattern, Method mapMethod, const std::string& controllerName, const FControllerFunc& func)
	{
		FPathMatcher* matcher = new FPathMatcher(pathPattern, true);
		uint64 priority = calcPathPatternPriority(matcher);
		FControllerPtr controller = nullptr;
		auto itor = _dp->mControllerMap.find(controllerName);
		if (itor == _dp->mControllerMap.end()) {
			Logger::instance()->log("FRouter", lvl::err, "Unknown Controller Name");
			throw std::exception("Unknown Controller Name");
		}

		controller = itor->second;

		ControllerAndPatternPtr _temp = std::make_shared<__ControllerAndPattern>();
		
		_temp->priority = priority;
		_temp->requestMethod = mapMethod;
		_temp->PathMatcher = matcher;
		_temp->ControllerFunc = std::move(func);
		_temp->ControllerBase = controller;

		assert(mapMethod < Method::MAX_SIZE);
		{
			auto& orderQueue = _dp->mControllerOrderQueue[(uint32)mapMethod];
			orderQueue.insert({priority ,std::move(_temp) });
		}
		Logger::instance()->log("FRouter", lvl::trace, "Register {} Mehtod path pattern: {}", methodToStr(mapMethod), pathPattern);
	}
	uint64 FRouter::calcPathPatternPriority(FPathMatcher* matcher)
	{
		uint64 priority = MaxPathLengthMatcherSupport;

		priority = priority - matcher->getOriginPattern().size();

		priority = priority + matcher->getUndecidedCharNums() * 100ull;

		priority = priority + matcher->getWildCardsNums() * 10000ull;

		return priority;
	}

	void FRouter::unregController(const std::string& controllerName)
	{
		FControllerPtr controllerPtr = 0;
		
			auto itor = _dp->mControllerMap.find(controllerName);
			if (itor == _dp->mControllerMap.end()) {
				return;
			}
			controllerPtr = itor->second;
			Logger::instance()->log("FRouter", lvl::trace, "Remove Controller {}", controllerName);
			auto& eraseLock = _dp->m_eraseLock;
			{
				FAUTO_LOCK(eraseLock);
				_dp->mControllerMap.unsafe_erase(itor);
			}
		
		for (auto queueIdx = 0; queueIdx < _dp->mControllerOrderQueue.size(); ++queueIdx) {
			auto& queue = _dp->mControllerOrderQueue[queueIdx];
			auto& eraselock = _dp->mControllerOrderQueueEraseLocks[queueIdx];
				for (auto i = queue.begin(); i != queue.end();) {
					auto controllerAndPatternPtr = i->second;
					if (controllerAndPatternPtr->ControllerBase == controllerPtr) {
						FAUTO_LOCK(eraselock);
						i = queue.unsafe_erase(i);
						Logger::instance()->log("FRouter", lvl::trace, "Remove {} Mehtod path pattern: {}", methodToStr(i->second->requestMethod), i->second->PathMatcher->getOriginPattern());
					}
					else {
						i++;
					}
				}

		}
	}
}