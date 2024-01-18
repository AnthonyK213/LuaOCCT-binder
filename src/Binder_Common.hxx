#ifndef _LuaOCCT_Binder_Common_HeaderFile
#define _LuaOCCT_Binder_Common_HeaderFile

#include <map>
#include <set>
#include <string>
#include <unordered_map>

namespace binder {

static const std::unordered_map<std::string, std::string> LUA_OPERATORS{
    {"operator+", "__add"}, {"operator-", "__sub"}, {"operator*", "__mul"},
    {"operator/", "__div"}, {"operator==", "__eq"},
};

static const std::set<std::string> METHOD_BLACKLIST{"DumpJson", "get_type_name",
                                                    "get_type_descriptor"};

static const std::set<std::string> IMMUTABLE_TYPE{
    "Standard_Boolean", "Standard_CString",   "Standard_Integer",
    "Standard_Real",    "NCollection_Array1", "NCollection_Array2",
    "NCollection_List", "NCollection_Map",    "handle"};

static const std::unordered_map<std::string, std::string> EXTRA_METHODS{
    {"gp_XYZ",
     R"===(.addFunction("__tostring",+[](const gp_XYZ &theSelf){ std::ostringstream oss{};oss << "gp_XYZ{" << theSelf.X() << ',' << theSelf.Y() << ',' << theSelf.Z() << '}';return oss.str(); }))==="},
    {"gp_Pnt",
     R"===(.addFunction("__unm",+[](const gp_Pnt &theSelf){ return gp_Pnt(-theSelf.X(), -theSelf.Y(), -theSelf.Z()); })
.addFunction("__tostring",+[](const gp_Pnt &theSelf){ std::ostringstream oss{};oss << "gp_Pnt{" << theSelf.X() << ',' << theSelf.Y() << ',' << theSelf.Z() << '}'; return oss.str(); }))==="},
    {"gp_Dir",
     R"===(.addFunction("__tostring",+[](const gp_Dir &theSelf){ std::ostringstream oss{};oss << "gp_Dir{" << theSelf.X() << ',' << theSelf.Y() << ',' << theSelf.Z() << '}';return oss.str(); }))==="},
    {"gp_Vec",
     R"===(.addFunction("__tostring",+[](const gp_Vec &theSelf){ std::ostringstream oss{};oss << "gp_Vec{" << theSelf.X() << ',' << theSelf.Y() << ',' << theSelf.Z() << '}';return oss.str(); }))==="},
    {"gp_Quaternion",
     R"===(.addFunction("__tostring",+[](const gp_Quaternion &theSelf){ std::ostringstream oss{};oss << "gp_Quaternion{" << theSelf.X() << ',' << theSelf.Y() << ',' << theSelf.Z() << ',' << theSelf.W() << '}';return oss.str(); }))==="},
};

static const std::unordered_map<std::string, std::string> MANUAL_METHODS{
    {"Bnd_OBB::ReBuild",
     R"===(+[](Bnd_OBB &__theSelf__,const TColgp_Array1OfPnt &theListOfPoints,const Standard_Boolean theIsOptimal){ __theSelf__.ReBuild(theListOfPoints, nullptr, theIsOptimal); },+[](Bnd_OBB &__theSelf__,const TColgp_Array1OfPnt &theListOfPoints,const TColStd_Array1OfReal &theListOfTolerance,const Standard_Boolean theIsOptimal){ __theSelf__.ReBuild(theListOfPoints,&theListOfTolerance,theIsOptimal); })==="},
};

} // namespace binder

#endif
