#include "Binder_Module.hxx"
#include "Binder_Generator.hxx"
#include "Binder_Util.hxx"

#include <algorithm>
#include <iterator>
#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

extern Binder_Config binder_config;

static std::string
normalizedTypeSpelling(const std::string &theTypeName,
                       const Binder_Module::CursorInfo &theInfo);

static std::string
normalizedTypeSpelling(const Binder_Type &theType,
                       const Binder_Module::CursorInfo &theInfo);

static std::string luaTypeMap(const Binder_Type &theType,
                              const Binder_Module::CursorInfo &theInfo) {
  Binder_Type aType = theType.IsPointerLike() ? theType.GetPointee() : theType;
  Binder_Cursor aDecl = aType.GetDeclaration();

  if (aDecl.IsNull()) {
    std::string aTypeSpelling = aType.Spelling();
    std::regex removeConst("const\\s+");
    std::string trimmed = std::regex_replace(aTypeSpelling, removeConst, "");
    return normalizedTypeSpelling(trimmed, theInfo);
  }

  std::string aDeclSpelling = aDecl.Spelling();

  static std::unordered_map<std::string, std::string> aMap{
      {"int", "integer"},
      {"long", "integer"},
      {"double", "number"},
      {"bool", "boolean"},
      {"char *", "string"},
      {"Standard_Integer", "integer"},
      {"Standard_Size", "integer"},
      {"Standard_Real", "number"},
      {"Standard_Boolean", "boolean"},
      {"Standard_CString", "string"},
      {"TCollection_AsciiString", "string"},
      {"TCollection_ExtendedString", "string"},
  };

  if (Binder_Util_Contains(aMap, aDeclSpelling)) {
    return aMap[aDeclSpelling];
  }

  if (aDeclSpelling == "handle") {
    Binder_Type aSpecType = aType.GetTemplateArgumentAsType(0);
    return luaTypeMap(aSpecType, theInfo);
  }

  static const std::set<std::string> IS_ARRAY1{
      "NCollection_Array1",
      "NCollection_List",
      "NCollection_Sequence",
      "vector",
  };

  Binder_Type aTp = aDecl.UnderlyingTypedefType();
  if (!aTp.IsNull()) {
    Binder_Cursor aD = aTp.GetDeclaration();
    std::string aTmplSpelling = aD.Spelling();
    if (Binder_Util_Contains(IS_ARRAY1, aTmplSpelling)) {
      Binder_Type aTmplArgType = aTp.GetTemplateArgumentAsType(0);
      return luaTypeMap(aTmplArgType, theInfo) + "[]";
    } else if (aTmplSpelling == "NCollection_Array2") {
      Binder_Type aTmplArgType = aTp.GetTemplateArgumentAsType(0);
      return luaTypeMap(aTmplArgType, theInfo) + "[][]";
    }
  }

  return aDeclSpelling;
}

static std::string normalizedTypeName(const std::string &theName) {
  std::regex evilComma("(,\\s*>)");
  return std::regex_replace(theName, evilComma, ">");
}

static std::string
normalizedTypeSpelling(const std::string &theTypeName,
                       const Binder_Module::CursorInfo &theInfo) {
  if (!theInfo.isTemplate)
    return theTypeName;

  std::ostringstream result{};
  std::ostringstream buffer{};
  for (const char &c : theTypeName) {
    if (isalpha(c) || isdigit(c) || c == '_') {
      buffer << c;
    } else {
      std::string segment = buffer.str();
      auto iter = theInfo.argMap.find(segment);
      if (iter != theInfo.argMap.end()) {
        result << iter->second;
      } else {
        result << segment;
      }
      result << c;
      buffer.str("");
    }
  }

  std::string segment = buffer.str();
  if (!segment.empty()) {
    auto iter = theInfo.argMap.find(segment);
    if (iter != theInfo.argMap.end()) {
      result << iter->second;
    } else {
      result << segment;
    }
  }

  return normalizedTypeName(result.str());
}

static std::string
normalizedTypeSpelling(const Binder_Type &theType,
                       const Binder_Module::CursorInfo &theInfo) {
  return normalizedTypeSpelling(theType.Spelling(), theInfo);
}

