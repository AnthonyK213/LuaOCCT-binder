#include "Binder_Generator.hxx"

#include <sstream>

/// arg[1]: OpenCASCADE include directory;
/// arg[2]: Module header directory;
/// arg[3]: Export directory;
int main(int argc, char const *argv[]) {
  if (argc < 4) {
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

  aGenerator.SetModDir(argv[2])
      .SetOcctIncDir(argv[1])
      .SetClangArgs({"-x", "c++", "-std=c++17", "-D__CODE_GENERATOR__",
                     "-Wno-deprecated-declarations", "-ferror-limit=0",
                     "-DCSFDB", "-DHAVE_CONFIG_H"})
      .SetExportDir(argv[3]);

  /* clang-format off */

  std::vector<std::string> aMods = {
      "Standard",
      "GeomAbs",
      "TopAbs",
      "Precision",
      "gp",
      "Geom2d",
      "Geom",
      "TopoDS",
      "TopExp",
      "TopLoc",
      "Poly",
      "BRepBuilderAPI",
      "BRep",
      "BRepLib",
      "Bnd",
      "CPnts",
      "GeomConvert",
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
