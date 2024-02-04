#include "Binder_Module.hxx"
#include "Binder_Common.hxx"
#include "Binder_Generator.hxx"
#include "Binder_Util.hxx"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

Binder_Module::Binder_Module(const std::string &theName,
                             Binder_Generator &theParent)
    : myName(theName), myParent(&theParent), myIndex(nullptr),
      myTransUnit(nullptr) {}

Binder_Module::~Binder_Module() { dispose(); }

bool Binder_Module::parse() {
  dispose();

  myIndex = clang_createIndex(0, 0);

  std::string aHeader = myParent->ModDir() + "/_" + myName + ".h";

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

static bool generateEnumCast(const Binder_Cursor &theEnum,
                             std::ostream &theStream) {
  std::string anEnumSpelling{};
  std::vector<Binder_Cursor> anEnumConsts{};

  if (!generateEnum(theEnum, anEnumSpelling, anEnumConsts))
    return false;

  std::cout << "Binding enum cast: " << anEnumSpelling << '\n';

  theStream << "template<> struct luabridge::Stack<" << anEnumSpelling
            << "> : luabridge::Enum<" << anEnumSpelling << ','
            << Binder_Util_Join(anEnumConsts.cbegin(), anEnumConsts.cend(),
                                [&](const Binder_Cursor &theEnumConst) {
                                  return anEnumSpelling +
                                         "::" + theEnumConst.Spelling();
                                })
            << ">{};\n";

  return true;
}

static bool generateEnumValue(const Binder_Cursor &theEnum,
                              std::ostream &theStream) {
  std::string anEnumSpelling{};
  std::vector<Binder_Cursor> anEnumConsts{};

  if (!generateEnum(theEnum, anEnumSpelling, anEnumConsts))
    return false;

  std::cout << "Binding enum: " << anEnumSpelling << '\n';

  theStream << ".beginNamespace(\"" << anEnumSpelling << "\")\n";

  for (const auto anEnumConst : anEnumConsts) {
    std::string anEnumConstSpelling = anEnumConst.Spelling();
    theStream << ".addProperty(\"" << anEnumConstSpelling << "\",+[](){ return "
              << anEnumSpelling << "::" << anEnumConstSpelling << "; })\n";
  }

  theStream << ".endNamespace()\n\n";

  return true;
}

static bool generateCtor(const Binder_Cursor &theClass,
                         std::ostream &theStream) {
  if (theClass.IsAbstract() || theClass.IsStaticClass())
    return true;

  std::string aClassSpelling = theClass.Spelling();
  bool needsDefaultCtor = theClass.NeedsDefaultCtor();

  std::vector<Binder_Cursor> aCtors =
      theClass.GetChildrenOfKind(CXCursor_Constructor, true);

  // if no public ctor but non-public, do not bind any ctor.
  if (aCtors.empty() && !needsDefaultCtor)
    return true;

  if (theClass.IsTransient()) {
    // Intrusive container is gooooooooooooooooooooooooood!
    theStream << ".addConstructorFrom<opencascade::handle<" << aClassSpelling
              << ">,";
  } else {
    theStream << ".addConstructor<";
  }

  if (needsDefaultCtor) {
    theStream << "void()";
  } else {
    theStream << Binder_Util_Join(
        aCtors.cbegin(), aCtors.cend(), +[](const Binder_Cursor &theCtor) {
          std::ostringstream oss{};
          oss << "void(";
          std::vector<Binder_Cursor> aParams = theCtor.Parameters();
          oss << Binder_Util_Join(
                     aParams.cbegin(), aParams.cend(),
                     +[](const Binder_Cursor &theParam) {
                       return theParam.Type().Spelling();
                     })
              << ')';
          return oss.str();
        });
  }

  theStream << ">()\n";

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
  if (Binder_Util_Contains(binder::METHOD_BLACKLIST, aFuncSpelling))
    return true;

  return false;
}

static std::string generateMethod(const Binder_Cursor &theClass,
                                  const Binder_Cursor &theMethod,
                                  bool theIsOverload = false) {
  std::string aClassSpelling = theClass.Spelling();
  std::string aMethodSpelling = theMethod.Spelling();
  std::vector<Binder_Cursor> aParams = theMethod.Parameters();
  std::ostringstream oss{};

  std::string aFuncName = aClassSpelling + "::" + aMethodSpelling;

  if (Binder_Util_Contains(binder::MANUAL_METHODS, aFuncName)) {
    return binder::MANUAL_METHODS.at(aFuncName);
  }

  if (theMethod.IsOperator()) {
    oss << "+[](const " << aClassSpelling << " &theSelf";

    if (aMethodSpelling == "operator-") { /* __unm */
      if (aParams.empty()) {
        oss << "){ return -theSelf; }";
      } else { /* __sub */
        oss << ',' << aParams[0].Type().Spelling()
            << " theOther){ return theSelf-theOther; }";
      }
    } else {
      if (aParams.empty())
        return "";

      oss << ',' << aParams[0].Type().Spelling() << " theOther){ return theSelf"
          << aMethodSpelling.substr(8) << "theOther; }";
    }

    return oss.str();
  }

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
        anIn.cbegin(), anIn.cend(), +[](const Binder_Cursor &theParam) {
          return theParam.Type().Spelling() + " " + theParam.Spelling();
        });

    Binder_Type aRetType = theMethod.ReturnType();
    std::string aRetTypeSpelling = aRetType.Spelling();
    bool anHasRetVal = aRetTypeSpelling != "void";
    bool anTupleOut = anHasRetVal || anOut.size() > 1;

    if (anTupleOut) {
      oss << ")->std::tuple<";

      if (anHasRetVal)
        oss << aRetTypeSpelling << ',';

      oss << Binder_Util_Join(
                 anOut.cbegin(), anOut.cend(),
                 +[](const Binder_Cursor &theParam) {
                   return theParam.Type().GetPointee().Spelling();
                 })
          << "> { ";
    } else {
      oss << ")->" << anOut[0].Type().GetPointee().Spelling() << " { ";
    }

    for (const auto &anOutParam : anOut) {
      oss << anOutParam.Type().GetPointee().Spelling() << ' '
          << anOutParam.Spelling() << "{};";
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
    } else {
      oss << "return " << anOut[0].Spelling() << "; }";
    }

    return oss.str();
  }

  if (theIsOverload) {
    oss << "luabridge::overload<";
    oss << Binder_Util_Join(
        aParams.cbegin(), aParams.cend(), +[](const Binder_Cursor &theParam) {
          return theParam.Type().Spelling();
        });
    oss << ">(&" << aClassSpelling << "::" << aMethodSpelling << ')';
  } else {
    oss << '&' << aClassSpelling << "::" << aMethodSpelling;
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

static bool generateMethods(const Binder_Cursor &theClass,
                            std::ostream &theStream) {
  std::string aClassSpelling = theClass.Spelling();
  std::vector<Binder_Cursor> aMethods =
      theClass.GetChildrenOfKind(CXCursor_CXXMethod);

  std::map<std::string, Binder_MethodGroup> aGroups{};

  bool hasCopyFunc = false;

  // Group cxxmethods by name.
  for (const auto &aMethod : aMethods) {
    std::string aFuncSpelling = aMethod.Spelling();

    if (aFuncSpelling == "Copy")
      hasCopyFunc = true;

    if (aMethod.IsOperator()) {
      if (aFuncSpelling == "operator-") {
        if (aMethod.Parameters().empty()) {
          aFuncSpelling = "__unm";
        } else {
          aFuncSpelling = "__sub";
        }
      } else {
        aFuncSpelling = binder::LUA_OPERATORS.at(aFuncSpelling);
      }
    }

    if (Binder_Util_Contains(aGroups, aFuncSpelling)) {
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

    if (!aMtd.empty()) {
      theStream << ".addFunction(\"" << anIter->first << "\",";

      if (aMethodGroup.HasOverload()) {
        theStream << Binder_Util_Join(
            aMtd.cbegin(), aMtd.cend(), [&](const Binder_Cursor &theMethod) {
              return generateMethod(theClass, theMethod, true);
            });
      } else {
        theStream << generateMethod(theClass, aMtd[0]);
      }

      theStream << ")\n";
    }

    if (!aMtdSt.empty()) {
      theStream << ".addStaticFunction(\"" << anIter->first
                << (aMtd.empty()
                        ? ""
                        : "_") /* Add a "_" if there is non-static overload. */
                << "\",";

      if (aMethodGroup.HasOverload()) {
        theStream << Binder_Util_Join(aMtdSt.cbegin(), aMtdSt.cend(),
                                      [&](const Binder_Cursor &theMethod) {
                                        return generateMethod(theClass,
                                                              theMethod, true);
                                      });
      } else {
        theStream << generateMethod(theClass, aMtdSt[0]);
      }

      theStream << ")\n";
    }
  }

  if (Binder_Util_Contains(binder::EXTRA_METHODS, aClassSpelling)) {
    theStream << binder::EXTRA_METHODS.at(aClassSpelling) << '\n';
  }

  // DownCast from Standard_Transient
  if (theClass.IsTransient() && aClassSpelling != "Standard_Transient") {
    theStream << ".addStaticFunction(\"DownCast\",+[](const "
                 "Handle(Standard_Transient) &h){ return Handle("
              << aClassSpelling << ")::DownCast(h); })\n";
  }

  if (!hasCopyFunc && !theClass.IsStaticClass() && !theClass.IsAbstract() &&
      !theClass.Ctors(true).empty() && aClassSpelling != "Standard_Transient" &&
      true /* TODO: Check copy contructor */ &&
      !Binder_Util_Contains(binder::COPY_BLACKLIST, aClassSpelling)) {
    theStream << ".addFunction(\"Copy\",+[](const " << aClassSpelling
              << " &__theSelf__){ return " << aClassSpelling
              << "{__theSelf__}; })\n";
  }

  return true;
}

static bool generateClass(const Binder_Cursor &theClass,
                          std::ostream &theStream,
                          const Binder_Generator *theParent) {
  std::string aClassSpelling = theClass.Spelling();
  std::cout << "Binding class: " << aClassSpelling << '\n';
  std::vector<Binder_Cursor> aBases = theClass.Bases();

  bool baseRegistered = false;
  for (const auto aBase : aBases) {
    // NOTE: Use definition spelling!
    if (theParent->IsClassVisited(aBase.GetDefinition().Spelling())) {
      baseRegistered = true;
      break;
    }
  }

  if (baseRegistered) {
    theStream << ".deriveClass<" << aClassSpelling << ", "
              << aBases[0].GetDefinition().Spelling() << ">(\""
              << aClassSpelling << "\")\n";
  } else {
    theStream << ".beginClass<" << aClassSpelling << ">(\"" << aClassSpelling
              << "\")\n";
  }

  generateCtor(theClass, theStream);
  generateMethods(theClass, theStream);

  theStream << ".endClass()\n\n";

  return true;
}

bool Binder_Module::generate(const std::string &theExportDir) {
  Binder_Cursor aCursor = clang_getTranslationUnitCursor(myTransUnit);
  std::string theExportName = theExportDir + "/l" + myName;
  std::string thePrefix = myName + "_";

  // The header file.
  std::ofstream aStream{theExportName + ".h"};
  std::string aGuard = "_LuaOCCT_l" + myName + "_HeaderFile";
  aStream << "/* This file is generated, do not edit. */\n\n";
  aStream << "#ifndef " << aGuard << "\n#define " << aGuard << "\n\n";
  aStream << "#include \"lbind.h\"\n\n";
  aStream << "void luaocct_init_" << myName << "(lua_State *L);\n\n";
  aStream << "#endif\n";

  // The source file.
  aStream = std::ofstream(theExportName + ".cpp");

  aStream << "/* This file is generated, do not edit. */\n\n";
  aStream << "#include \"l" << myName << ".h\"\n\n";

  // Bind enumerators.
  std::vector<Binder_Cursor> anEnums = aCursor.Enums();
  std::ostringstream anEnumChuck{};

  for (const auto &anEnum : anEnums) {
    std::string anEnumSpelling = anEnum.Spelling();

    if (!Binder_Util_StartsWith(anEnumSpelling, thePrefix))
      continue;

    if (anEnumSpelling.empty())
      continue;

    if (!myParent->AddVisitedClass(anEnumSpelling))
      continue;

    if (!generateEnumCast(anEnum, aStream))
      continue;

    if (!generateEnumValue(anEnum, anEnumChuck))
      continue;
  }

  aStream << "\nvoid luaocct_init_" << myName << "(lua_State *L) {\n";
  aStream << "luabridge::getGlobalNamespace(L)\n";
  aStream << ".beginNamespace(\"LuaOCCT\")\n";
  aStream << ".beginNamespace(\"" << myName << "\")\n\n";

  aStream << anEnumChuck.str();

  // Bind classes.
  std::vector<Binder_Cursor> aClasses =
      aCursor.GetChildrenOfKind(CXCursor_ClassDecl);

  for (const auto &aClass : aClasses) {
    std::string aClassSpelling = aClass.Spelling();

    if (!Binder_Util_StartsWith(aClassSpelling, thePrefix) &&
        aClassSpelling != myName)
      continue;

    if (Binder_Util_StartsWith(aClassSpelling, "Handle"))
      continue;

    if (Binder_Util_StartsWith(aClassSpelling, "NCollection"))
      continue;

    // if (Binder_Util_StartsWith(aClassSpelling, "TCol"))
    //   continue;

    if (Binder_Util_StrContains(aClassSpelling, "Sequence"))
      continue;

    if (Binder_Util_StrContains(aClassSpelling, "Array"))
      continue;

    if (Binder_Util_StrContains(aClassSpelling, "List"))
      continue;

    if (Binder_Util_Contains(binder::CLASS_BLACKLIST, aClassSpelling))
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

    generateClass(aClass, aStream, myParent);
  }

  aStream << ".endNamespace()\n.endNamespace();\n}\n";
  std::cout << "Module exported: " << theExportName << '\n' << std::endl;

  return true;
}

int Binder_Module::save(const std::string &theFilePath) const {
  return clang_saveTranslationUnit(myTransUnit, theFilePath.c_str(),
                                   CXSaveTranslationUnit_None);
}

bool Binder_Module::load(const std::string &theFilePath) {
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
