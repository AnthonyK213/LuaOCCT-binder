#ifndef _LuaOCCT_Binder_Util_HeaderFile
#define _LuaOCCT_Binder_Util_HeaderFile

#include <clang-c/Index.h>

#include <sstream>
#include <string>

std::string Binder_Util_GetCString(const CXString &theStr);

std::string Binder_Util_GetCString(CXString &&theStr);

inline bool Binder_Util_StartsWith(const std::string &theStr,
                                   const std::string &thePrefix) {

  return theStr.rfind(thePrefix, 0) == 0;
}

inline bool Binder_Util_StrContains(const std::string &theStr,
                                    const std::string &theSub) {
  return theStr.find(theSub) != std::string::npos;
}

template <typename Iter_, typename Fn_>
std::string Binder_Util_Join(Iter_ theFirst, Iter_ theLast, Fn_ theFn,
                             const std::string &theSep = ",");

template <typename C, typename T>
bool Binder_Util_Contains(C &&theContainer, T &&theElem);

#include "detail/Binder_Util.inl"

#endif
