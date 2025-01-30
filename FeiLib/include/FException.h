#ifndef FEXCEPTION_H
#define FEXCEPTION_H
#include "FDef.h"
#include<string>
#include <exception>

namespace Fei {
	std::string F_API getStackTrace(uint32 skip);
	
	class FException : public std::exception {
	public:
		FException():mStackTace(getStackTrace(1)){
		
		}

		inline const char* what() const noexcept final override
		{
			mErrorStr = reason() + std::string("\n") + mErrorStr;
			return mErrorStr.c_str();
		}
		
		inline const std::string& stackTrace()const { return mStackTace; }
		inline virtual std::string reason()const { return""; }
	private:
		std::string mStackTace;
		mutable std::string mErrorStr;
	};

};

#endif