#ifndef FEXCEPTION_H
#define FEXCEPTION_H
#include "FDef.h"
#include<string>
#include <exception>
#include <vector>

namespace Fei {
	std::vector<std::string> F_API getStackTrace(uint32 skip);
	
	class FException : public std::exception {
	public:
		FException():mStackTace(getStackTrace(1)){
		
		}

		inline const char* what() const noexcept final override
		{
			std::string stack = "Calling stack:\n";
			for(auto&& str: mStackTace){
				stack += str + "\n";				
			}
			mErrorStr = reason() + std::string("\n") + stack;
			return mErrorStr.c_str();
		}
		
		inline const std::vector<std::string>& stackTrace()const { return mStackTace; }
		inline virtual std::string reason()const { return""; }
	private:
		std::vector<std::string> mStackTace;
		mutable std::string mErrorStr;
	};

};

#endif