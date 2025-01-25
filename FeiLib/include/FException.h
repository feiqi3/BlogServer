#ifndef FEXCEPTION_H
#define FEXCEPTION_H
#include<string>
#include <exception>

namespace Fei {

	class FException : public std::exception {
	public:
		FException();

		const char* what() const override;
		
		const std::string& stackTrace()const { return mStackTace; }

	private:
		std::string mStackTace;
	};

};

#endif