static std::unordered_map<std::string, std::string>
getTemplateInstanceArgMap(const Binder_Cursor &theCursor) {
  Binder_Type aType = theCursor.UnderlyingTypedefType();
  int num = aType.GetNumTempalteArguments();
  Binder_Cursor aTemplate = aType.GetDeclaration().GetSpecialization();
  std::string spelling = aTemplate.DisplayName();

  std::unordered_map<std::string, std::string> aMap{};
  std::ostringstream buffer{};
  bool found = false;
  int i = 0;
  for (const char &c : spelling) {
    if (found) {
      if (isalpha(c) || isdigit(c) || c == '_') {
        buffer << c;
      } else {
        std::string key = buffer.str();

        if (key.empty())
          continue;

        aMap.insert(
            std::make_pair(key, aType.GetTemplateArgumentAsType(i).Spelling()));
        i++;
        buffer.str("");

        if (c == '>') {
          break;
        }
      }
    } else if (c == '<') {
      found = true;
    }
  }

  return aMap;
}

Binder_Module::Binder_Module(const std::string &theName,
                             Binder_Generator &theParent)
    : myName(theName), myParent(&theParent), myIndex(nullptr),
      myTransUnit(nullptr) {
  myExportDir = myParent->ExportDir();
  myMetaExportDir = myParent->ExportDir() + "/_meta/";
}

Binder_Module::~Binder_Module() { dispose(); }

bool Binder_Module::Parse() {
  dispose();

  myIndex = clang_createIndex(0, 0);

  std::string aHeader = myParent->ModDir() + "/" + myName + ".h";

  std::vector<const char *> aClangArgs{};
  std::transform(
      myParent->ClangArgs().cbegin(), myParent->ClangArgs().cend(),
      std::back_inserter(aClangArgs),
      [](const std::string &theStr) -> const char * { return theStr.c_str(); });

  aClangArgs.push_back("-I");
  aClangArgs.push_back(myParent->OcctIncDir().c_str());

  for (const std::string &aStr : myParent->IncludeDirs()) {
    aClangArgs.push_back("-I");
    aClangArgs.push_back(aStr.c_str());
  }

  myTransUnit = clang_parseTranslationUnit(
      myIndex, aHeader.c_str(), aClangArgs.data(), aClangArgs.size(), nullptr,
      0, CXTranslationUnit_DetailedPreprocessingRecord);

  if (myTransUnit == nullptr) {
    std::cout << "Unable to parse translation unit.\n";
    return false;
  }

  return true;
}

static bool generateEnum(const Binder_Cursor &theEnum,
                         std::string &theEnumSpelling,
                         std::vector<Binder_Cursor> &theEnumConsts) {
  theEnumConsts = theEnum.EnumConsts();

  if (theEnumConsts.empty())
    return false;

  theEnumSpelling = theEnum.Spelling();

  // FIXME: (unnamed enum at ...) ???
  if (theEnumSpelling.empty() ||
      Binder_Util_StrContains(theEnumSpelling, "unnamed enum"))
    return false;

  return true;
}

bool Binder_Module::generateEnumCast(const Binder_Cursor &theEnum) {
  std::string anEnumSpelling{};
  std::vector<Binder_Cursor> anEnumConsts{};

  if (!generateEnum(theEnum, anEnumSpelling, anEnumConsts))
    return false;

  std::cout << "Binding enum cast: " << anEnumSpelling << '\n';

  myEnumStream << "template<> struct luabridge::Stack<" << anEnumSpelling
               << "> : luabridge::Enum<" << anEnumSpelling << ','
               << Binder_Util_Join(anEnumConsts.cbegin(), anEnumConsts.cend(),
                                   [&](const Binder_Cursor &theEnumConst) {
                                     return anEnumSpelling +
                                            "::" + theEnumConst.Spelling();
                                   })
               << ">{};\n";

  return true;
}

bool Binder_Module::generateEnumValue(const Binder_Cursor &theEnum) {
  std::string anEnumSpelling{};
  std::vector<Binder_Cursor> anEnumConsts{};

  if (!generateEnum(theEnum, anEnumSpelling, anEnumConsts))
    return false;

  std::cout << "Binding enum: " << anEnumSpelling << '\n';

  mySourceStream << ".beginNamespace(\"" << anEnumSpelling << "\")\n";
  myMetaStream << "---@enum " << anEnumSpelling << '\n';
  myMetaStream << "LuaOCCT." << myName << '.' << anEnumSpelling << " = {\n";

  for (const auto anEnumConst : anEnumConsts) {
    std::string anEnumConstSpelling = anEnumConst.Spelling();
    mySourceStream << ".addProperty(\"" << anEnumConstSpelling
                   << "\",+[](){ return " << anEnumSpelling
                   << "::" << anEnumConstSpelling << "; })\n";
    myMetaStream << '\t' << anEnumConstSpelling << " = "
                 << clang_getEnumConstantDeclValue(anEnumConst) << ",\n";
  }

  mySourceStream << ".endNamespace()\n\n";
  myMetaStream << "}\n\n";

  return true;
}

