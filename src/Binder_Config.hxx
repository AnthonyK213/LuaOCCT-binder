#ifndef _LuaOCCT_Binder_Config_HeaderFile
#define _LuaOCCT_Binder_Config_HeaderFile

#include "toml.hpp"

#include <set>
#include <unordered_map>
#include <vector>

struct Binder_Config {
  toml::v3::ex::parse_result myToml;
  std::vector<std::string> myModules{};
  std::vector<std::string> myExtraModules{};
  std::set<std::string> myTemplateClass{};
  std::set<std::string> myImmutableType{};
  std::unordered_map<std::string, std::string> myLuaOperators{};
  std::set<std::string> myBlackListClass{};
  std::set<std::string> myBlackListMethodByName{};
  std::set<std::string> myBlackListMethod{};
  std::set<std::string> myBlackListCopyable{};
  std::unordered_map<std::string, std::string> myExtraMethod{};
  std::unordered_map<std::string, std::string> myManualMethod{};

  Binder_Config();

  ~Binder_Config();

  bool Init(const std::string &theFile);

private:
  bool load();

  static bool loadStringVec(const toml::v3::node_view<toml::v3::node> &theNode,
                            std::vector<std::string> &theStringVec);

  static bool loadStringSet(const toml::v3::node_view<toml::v3::node> &theNode,
                            std::set<std::string> &theStringSet);

  static bool
  loadStringMap(const toml::v3::node_view<toml::v3::node> &theNode,
                std::unordered_map<std::string, std::string> &theStringMap);
};

#endif
