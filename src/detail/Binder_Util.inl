#include <sstream>
#include <string>
#include <vector>

template <typename Iter_, typename Fn_>
std::string Binder_Util_Join(Iter_ theFirst, Iter_ theLast, Fn_ theFn,
                             const std::string &theSep) {
  if (theFirst == theLast)
    return "";

  std::ostringstream oss{};
  oss << theFn(*theFirst);

  for (Iter_ anIter = ++theFirst; anIter != theLast; ++anIter) {
    oss << theSep << theFn(*anIter);
  }

  return oss.str();
}

template <typename C, typename T>
bool Binder_Util_Contains(C &&theContainer, T &&theElem) {
  return theContainer.find(theElem) != theContainer.end();
}