bool Binder_Module::generateCtor(const Binder_Cursor &theClass,
                                 const CursorInfo &theInfo) {
  if (theClass.IsAbstract() || theClass.IsStaticClass()) {
    std::cout << "Skip ctor: " << theClass.Spelling()
              << " isStatic:" << theClass.IsStaticClass() << '\n';
    return true;
  }

  const std::string &aClassSpelling = theInfo.spelling;
  bool needsDefaultCtor = theClass.NeedsDefaultCtor();

  std::vector<Binder_Cursor> aCtorsb = theClass.Ctors(true);
  // Remove move ctor.
  std::vector<Binder_Cursor> aCtors{};
  std::copy_if(
      aCtorsb.cbegin(), aCtorsb.cend(), std::back_inserter(aCtors),
      +[](const Binder_Cursor &cursor) { return !cursor.IsMoveCtor(); });

  // if no public ctor but non-public, do not bind any ctor.
  if (aCtors.empty() && !needsDefaultCtor)
    return true;

  if (theClass.IsTransient()) {
    // Intrusive container is gooooooooooooooooooooooooood!
    mySourceStream << ".addConstructorFrom<opencascade::handle<"
                   << aClassSpelling << ">,";
  } else {
    mySourceStream << ".addConstructor<";
  }

  bool declCopyCtor = false;

  if (needsDefaultCtor) {
    mySourceStream << "void()";
    myMetaStream << "---@overload fun():" << aClassSpelling << '\n';
  } else {
    mySourceStream << Binder_Util_Join(
        aCtors.cbegin(), aCtors.cend(), [&](const Binder_Cursor &theCtor) {
          if (theCtor.IsCopyCtor())
            declCopyCtor = true;

          std::ostringstream oss{};
          oss << "void(";
          std::vector<Binder_Cursor> aParams = theCtor.Parameters();
          oss << Binder_Util_Join(aParams.cbegin(), aParams.cend(),
                                  [&](const Binder_Cursor &theParam) {
                                    //  return theParam.Type().Spelling();
                                    return normalizedTypeSpelling(
                                        theParam.Type(), theInfo);
                                  })
              << ')';

          myMetaStream << "---@overload fun("
                       << Binder_Util_Join(aParams.cbegin(), aParams.cend(),
                                           [&](const Binder_Cursor &theParam) {
                                             std::ostringstream o{};
                                             o << theParam.Spelling() << ':'
                                               << luaTypeMap(theParam.Type(),
                                                             theInfo);
                                             return o.str();
                                           })
                       << "):" << aClassSpelling << '\n';

          return oss.str();
        });
  }

  if (!declCopyCtor && theClass.IsCopyable()) {
    mySourceStream << ",void(const " << aClassSpelling << "&)";
    myMetaStream << "---@overload fun(theOther:" << aClassSpelling
                 << "):" << aClassSpelling << '\n';
  }

  mySourceStream << ">()\n";

  return true;
}

static bool isIgnoredMethod(const Binder_Cursor &theMethod) {
  if (theMethod.IsOverride() || !theMethod.IsPublic() ||
      theMethod.IsFunctionTemplate())
    return true;

  // IN/OUT parameter
  // if (theMethod.NeedsInOutMethod())
  //   return true;

  std::string aFuncSpelling = theMethod.Spelling();

  // FIXME: Poly_Trangulation::createNewEntity is public??????????
  // This is a workaround, since OCCT made a public cxxmethod's first
  // character capitalized.
  if (!std::isupper(aFuncSpelling.c_str()[0]) && !theMethod.IsOperator())
    return true;

  // Any one uses these methods?
  if (Binder_Util_Contains(binder_config.myBlackListMethodByName,
                           aFuncSpelling))
    return true;

  std::vector<Binder_Cursor> aParams = theMethod.Parameters();
  for (const Binder_Cursor &aParam : aParams) {
    if (aParam.Type().IsRvalue())
      return true;
  }

  return false;
}

