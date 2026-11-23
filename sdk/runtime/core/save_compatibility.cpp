#include "core/save_compatibility.h"
#include "core/patch_json.h"
#include <map>
#include <stdexcept>
namespace save_compatibility {
namespace {
std::map<std::string,std::string> Identity(const std::string& text) {
    patch_json::Json root;
    if(!patch_json::ParseJson(text,root) || root.Num("format",0)!=1)throw std::runtime_error("Invalid save mod stamp");
    auto* mods=root.Get("mods");if(!mods || mods->type!=patch_json::Json::Array)throw std::runtime_error("Invalid save mod list");
    std::map<std::string,std::string> identity;
    for(const auto& mod:mods->items)if(mod.Flag("affectsSaves")) {
        auto id=mod.Str("id"),version=mod.Str("version");
        if(id.empty()||version.empty()||!identity.emplace(id,version).second)throw std::runtime_error("Invalid/duplicate save dependency");
    }
    return identity;
}
}
bool HasGameData(const std::string& record){return !Identity(record).empty();}
bool Compatible(const std::string& saved,const std::string& active,std::string& reason) {
    try {
        auto current=Identity(active);
        if(saved.empty()) {
            if(current.empty())return true;
            reason="This save has no league version stamp. It cannot be loaded or overwritten with the installed league. The original save has been preserved.";return false;
        }
        if(Identity(saved)==current)return true;
        reason="This save was created with a different league or game-data mod version. Restore the matching league version to use it. The save has been preserved.";
    }catch(const std::exception& error){reason=std::string(error.what())+". The save was preserved; it will not be loaded or overwritten.";}
    return false;
}
}
