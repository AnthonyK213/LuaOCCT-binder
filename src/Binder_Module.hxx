#ifndef _LuaOCCT_Binder_Module_HeaderFile
#define _LuaOCCT_Binder_Module_HeaderFile

#include <fstream>
#include <string>
#include <vector>

#include "Binder_Cursor.hxx"

class Binder_Generator;

class Binder_Module {
public:
  Binder_Module(const std::string &theName, Binder_Generator &theParent);

  ~Binder_Module();

  bool Init();

  bool Generate();

  int Save(const std::string &theFilePath) const;

  bool Load(const std::string &theFilePath);

  bool Parse();

protected:
  bool generateEnumCast(const Binder_Cursor &theEnum);

  bool generateEnumValue(const Binder_Cursor &theEnum);

  bool generateCtor(const Binder_Cursor &theClass);

  std::string generateMethod(const Binder_Cursor &theClass,
                             const Binder_Cursor &theMethod,
                             const std::string &theSuffix,
                             bool theIsOverload = false);

  bool generateMethods(const Binder_Cursor &theClass);

  bool generateFields(const Binder_Cursor &theStruct);

  bool generateStruct(const Binder_Cursor &theStruct,
                      const Binder_Generator *theParent);

  bool generateClass(const Binder_Cursor &theClass,
                     const Binder_Generator *theParent);

  void dispose();

private:
  std::string myName;
  Binder_Generator *myParent;

  std::string myExportDir;
  std::string myMetaExportDir;
  std::string myExportName;
  std::string myPrefix;

  CXIndex myIndex;
  CXTranslationUnit myTransUnit;

  std::ofstream myHeaderStream;
  std::ofstream mySourceStream;
  std::ofstream myEnumStream;
  std::ofstream myMetaStream;
};

#endif