std::string Binder_Module::generateMethod(const Binder_Cursor &theClass,
                                          const Binder_Cursor &theMethod,
                                          const std::string &theSuffix,
                                          const CursorInfo &theInfo,
                                          bool theIsOverload) {
  std::string aClassSpelling = theInfo.spelling;
  std::string aMethodSpelling = theMethod.Spelling();
  std::vector<Binder_Cursor> aParams = theMethod.Parameters();
  std::ostringstream oss{};

  std::string aFuncName = aClassSpelling + "::" + aMethodSpelling;

  if (Binder_Util_Contains(binder_config.myManualMethod, aFuncName)) {
    return binder_config.myManualMethod.at(aFuncName);
  }

  if (theMethod.IsOperator()) {
    oss << "+[](" << (theMethod.IsConstMethod() ? "const " : "")
        << aClassSpelling << " &theSelf";

    if (aMethodSpelling == "operator-") { /* __unm */
      if (aParams.empty()) {
        oss << "){ return -theSelf; }";
      } else { /* __sub */
        oss << ',' << normalizedTypeSpelling(aParams[0].Type(), theInfo)
            << " theOther){ return theSelf-theOther; }";
      }
    } else {
      if (aParams.empty())
        return "";

      oss << ',' << normalizedTypeSpelling(aParams[0].Type(), theInfo)
          << " theOther){ return theSelf" << aMethodSpelling.substr(8)
          << "theOther; }";
    }

    return oss.str();
  }

  bool genMeta = !theIsOverload;

  if (genMeta) {
    myMetaStream << "---\n";
  }

  std::string aF = "function LuaOCCT." + myName + '.' + aClassSpelling +
                   (theMethod.IsStaticMethod() ? "." : ":") + aMethodSpelling +
                   theSuffix;

  if (theMethod.NeedsInOutMethod()) {
    std::vector<Binder_Cursor> anIn{};
    std::vector<Binder_Cursor> anOut{};
    theMethod.GetInOutParams(anIn, anOut);
    bool anIsStatic = theMethod.IsStaticMethod();

    oss << "+[](";

    if (!anIsStatic) {
      if (theMethod.IsConstMethod())
        oss << "const ";

      oss << aClassSpelling << " &__theSelf__";

      if (!anIn.empty())
        oss << ',';
    }

    oss << Binder_Util_Join(
        anIn.cbegin(), anIn.cend(), [&](const Binder_Cursor &theParam) {
          return normalizedTypeSpelling(theParam.Type(), theInfo) + " " +
                 theParam.Spelling();
        });

    Binder_Type aRetType = theMethod.ReturnType();
    std::string aRetTypeSpelling =
        normalizedTypeSpelling(aRetType.Spelling(), theInfo);
    bool anHasRetVal = aRetTypeSpelling != "void";
    int nbReturn = (int)anHasRetVal + anOut.size();
    bool anTupleOut = nbReturn >= 2;

    if (anTupleOut) {
      oss << ")->std::tuple<";

      if (anHasRetVal)
        oss << aRetTypeSpelling << ',';

      oss << Binder_Util_Join(anOut.cbegin(), anOut.cend(),
                              [&](const Binder_Cursor &theParam) {
                                return normalizedTypeSpelling(
                                    theParam.Type().GetPointee(), theInfo);
                              })
          << "> { ";
    } else if (nbReturn == 1) {
      if (anOut.empty())
        oss << ") { ";
      else
        oss << ")->"
            << normalizedTypeSpelling(anOut[0].Type().GetPointee(), theInfo)
            << " { ";
    } else {
      oss << ") { ";
    }

    for (const auto &anOutParam : anOut) {
      oss << normalizedTypeSpelling(anOutParam.Type().GetPointee(), theInfo)
          << ' ' << anOutParam.Spelling() << "{};";
    }

    if (anHasRetVal) {
      oss << aRetTypeSpelling << " __theRet__=";
    }

    if (anIsStatic) {
      oss << aClassSpelling << "::";
    } else {
      oss << "__theSelf__.";
    }

    oss << aMethodSpelling << "("
        << Binder_Util_Join(
               aParams.cbegin(), aParams.cend(),
               +[](const Binder_Cursor &theParam) {
                 if (theParam.Type().IsPointer()) {
                   return "&" + theParam.Spelling();
                 }
                 return theParam.Spelling();
               })
        << ");";

    if (anTupleOut) {
      oss << "return {";

      if (anHasRetVal) {
        oss << "__theRet__,";
      }

      oss << Binder_Util_Join(
                 anOut.cbegin(), anOut.cend(),
                 +[](const Binder_Cursor &theParam) {
                   return theParam.Spelling();
                 })
          << "}; }";
    } else if (nbReturn == 1) {
      if (anOut.empty())
        oss << "return __theRet__; }";
      else
        oss << "return " << anOut[0].Spelling() << "; }";
    } else {
      oss << " }";
    }

    if (genMeta) {
      for (auto it = anIn.cbegin(); it != anIn.cend(); ++it) {
        myMetaStream << "---@param " << it->Spelling() << ' '
                     << luaTypeMap(it->Type(), theInfo) << '\n';
      }
      if (anTupleOut) {
        myMetaStream << "---@return {";
        int i = 1;
        if (anHasRetVal) {
          myMetaStream << '[' << i << "]:" << luaTypeMap(aRetType, theInfo)
                       << ',';
          ++i;
        }
        for (const auto &o : anOut) {
          myMetaStream << '[' << i << "]:" << luaTypeMap(o.Type(), theInfo)
                       << ',';
          ++i;
        }
        myMetaStream << '}' << '\n';
      } else if (nbReturn == 1) {
        myMetaStream << "---@return " << luaTypeMap(aRetType, theInfo) << '\n';
      }

      myMetaStream << aF << '('
                   << Binder_Util_Join(
                          anIn.cbegin(), anIn.cend(),
                          +[](const Binder_Cursor &theParam) {
                            return theParam.Spelling();
                          })
                   << ") end\n\n";
    }

    return oss.str();
  }

  if (theIsOverload) {
    oss << "luabridge::overload<";
    oss << Binder_Util_Join(
        aParams.cbegin(), aParams.cend(), [&](const Binder_Cursor &theParam) {
          return normalizedTypeSpelling(theParam.Type(), theInfo);
        });
    oss << ">(&" << aClassSpelling << "::" << aMethodSpelling << ')';
    myMetaStream << "---@overload fun(";
    if (!theMethod.IsStaticMethod()) {
      myMetaStream << "self";
      if (!aParams.empty())
        myMetaStream << ',';
    }
    myMetaStream << Binder_Util_Join(aParams.cbegin(), aParams.cend(),
                                     [&](const Binder_Cursor &theParam) {
                                       std::ostringstream o{};
                                       o << theParam.Spelling() << ':'
                                         << luaTypeMap(theParam.Type(),
                                                       theInfo);
                                       return o.str();
                                     })
                 << ")";
    Binder_Type aRetType = theMethod.ReturnType();
    if (!aRetType.IsNull() && aRetType.Spelling() != "void") {
      myMetaStream << ':' << luaTypeMap(aRetType, theInfo);
    }
    myMetaStream << '\n';
  } else {
    oss << '&' << aClassSpelling << "::" << aMethodSpelling;
    for (auto it = aParams.cbegin(); it != aParams.cend(); ++it) {
      myMetaStream << "---@param " << it->Spelling() << ' '
                   << luaTypeMap(it->Type(), theInfo) << '\n';
    }
    Binder_Type aRetType = theMethod.ReturnType();
    if (!aRetType.IsNull() && aRetType.Spelling() != "void") {
      myMetaStream << "---@return " << luaTypeMap(aRetType, theInfo) << '\n';
    }
    myMetaStream << aF << '('
                 << Binder_Util_Join(
                        aParams.cbegin(), aParams.cend(),
                        +[](const Binder_Cursor &theParam) {
                          return theParam.Spelling();
                        })
                 << ") end\n\n";
  }

  return oss.str();
}

