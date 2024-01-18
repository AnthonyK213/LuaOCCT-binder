#include "Binder_Generator.hxx"
#include "Binder_Module.hxx"
#include "Binder_Util.hxx"

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

bool Binder_Generator::Parse() { return MOD_CALL(parse()); }

bool Binder_Generator::Generate() { return MOD_CALL(generate(myExportDir)); }

int Binder_Generator::Save(const std::string &theFilePath) const {
  return MOD_CALL(save(theFilePath));
}

bool Binder_Generator::Load(const std::string &theFilePath) {
  return MOD_CALL(load(theFilePath));
}

#undef MOD_CALL

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

  aStream << "#include \"lGeomConvert.h\"\n";
  aStream << "#include \"lutil.h\"\n\n";
  aStream << "int32_t luaopen_luaocct(lua_State *L) {\n";

  for (const auto &aMod : theModules) {
    aStream << "\tluaocct_init_" << aMod << "(L);\n";
  }

  aStream << "\tluaocct_init_GeomConvert(L);\n";
  aStream << "\tluaocct_init_util(L);\n";
  aStream << "\n\treturn 0;\n}\n";

  std::cout << "Exported: " << thePath << '\n' << std::endl;

  return true;
}

bool Binder_Generator::IsClassVisited(const std::string &theClass) const {
  return Binder_Util_Contains(myVisitedClasses, theClass);
}
