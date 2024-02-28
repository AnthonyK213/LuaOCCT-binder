#include "Binder_Generator.hxx"

#include <filesystem>
#include <sstream>

/// arg[1]: OpenCASCADE include directory;
/// arg[2]: Module header directory;
/// arg[3]: Export directory;
int main(int argc, char const *argv[]) {
  if (argc < 5) {
    std::cerr << "Args?\n";
    return 1;
  }

  Binder_Generator aGenerator{};

  // std::vector<std::string> aWin32Args = {"-fms-compatibility",
  //                                        "-fms-extensions",
  //                                        "-fms-compatibility-version=19",
  //                                        "-fdelayed-template-parsing",
  //                                        "-DWNT",
  //                                        "-DWIN32",
  //                                        "-D_WINDOWS"};

  // std::vector<std::string> aLinuxArgs = {"-fpermissive",
  // "-fvisibility=hidden",
  //                                        "-fvisibility-inlines-hidden"};

  std::string modDir = argv[2];

  aGenerator.SetModDir(modDir)
      .SetOcctIncDir(argv[1])
      .SetClangArgs({"-x", "c++", "-std=c++17", "-D__CODE_GENERATOR__",
                     "-Wno-deprecated-declarations", "-ferror-limit=0",
                     "-DCSFDB", "-DHAVE_CONFIG_H"})
      .SetExportDir(argv[3])
      .SetConfigFile(argv[4]);

  if (!aGenerator.IsValid()) {
    std::cerr << "Generator is invalid\n";
    return -1;
  }

  // std::vector<std::string> aMods{};

  // for (auto &entry : std::filesystem::directory_iterator(modDir)) {
  //   std::string name = entry.path().stem().string();
  //   aMods.push_back(name);
  // }

  /* clang-format off */

  /// NOTE: Keep the order correct!!
  std::vector<std::string> aMods = {
      "Standard",
      "GeomAbs",
      "TopAbs",
      "Precision",
      // "TCollection",
      "gp",
      "Geom2d",
      "Geom",
      "TopoDS",
      "TopExp",
      "TopLoc",
      "Poly",
      "Message",
      "BRepBuilderAPI",
      "IntTools",
      "BOPDS",
      "BOPAlgo",
      "BRepAlgoAPI",
      "BRep",
      "BRepLib",
      "Bnd",
      "CPnts",
      "GeomConvert",
      "IMeshTools",
      "BRepGProp",
      "GProp",
      // "TDocStd",
      // "TDF",
      // "XCAFPrs",
  };

  /* clang-format on */

  for (const std::string &aModName : aMods) {
    auto aMod = std::make_shared<Binder_Module>(aModName, aGenerator);
    aGenerator.SetModule(aMod);

    if (!aGenerator.Parse()) {
      return 1;
    }

    if (!aGenerator.Generate()) {
      return 2;
    }
  }

  aGenerator.GenerateMain(aMods);

  return 0;
}