struct Binder_MethodGroup {
public:
  void Add(const Binder_Cursor &theMethod) { myMethods.push_back(theMethod); }

  std::size_t Size() const { return myMethods.size(); };

  bool HasOverload() const { return Size() > 1; }

  std::vector<Binder_Cursor> Methods() const {
    std::vector<Binder_Cursor> aResult{};
    std::copy_if(
        myMethods.cbegin(), myMethods.cend(), std::back_inserter(aResult),
        [](const Binder_Cursor &theMethod) {
          return !isIgnoredMethod(theMethod) && !theMethod.IsStaticMethod();
        });

    return aResult;
  }

  std::vector<Binder_Cursor> StaticMethods() const {
    std::vector<Binder_Cursor> aResult{};
    std::copy_if(
        myMethods.cbegin(), myMethods.cend(), std::back_inserter(aResult),
        [](const Binder_Cursor &theMethod) {
          return !isIgnoredMethod(theMethod) && theMethod.IsStaticMethod();
        });

    return aResult;
  }

private:
  std::vector<Binder_Cursor> myMethods{};
};

bool Binder_Module::generateMethods(const Binder_Cursor &theClass,
                                    const CursorInfo &theInfo) {
  std::string aClassSpelling = theInfo.spelling;
  std::vector<Binder_Cursor> aMethods =
      theClass.GetChildrenOfKind(CXCursor_CXXMethod);

  std::map<std::string, Binder_MethodGroup> aGroups{};

  // Group cxxmethods by name.
  for (const auto &aMethod : aMethods) {
    std::string aFuncSpelling = aMethod.Spelling();

    std::string aFuncName = aClassSpelling + "::" + aFuncSpelling;
    if (Binder_Util_Contains(binder_config.myBlackListMethod, aFuncName))
      continue;

    bool aManual =
        Binder_Util_Contains(binder_config.myManualMethod, aFuncName);

    if (aMethod.IsOperator()) {
      if (aFuncSpelling == "operator-") {
        if (aMethod.Parameters().empty()) {
          aFuncSpelling = "__unm";
        } else {
          aFuncSpelling = "__sub";
        }
      } else {
        aFuncSpelling = binder_config.myLuaOperators.at(aFuncSpelling);
      }
    }

    if (Binder_Util_Contains(aGroups, aFuncSpelling) && !aManual) {
      aGroups[aFuncSpelling].Add(aMethod);
    } else {
      Binder_MethodGroup aGrp{};
      aGrp.Add(aMethod);
      aGroups.insert({aFuncSpelling, std::move(aGrp)});
    }
  }

  // Bind methods.
  for (auto anIter = aGroups.cbegin(); anIter != aGroups.cend(); ++anIter) {
    const Binder_MethodGroup &aMethodGroup = anIter->second;
    const std::vector<Binder_Cursor> aMtd = aMethodGroup.Methods();
    const std::vector<Binder_Cursor> aMtdSt = aMethodGroup.StaticMethods();
    const std::string aMethodMeta = "LuaOCCT." + myName + "." + aClassSpelling;
    const std::string &aMethodSpelling = anIter->first;

    if (!aMtd.empty()) {
      mySourceStream << ".addFunction(\"" << aMethodSpelling << "\",";

      if (aMethodGroup.HasOverload()) {
        myMetaStream << "---\n";
        // myMetaStream << "---@param ... any\n";
        mySourceStream << Binder_Util_Join(
            aMtd.cbegin(), aMtd.cend(),
            [&, this](const Binder_Cursor &theMethod) {
              return generateMethod(theClass, theMethod, "", theInfo, true);
            });
        myMetaStream << "function " << aMethodMeta << ':' << aMethodSpelling
                     << "(...) end\n\n";
      } else {
        mySourceStream << generateMethod(theClass, aMtd[0], "", theInfo);
      }

      mySourceStream << ")\n";
    }

    if (!aMtdSt.empty()) {
      std::string suffix =
          (aMtd.empty() ? ""
                        : "_"); /* Add a "_" if there is non-static overload. */
      mySourceStream << ".addStaticFunction(\"" << aMethodSpelling << suffix
                     << "\",";

      if (aMethodGroup.HasOverload()) {
        myMetaStream << "---\n";
        // myMetaStream << "---@param ... any\n";
        mySourceStream << Binder_Util_Join(
            aMtdSt.cbegin(), aMtdSt.cend(),
            [&, this](const Binder_Cursor &theMethod) {
              return generateMethod(theClass, theMethod, suffix, theInfo, true);
            });
        myMetaStream << "function " << aMethodMeta << '.' << aMethodSpelling
                     << suffix << "(...) end\n\n";
      } else {
        mySourceStream << generateMethod(theClass, aMtdSt[0], suffix, theInfo);
      }

      mySourceStream << ")\n";
    }
  }

  if (Binder_Util_Contains(binder_config.myExtraMethod, aClassSpelling)) {
    mySourceStream << binder_config.myExtraMethod.at(aClassSpelling) << '\n';
  }

  // DownCast from Standard_Transient
  if (theClass.IsTransient() && aClassSpelling != "Standard_Transient") {
    mySourceStream << ".addStaticFunction(\"DownCast\",+[](const "
                      "Handle(Standard_Transient) &h){ return Handle("
                   << aClassSpelling << ")::DownCast(h); })\n";
    myMetaStream << "---Down casting operator from handle to " << aClassSpelling
                 << ".\n";
    myMetaStream << "---@param h Standard_Transient\n";
    myMetaStream << "---@return " << aClassSpelling << '\n';
    myMetaStream << "function LuaOCCT." << myName << '.' << aClassSpelling
                 << ".DownCast(h) end\n\n";
  }

  return true;
}

