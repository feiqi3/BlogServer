#pragma once
#include "FException.h"
namespace Blog {

	class RuntimeError : public Fei::FException
	{
	public:
		RuntimeError(const std::string& err):runtimeErr(err) {}
	private:
		std::string reason()const override{
			return runtimeErr;
		}
	private:
		std::string runtimeErr;
	};

}