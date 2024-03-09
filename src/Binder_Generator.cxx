#include "Binder_Generator.hxx"
#include "Binder_Module.hxx"
#include "Binder_Util.hxx"

#include <filesystem>
#include <fstream>

Binder_Generator::Binder_Generator() : myCurMod(nullptr), myExportDir(".") {}

Binder_Generator::~Binder_Generator() {}

bool Binder_Generator::AddVisitedClass(const std::string &theClass) {
  if (myVisitedClasses.find(theClass) == myVisitedClasses.end()) {
    myVisitedClasses.insert(theClass);
    return true;
  }

  return false;
}

#define MOD_CALL(F) myCurMod ? (myCurMod->F) : false

bool Binder_Generator::Parse() { return MOD_CALL(Parse()); }

bool Binder_Generator::Generate() {
  if (!myCurMod->Init())
    return false;

  if (!myCurMod->Generate())
    return false;

  return true;
}

int Binder_Generator::Save(const std::string &theFilePath) const {
  return MOD_CALL(Save(theFilePath));
}

bool Binder_Generator::Load(const std::string &theFilePath) {
  return MOD_CALL(Load(theFilePath));
}

#undef MOD_CALL

bool Binder_Generator::GenerateEnumsBegin() {
  std::string thePath = myExportDir + "/lenums.h";

  std::ofstream aStream{thePath};
  aStream << "/* This file is generated, do not edit. */\n\n";
  aStream << "#ifndef _LuaOCCT_lenum_HeaderFile\n#define "
             "_LuaOCCT_lenum_HeaderFile\n\n";
  aStream << "#include \"lbind.h\"\n\n";

  return true;
}

bool Binder_Generator::GenerateEnumsEnd() {
  std::string thePath = myExportDir + "/lenums.h";

  std::ofstream aStream{thePath, std::ios::app};
  aStream << "\n#endif\n";

  return true;
}

bool Binder_Generator::GenerateMain(
    const std::vector<std::string> &theModules) {
  std::string thePath = myExportDir + "/luaocct.cpp";

  // The header file.
  std::ofstream aStream{thePath};
  aStream << "/* This file is generated, do not edit. */\n\n";
  aStream << "#include \"luaocct.h\"\n";

  for (const auto &aMod : theModules) {
    aStream << "#include \"l" << aMod << ".h\"\n";
  }

  aStream << "#include \"lutil.h\"\n\n";
  aStream << "int32_t luaopen_luaocct(lua_State *L) {\n";

  for (const auto &aMod : theModules) {
    aStream << "\tluaocct_init_" << aMod << "(L);\n";
  }

  aStream << "\tluaocct_init_util(L);\n";
  aStream << "\n\treturn 0;\n}\n";

  std::cout << "Exported: " << thePath << '\n' << std::endl;

  return true;
}

bool Binder_Generator::IsClassVisited(const std::string &theClass) const {
  return Binder_Util_Contains(myVisitedClasses, theClass);
}

bool Binder_Generator::IsValid() const {
  if (!std::filesystem::is_directory(myModDir))
    return false;

  if (!std::filesystem::is_directory(myOcctIncDir))
    return false;

  return true;
}