bool Binder_Module::generateFields(const Binder_Cursor &theStruct,
                                   const CursorInfo &theInfo) {
  std::string aStructSpelling = theStruct.Spelling();
  std::vector<Binder_Cursor> aFields =
      theStruct.GetChildrenOfKind(CXCursor_FieldDecl, true);

  for (const auto &aField : aFields) {
    std::string aFieldSpelling = aField.Spelling();
    mySourceStream << ".addProperty(\"" << aFieldSpelling << "\",&"
                   << aStructSpelling << "::" << aFieldSpelling << ")\n";
    myMetaStream << "---@field " << aFieldSpelling << " "
                 << luaTypeMap(aField.Type(), theInfo) << '\n';
  }

  return true;
}

bool Binder_Module::generateStruct(const Binder_Cursor &theStruct,
                                   const Binder_Generator *theParent) {
  std::string aStructSpelling = theStruct.Spelling();
  if (Binder_Util_Contains(binder_config.myBlackListClass, aStructSpelling))
    return true;

  std::cout << "Binding struct: " << aStructSpelling << '\n';

  mySourceStream << ".beginClass<" << aStructSpelling << ">(\""
                 << aStructSpelling << "\")\n";
  CursorInfo info = {false, theStruct, aStructSpelling, {}};
  myMetaStream << "---@class " << aStructSpelling << '\n';
  generateCtor(theStruct, info);
  generateFields(theStruct, info);
  myMetaStream << "LuaOCCT." << myName << '.' << aStructSpelling << " = {}\n\n";
  generateMethods(theStruct, info);
  mySourceStream << ".endClass()\n\n";

  return true;
}

