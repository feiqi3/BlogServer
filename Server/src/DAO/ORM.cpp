#include "ORM.h"
#include <vector>

namespace Blog {
template <class T> std::vector<T> Query<T>::getVector() const {
  std::vector<T> ret;
  while (result->step()) {
    ret.push_back(ToEntity());
  }
  return ret;
}

} // namespace Blog