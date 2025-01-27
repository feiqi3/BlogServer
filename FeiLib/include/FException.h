#ifndef FEXCEPTION_H
#define FEXCEPTION_H
#include<string>
#include <exception>

namespace Fei {

	class FException : public std::exception {
	public:
		FException();

		const char* what() const final override;
		
		const std::string& stackTrace()const { return mStackTace; }
		virtual std::string reason()const { return""; }
	private:
		std::string mStackTace;
		mutable std::string mErrorStr;
	};

};

#endif