bool Binder_Module::generateClass(const Binder_Cursor &theClass,
                                  const Binder_Generator *theParent) {
  std::string aClassSpelling = theClass.Spelling();
  std::cout << "Binding class: " << aClassSpelling << '\n';

  Binder_Type aType = theClass.Type();
  Binder_Cursor aCls = theClass;
  CursorInfo info{false, aCls, aClassSpelling, {}};

  if (aCls.IsTypeDef()) {
    aType = aCls.UnderlyingTypedefType();
    aCls = aType.GetDeclaration().GetSpecialization();
    if (aCls.IsClassTemplate()) {
      info.isTemplate = true;
      info.argMap = getTemplateInstanceArgMap(theClass);
    } else
      return false;
  }

  std::vector<Binder_Cursor> aBases = aCls.Bases();

  bool baseRegistered = false;
  for (const auto aBase : aBases) {
    // NOTE: Use definition spelling!
    if (theParent->IsClassVisited(aBase.GetDefinition().Spelling())) {
      baseRegistered = true;
      break;
    }
  }

  if (baseRegistered) {
    std::string aBaseSpelling = aBases[0].GetDefinition().Spelling();
    mySourceStream << ".deriveClass<" << aClassSpelling << ',' << aBaseSpelling
                   << ">(\"" << aClassSpelling << "\")\n";
    myMetaStream << "---@class " << aClassSpelling << " : " << aBaseSpelling
                 << '\n';
  } else {
    mySourceStream << ".beginClass<" << aClassSpelling << ">(\""
                   << aClassSpelling << "\")\n";
    myMetaStream << "---@class " << aClassSpelling << '\n';
  }

  generateCtor(aCls, info);

  myMetaStream << "LuaOCCT." << myName << '.' << aClassSpelling << " = {}\n\n";

  generateMethods(aCls, info);

  mySourceStream << ".endClass()\n\n";

  return true;
}

bool Binder_Module::Init() {
  myExportName = myExportDir + "/l" + myName;
  myPrefix = myName + "_";

  mySourceStream = std::ofstream(myExportName + ".cpp");
  myEnumStream = std::ofstream(myExportDir + "/lenums.h", std::ios::app);
  myMetaStream = std::ofstream(myExportDir + "/_meta/" + myName + ".lua");

  return true;
}

