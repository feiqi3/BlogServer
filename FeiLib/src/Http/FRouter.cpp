#include "Http/FRouter.h"
#include "Http/FPathMatcher.h"
#include "FLogger.h"
#include "tbb/concurrent_priority_queue.h"
#include "tbb/concurrent_map.h"
#include "FException.h"
namespace Fei::Http {
	namespace {
	
		class RouterDuplicateRegisterException :public FException {
		public:
			RouterDuplicateRegisterException(const std::string& name) :mName(name) {}
			std::string reason()const override {
				return "Register duplicated controller name: " + mName;
			}
		private:
			std::string mName;
		};

		class RouterInvalidControllerNameException :public FException {
		public:
			RouterInvalidControllerNameException(const std::string& name) :mName(name) {}
			std::string reason()const override {
				return "Unknown controller name: " + mName;
			}
		private:
			std::string mName;
		};
	};


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

	struct __DoHandle {
		__DoHandle(std::atomic_bool& in) :f(in) {
			in = true;
		}
		~__DoHandle() {
			f = false;
		}
		std::atomic_bool& f;
	};

	class __FRouterInner {
	public:

		__FRouterInner() {
			mControllerOrderQueue.resize((uint32)Method::MAX_SIZE);
			mRouteCaches.resize((uint32)Method::MAX_SIZE);
			mControllerOrderQueueEraseLocks.reset(new std::mutex[(uint32)Method::MAX_SIZE]);
		}
		

		std::mutex m_eraseLock;
		std::mutex m_routeCacheEraseLock;
		tbb::concurrent_map<std::string, FControllerPtr> mControllerMap;
		
		struct RouteCahce {
			uint64 cacheTime;
			FRouter::RouteResult result;
		};
		using RouteCacheMap = tbb::concurrent_map<std::string, RouteCahce>;
		std::vector<RouteCacheMap> mRouteCaches;

		using PathOrderQueue = tbb::concurrent_map<uint64, ControllerAndPatternPtr, __ControllerAndPatternCompare>;
		std::vector<PathOrderQueue> mControllerOrderQueue;
		std::unique_ptr<std::mutex[]> mControllerOrderQueueEraseLocks;

		std::atomic_bool isHandleRoute = false;
		std::atomic_bool isHandleUnreg = false;
	
	public:
		void clearCache() {
			FAUTO_LOCK(m_routeCacheEraseLock);
			for (auto&& i : mRouteCaches) {
				i.clear();
			}
		}
		
		void putCache(const std::string& str,Method method, const FRouter::RouteResult& in) {
			uint64 timeNow = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			mRouteCaches[(uint32)method].insert({str, RouteCahce{.cacheTime = timeNow, .result = in}});
		}

		void invalidCache(const std::string& str,Method method) {
			auto& caches = mRouteCaches[(int)method];
			auto itor = caches.find(str);
			if (itor == caches.end()) {
				return;
			}
			FAUTO_LOCK(m_routeCacheEraseLock);
			caches.unsafe_erase(itor);
		}

		void checkCacheOverdue(uint64 msDue) {
			uint64 timeNow = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			for (int i = 0; i < (int)Method::MAX_SIZE; ++i) {
				auto& caches = mRouteCaches[i];
				for (auto itor = caches.begin(); itor != caches.end();) {
					if (timeNow - itor->second.cacheTime > msDue) {
						FAUTO_LOCK(m_routeCacheEraseLock);
						itor = caches.unsafe_erase(itor);
					}
				}
			}
		}

		bool getRouteInCache(const std::string& str, Method method, FRouter::RouteResult& out) {
			auto& caches = mRouteCaches[(int)method];
			auto itor = caches.find(str);
			if (itor == caches.end()) {
				return false;
			}
			uint64 timeNow = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			itor->second.cacheTime = timeNow;
			out = itor->second.result;
			return true;
		}
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
		if(FRouter::valid())
			FRouter::instance()->unregController(controllerName);
	}

	FRouter::FRouter():_dp(new __FRouterInner)
	{
	}

	FRouter::RouteResult FRouter::route(Method method, const std::string& path)
	{
		RouteResult res{};
		//Spin wait.
		while (_dp->isHandleUnreg);
		//----------//

		{
			__DoHandle handle(_dp->isHandleRoute);
			if (_dp->getRouteInCache(path, method, res)) {
				Logger::instance()->log("FRouter", lvl::trace, "{} with method: {} Find in cache.", path,methodToStr(method));
				return res;
			}

			auto& controllers = _dp->mControllerOrderQueue[(uint32)method];
			for (auto&& [key,val] : controllers) {
				if (val->PathMatcher->isMatch(path, res.pathVariable)) {
					Logger::instance()->log("FRouter", lvl::trace, "{} match path pattern {}",path, val->PathMatcher->getOriginPattern());
					res.controllerFunc = val->ControllerFunc;
					res.controllerSave = val->ControllerBase;
					_dp->putCache(path, method, res);
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
			throw RouterDuplicateRegisterException(controllerName);
		}
		_dp->mControllerMap.insert({ controllerName ,controller });
		if (Logger::valid())
			Logger::instance()->log("FRouter", lvl::trace, "Register Controller {}", controllerName);

	}

	void FRouter::regControllerFunc(const std::string& pathPattern, Method mapMethod, const std::string& controllerName, FControllerFunc& func)
	{
		FPathMatcher* matcher = new FPathMatcher(pathPattern, true);
		uint64 priority = calcPathPatternPriority(matcher);
		FControllerPtr controller = nullptr;
		{
			auto itor = _dp->mControllerMap.find(controllerName);
			if (itor == _dp->mControllerMap.end()) {
				Logger::instance()->log("FRouter", lvl::err, "Unknown Controller Name");
				throw RouterInvalidControllerNameException(controllerName);
			}

			controller = itor->second;
		}
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
		if(Logger::valid() )
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
		__DoHandle handle(_dp->isHandleUnreg);
		//Spin wait.
		while (_dp->isHandleRoute);

		_dp->clearCache();

		FControllerPtr controllerPtr = 0;
		{
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
		}

		for (auto queueIdx = 0; queueIdx < _dp->mControllerOrderQueue.size(); ++queueIdx) {
			auto& queue = _dp->mControllerOrderQueue[queueIdx];
			auto& eraselock = _dp->mControllerOrderQueueEraseLocks[queueIdx];
				for (auto i = queue.begin(); i != queue.end();) {
					auto controllerAndPatternPtr = i->second;
					if (controllerAndPatternPtr->ControllerBase == controllerPtr) {
						FAUTO_LOCK(eraselock);
						Logger::instance()->log("FRouter", lvl::trace, "Remove {} Mehtod path pattern: {}", methodToStr(i->second->requestMethod), i->second->PathMatcher->getOriginPattern());
						i = queue.unsafe_erase(i);
					}
					else {
						i++;
					}
				}

		}
	}

}