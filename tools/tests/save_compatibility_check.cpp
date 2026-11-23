#include "core/save_compatibility.h"
#include "kernel/save_safety.h"
#include "kernel/mods.h"
#include "app/app.h"
#include "core/log.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
namespace { std::string active; }
namespace app { Settings& Get(){static Settings settings;return settings;} }
namespace kernel::mods {std::string SaveRecord(){return active;}}
namespace logx {void Write(Level,const char*,...) {}}
std::string Record(const char* version,bool cosmetic=false){return std::string("{\"format\":1,\"mods\":[{\"id\":\"mvp27-league\",\"version\":\"")+version+"\",\"affectsSaves\":true}"+(cosmetic?",{\"id\":\"ui\",\"version\":\"2\",\"affectsSaves\":false}":"")+"]}";}
void Check(bool value,const char* what){if(!value)throw std::runtime_error(what);}
int main(int argc,char** argv) {
    if(argc!=2)return 2;namespace fs=std::filesystem;auto root=fs::absolute(argv[1]);
    auto saves=root/"saves";kernel::saves::Configure(saves,root);active=Record("1.0.0");std::string reason;
    Check(save_compatibility::Compatible(active,Record("1.0.0",true),reason),"cosmetic changes rejected");
    Check(!save_compatibility::Compatible(active,Record("2.0.0"),reason),"version mismatch accepted");
    Check(!save_compatibility::Compatible("invalid",active,reason),"bad stamp accepted");
    auto fresh=saves/"E/UDATA/4541008d/new/data.bin";fs::create_directories(fresh.parent_path());
    Check(kernel::saves::Allow(fresh,true,reason),"new save not stamped");
    {std::ofstream(fresh)<<"original player save";}
    Check(kernel::saves::Allow(fresh,false,reason),"same league cannot load");
    active=Record("2.0.0");
    Check(!kernel::saves::Allow(fresh,false,reason) && !kernel::saves::Allow(fresh,true,reason),"different league not protected");
    std::ifstream in(fresh);std::string content((std::istreambuf_iterator<char>(in)),{});Check(content=="original player save","save modified");
    in.close();
    auto legacy=saves/"E/UDATA/4541008d/legacy/data.bin";fs::create_directories(legacy.parent_path());{std::ofstream(legacy)<<"legacy save";}
    Check(!kernel::saves::Allow(legacy,false,reason),"unstamped legacy save allowed");
    Check(fs::exists(legacy) && fs::exists(fresh),"save deleted");
    active=Record("1.0.0",true);Check(kernel::saves::Allow(fresh,false,reason),"cosmetic mods changed compatibility");
    Check(kernel::saves::Backup("league-v1"),"versioned backup failed");
    std::string backup;
    for(const auto& entry:fs::directory_iterator(root/"backups"))if(entry.path().filename().string().find("league-v1")!=std::string::npos)backup=entry.path().filename().string();
    Check(!backup.empty(),"missing backup");
    auto stamp=saves/".mvp27-league-stamps/E/UDATA/4541008d/new.json";
    active=Record("2.0.0");{std::ofstream(stamp)<<active;std::ofstream(fresh)<<"new league bytes";}
    std::wstring message;Check(kernel::saves::Restore(backup,message),"backup restore failed");
    Check(!kernel::saves::Allow(fresh,false,reason),"restoring v1 save retained v2 stamp");
    std::ifstream restored(fresh);std::string restoredBytes((std::istreambuf_iterator<char>(restored)),{});Check(restoredBytes=="original player save","backup bytes not restored");
    std::cout<<"PASS new-slot stamping, same-league load, cosmetic changes, malformed/legacy/version mismatch rejection; original save bytes preserved; backup restores matching league stamp\n"<<std::flush;
    // Configure starts the same detached safety worker as the application.
    // This small harness has no application shutdown loop; end after assertions.
    std::_Exit(0);
}
