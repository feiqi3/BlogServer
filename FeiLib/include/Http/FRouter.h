#ifndef FROUTER_H
#define FROUTER_H

#include <string>
namespace Fei::Http {
class FRouter {
public:
  void route(const std::string &path);
};
}; // namespace Fei::Http

#endif