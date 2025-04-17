#include "Http/FRouter.h"
#include "FConfigReader.h"
#include "FDef.h"
#include "Http/FPathMatcher.h"
#include "FLogger.h"
#include "tbb/concurrent_map.h"
#include "FException.h"
#include <cassert>
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include "FConcurrentMap.h"

#define CACHE_CLEAR_CACHE_UNUSED_TIME 1000 * 60 * 60 * 1 //ms --> 1h

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
			mRouteCaches = std::make_unique<RouteCacheMap[]>
			 ((uint32)Method::MAX_SIZE);
			mControllerOrderQueueEraseLocks.reset(new std::mutex[(uint32)Method::MAX_SIZE]);
		}
		

		std::mutex m_eraseLock;
		std::mutex m_routeCacheEraseLock;
		FConcurrentHashMap<std::string, FControllerPtr> mControllerMap;
		struct RouteCahce {
			uint64 cacheTime;
			FRouter::RouteResult result;
		};
		using RouteCacheMap = FConcurrentHashMap<std::string, RouteCahce>;
		std::unique_ptr<RouteCacheMap[]> mRouteCaches;

		using PathOrderQueue = tbb::concurrent_map<uint64, ControllerAndPatternPtr, __ControllerAndPatternCompare>;
		std::vector<PathOrderQueue> mControllerOrderQueue;
		std::unique_ptr<std::mutex[]> mControllerOrderQueueEraseLocks;

		std::atomic_bool isHandleRoute = false;
		std::atomic_bool isHandleUnreg = false;
		std::atomic_bool hasLateInit = false;

	public:
		void clearCache() {
			for(int i = 0; i < (int)Method::MAX_SIZE; ++i){
				auto& caches = mRouteCaches[i];
				caches.clear();
			}
		}
		
		void putCache(const std::string& str,Method method, const FRouter::RouteResult& in) {
			uint64 timeNow = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			assert( (method < Method::MAX_SIZE));
			mRouteCaches[(uint32)method].insert(str, RouteCahce{.cacheTime = timeNow, .result = in});
		}

		void invalidCache(const std::string& str,Method method) {
			assert( (method < Method::MAX_SIZE));
			auto& caches = mRouteCaches[(uint32)method];

			if(caches.find(str) == false){
				return;
			}

			Logger::instance()->log(lvl::info, "Try erase invalid cache {} with method.", str,methodToStr(method));
			caches.erase(str);
		}

		void checkCacheOverdue(uint64 msDue) {
			uint64 timeNow = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			for (int i = 0; i < (int)Method::MAX_SIZE; ++i) {
				auto& caches = mRouteCaches[i];
				std::vector<std::string> toErase;
				caches.traversal([this, timeNow, msDue, &toErase](const std::string& key, const RouteCahce& cache) {
					if (timeNow - cache.cacheTime > msDue) {
						toErase.push_back(key);
					}
				});

				for (auto&& i : toErase) {
					caches.erase(i);
				}
			}
		}

		bool getRouteInCache(const std::string& str, Method method, FRouter::RouteResult& out) {
			assert( (method < Method::MAX_SIZE));
			auto& caches = mRouteCaches[(int)method];
			if(caches.find(str) == false){
				return false;
			}

			uint64 timeNow = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			caches.findAndModifyLocked(str, [&](auto& in){
				in.cacheTime = timeNow;
				out = in.result;
			});
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
	void FRouter::lateInit(){
		_dp->mControllerMap.traversal([this](const std::string& key, FControllerPtr controller) {
			controller->lateInit();
		});

		this->_dp->hasLateInit = true;
		if(FConfigReader::instance()->getCurrentEnv() == FConfigReader::Env::Test){
				Logger::instance()->log("FRouter", lvl::trace, "Registered Controller Functions:");
				int queueMEthod = 0;
			for(auto && queue :this->_dp->mControllerOrderQueue){
				Logger::instance()->log("FRouter", lvl::trace, "Queue Method: {}",queueMEthod);
				for(auto && controllerFunc : queue){
					auto controllerAndPattern = controllerFunc.second;
					Logger::instance()->log("FRouter", lvl::trace, "Controller: {}, Pattern: {}, Priority: {}",controllerAndPattern->ControllerBase->getControllerName(),controllerAndPattern->PathMatcher->getOriginPattern(),controllerAndPattern->priority);
				}
				queueMEthod++;
			}
		}
	}

	FRouter::FRouter():_dp(new __FRouterInner)
	{
	}

	void FRouter::checkRouteCache(uint64 timeNow){
		static uint64 lastCheckTime  = 0;
		if(timeNow - lastCheckTime <  CACHE_CLEAR_CACHE_UNUSED_TIME  >> 2){
			return;
		}
		lastCheckTime = timeNow;
		_dp->checkCacheOverdue(CACHE_CLEAR_CACHE_UNUSED_TIME);
	}

	FRouter::RouteResult FRouter::route(Method method, const std::string& path)
	{
		RouteResult res{};
		if(method >= Method::MAX_SIZE){
			Logger::instance()->log("FRouter", lvl::err, "Invalid method: {}", methodToStr(method));
			return res;
		}
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
		if (_dp->mControllerMap.find(controllerName)) {
			throw RouterDuplicateRegisterException(controllerName);
		}
		_dp->mControllerMap.insert( controllerName ,controller );
		if (Logger::valid())
			Logger::instance()->log("FRouter", lvl::trace, "Register Controller {}", controllerName);
		
			if(_dp->hasLateInit){
			controller->lateInit();
		}
	}

	void FRouter::regControllerFunc(const std::string& pathPattern, Method mapMethod, const std::string& controllerName, FControllerFunc& func)
	{
		FPathMatcher* matcher = new FPathMatcher(pathPattern, true);
		uint64 priority = calcPathPatternPriority(matcher);
		FControllerPtr controller = nullptr;
		{
			bool hasFind = _dp->mControllerMap.find(controllerName,controller);
			if (!hasFind) {
				Logger::instance()->log("FRouter", lvl::err, "Unknown Controller Name");
				throw RouterInvalidControllerNameException(controllerName);
			}
			assert(controller != nullptr);
		}
		ControllerAndPatternPtr _temp = std::make_shared<__ControllerAndPattern>();
		
		_temp->priority = priority;
		_temp->requestMethod = mapMethod;
		_temp->PathMatcher = matcher;
		_temp->ControllerFunc = (func);
		_temp->ControllerBase = controller;

		assert(mapMethod < Method::MAX_SIZE);
		{
			auto& orderQueue = _dp->mControllerOrderQueue[(uint32)mapMethod];
			while(orderQueue.find(priority) != orderQueue.end()){
				++priority;
			}
			_temp->priority = priority;
			orderQueue.insert({priority ,(_temp) });
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
		//Lock until other unreg finished
		while(_dp->isHandleUnreg);
		
		__DoHandle handle(_dp->isHandleUnreg);
		//Spin wait.
		while (_dp->isHandleRoute);

		_dp->clearCache();

		FControllerPtr controllerPtr = 0;
		{
			bool hasFind = _dp->mControllerMap.find(controllerName, controllerPtr);
			if (!hasFind) {
				return;
			}
			assert(controllerPtr != nullptr);
			Logger::instance()->log("FRouter", lvl::trace, "Remove Controller {}", controllerName);
			{
				Logger::instance()->log("FRouter", lvl::trace, "Remove {}", controllerPtr->getControllerName());
				_dp->mControllerMap.erase(controllerName);
			}
		}

		for (auto queueIdx = 0; queueIdx < _dp->mControllerOrderQueue.size(); ++queueIdx) {
			auto& queue = _dp->mControllerOrderQueue[queueIdx];
			auto& eraselock = _dp->mControllerOrderQueueEraseLocks[queueIdx];
				for (auto i = queue.begin(); i != queue.end();) {
					auto controllerAndPatternPtr = i->second;
					if (controllerAndPatternPtr->ControllerBase == controllerPtr) {
						//TODO: erase
						Logger::instance()->log("FRouter", lvl::trace, "Remove {} Mehtod path pattern: {}, but now it is not implemented yet", methodToStr(i->second->requestMethod), i->second->PathMatcher->getOriginPattern());
					}
					else {
						i++;
					}
				}

		}
	}

}