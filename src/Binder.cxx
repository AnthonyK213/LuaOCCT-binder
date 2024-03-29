#include "Binder_Generator.hxx"

#include <filesystem>
#include <sstream>

#include "Binder_Config.hxx"

Binder_Config binder_config;

/// arg[1]: OpenCASCADE include directory;
/// arg[2]: Module header directory;
/// arg[3]: Export directory;
/// arg[4]: Configuration file;
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
      .SetExportDir(argv[3]);

  if (!aGenerator.IsValid()) {
    std::cerr << "Generator is invalid\n";
    return -1;
  }

  binder_config.Init(argv[4]);
  aGenerator.GenerateEnumsBegin();

  for (const std::string &aModName : binder_config.myModules) {
    auto aMod = std::make_shared<Binder_Module>(aModName, aGenerator);
    aGenerator.SetModule(aMod);

    if (!aGenerator.Parse()) {
      return 1;
    }

    if (!aGenerator.Generate()) {
      return 2;
    }
  }

  aGenerator.GenerateEnumsEnd();
  aGenerator.GenerateMain();

  return 0;
}