bool Binder_Module::Generate() {
  Binder_Cursor aCursor = clang_getTranslationUnitCursor(myTransUnit);

  mySourceStream << "/* This file is generated, do not edit. */\n\n";
  mySourceStream << "#include \"lenums.h\"\n\n";
  mySourceStream << "\nvoid luaocct_init_" << myName << "(lua_State *L) {\n";
  mySourceStream << "luabridge::getGlobalNamespace(L)\n";
  mySourceStream << ".beginNamespace(\"LuaOCCT\")\n";
  mySourceStream << ".beginNamespace(\"" << myName << "\")\n\n";

  myMetaStream << "---@meta _\n";
  myMetaStream << "-- This file is generated, do not edit.\n";
  myMetaStream << "error('Cannot require a meta file')\n\n";
  myMetaStream << "LuaOCCT." << myName << " = {}\n\n";

  // Bind enumerators.
  std::vector<Binder_Cursor> anEnums = aCursor.Enums();

  for (const auto &anEnum : anEnums) {
    std::string anEnumSpelling = anEnum.Spelling();

    if (!Binder_Util_StartsWith(anEnumSpelling, myPrefix))
      continue;

    if (anEnumSpelling.empty())
      continue;

    if (!myParent->AddVisitedClass(anEnumSpelling))
      continue;

    if (!generateEnumCast(anEnum))
      continue;

    if (!generateEnumValue(anEnum))
      continue;
  }

  // Bind structs.
  std::vector<Binder_Cursor> aStructs =
      aCursor.GetChildrenOfKind(CXCursor_StructDecl);

  for (const auto &aStruct : aStructs) {
    std::string aStructSpelling = aStruct.Spelling();

    if (!Binder_Util_StartsWith(aStructSpelling, myPrefix) &&
        aStructSpelling != myName)
      continue;

    generateStruct(aStruct, myParent);
  }

  // Bind typedefs.
  std::vector<Binder_Cursor> aTypeDefs =
      aCursor.GetChildrenOfKind(CXCursor_TypedefDecl);

  for (const auto &aTypeDef : aTypeDefs) {
    std::string aClassSpelling = aTypeDef.Spelling();

    if (!Binder_Util_StartsWith(aClassSpelling, myPrefix) &&
        aClassSpelling != myName)
      continue;

    if (Binder_Util_Contains(binder_config.myBlackListClass, aClassSpelling))
      continue;

    Binder_Cursor aTDDecl = aTypeDef.UnderlyingTypedefType().GetDeclaration();
    std::string aTDDeclSpelling = aTDDecl.Spelling();

    if (aTDDecl.IsClass() &&
        Binder_Util_Contains(binder_config.myTemplateClass, aTDDeclSpelling)) {
      std::cout << "typedef: " << aTDDeclSpelling << ' ' << aClassSpelling
                << '\n';
      generateClass(aTypeDef, myParent);
    }
  }

  // Bind classes.
  std::vector<Binder_Cursor> aClasses =
      aCursor.GetChildrenOfKind(CXCursor_ClassDecl);

  for (const auto &aClass : aClasses) {
    std::string aClassSpelling = aClass.Spelling();

    if (!Binder_Util_StartsWith(aClassSpelling, myPrefix) &&
        aClassSpelling != myName)
      continue;

    if (Binder_Util_StrContains(aClassSpelling, "Sequence"))
      continue;

    if (Binder_Util_StrContains(aClassSpelling, "Array"))
      continue;

    if (Binder_Util_StrContains(aClassSpelling, "List"))
      continue;

    if (Binder_Util_Contains(binder_config.myBlackListClass, aClassSpelling))
      continue;

    // Handle forward declaration.
    // To make sure the binding order is along the inheritance tree.

    // Binder_Cursor aClassDef = aClass.GetDefinition();

    // if (aClass.GetChildren().empty()) {
    //   aClassDef = aClass.GetDefinition();

    //   if (aClassDef.IsNull())
    //     continue;
    // }

    if (aClass.GetChildren().empty())
      continue;

    if (!myParent->AddVisitedClass(aClassSpelling))
      continue;

    generateClass(aClass, myParent);
  }

  mySourceStream << ".endNamespace()\n.endNamespace();\n}\n";
  std::cout << "Module exported: " << myExportName << '\n' << std::endl;

  return true;
}

int Binder_Module::Save(const std::string &theFilePath) const {
  return clang_saveTranslationUnit(myTransUnit, theFilePath.c_str(),
                                   CXSaveTranslationUnit_None);
}

bool Binder_Module::Load(const std::string &theFilePath) {
  CXIndex anIndex = clang_createIndex(0, 0);
  CXTranslationUnit anTransUnit =
      clang_createTranslationUnit(anIndex, theFilePath.c_str());

  if (anTransUnit == nullptr) {
    clang_disposeTranslationUnit(anTransUnit);
    clang_disposeIndex(anIndex);
    return false;
  }

  dispose();
  myIndex = anIndex;
  myTransUnit = anTransUnit;

  return true;
}

void Binder_Module::dispose() {
  clang_disposeTranslationUnit(myTransUnit);
  clang_disposeIndex(myIndex);
}
