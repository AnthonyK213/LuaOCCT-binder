#ifndef _LuaOCCT_Binder_Common_HeaderFile
#define _LuaOCCT_Binder_Common_HeaderFile

#include <map>
#include <set>
#include <string>

namespace binder {

static const std::map<std::string, std::string> LUA_OPERATORS{
    {"operator+", "__add"}, {"operator-", "__sub"}, {"operator*", "__mul"},
    {"operator/", "__div"}, {"operator==", "__eq"},
};

static const std::set<std::string> METHOD_BLACKLIST{"DumpJson", "get_type_name",
                                                    "get_type_descriptor"};

static const std::set<std::string> IMMUTABLE_TYPE{
    "Standard_Boolean", "Standard_CString",   "Standard_Integer",
    "Standard_Real",    "NCollection_Array1", "NCollection_Array2"};

} // namespace binder

#endif
