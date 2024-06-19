#include "Binder_Config.hxx"

using namespace std::string_view_literals;

Binder_Config::Binder_Config() {}

Binder_Config::~Binder_Config() {}

bool Binder_Config::Init(const std::string &theFile) {
  myToml = toml::parse_file(theFile);
  return load();
}

bool Binder_Config::loadStringVec(
    const toml::v3::node_view<toml::v3::node> &theNode,
    std::vector<std::string> &theStringVec) {
  if (toml::array *arr = theNode.as_array()) {
    theStringVec.clear();

    for (auto it = arr->cbegin(); it != arr->cend(); ++it) {
      if (it->is_string()) {
        std::optional<std::string> v =
            it->as_string()->value_exact<std::string>();
        if (v.has_value())
          theStringVec.push_back(v.value());
      }
    }

    return true;
  }

  return false;
}

bool Binder_Config::loadStringSet(
    const toml::v3::node_view<toml::v3::node> &theNode,
    std::set<std::string> &theStringSet) {
  if (toml::array *arr = theNode.as_array()) {
    theStringSet.clear();

    for (auto it = arr->cbegin(); it != arr->cend(); ++it) {
      if (it->is_string()) {
        std::optional<std::string> v =
            it->as_string()->value_exact<std::string>();
        if (v.has_value())
          theStringSet.insert(v.value());
      }
    }

    return true;
  }

  return false;
}

bool Binder_Config::loadStringMap(
    const toml::v3::node_view<toml::v3::node> &theNode,
    std::unordered_map<std::string, std::string> &theStringMap) {
  if (toml::table *tbl = theNode.as_table()) {
    theStringMap.clear();

    for (auto it = tbl->cbegin(); it != tbl->cend(); ++it) {
      std::string key = it->first.data();
      if (it->second.is_string()) {
        std::optional<std::string> v =
            it->second.as_string()->value_exact<std::string>();
        if (v.has_value())
          theStringMap[key] = v.value();
      }
    }

    return true;
  }

  return false;
}

bool Binder_Config::load() {
  if (!loadStringVec(myToml["modules"], myModules))
    return false;

  if (!loadStringVec(myToml["extra_modules"], myExtraModules))
    return false;

  if (!loadStringSet(myToml["template_class"], myTemplateClass))
    return false;

  if (!loadStringSet(myToml["immutable_type"], myImmutableType))
    return false;

  if (!loadStringMap(myToml["lua_operators"], myLuaOperators))
    return false;

  if (!loadStringSet(myToml["black_list"]["class"], myBlackListClass))
    return false;

  if (!loadStringSet(myToml["black_list"]["method_by_name"],
                     myBlackListMethodByName))
    return false;

  if (!loadStringSet(myToml["black_list"]["method"], myBlackListMethod))
    return false;

  if (!loadStringSet(myToml["black_list"]["copyable"], myBlackListCopyable))
    return false;

  if (!loadStringMap(myToml["extra_method"], myExtraMethod))
    return false;

  if (!loadStringMap(myToml["manual_method"], myManualMethod))
    return false;

  return true;
}
