#include "kernel/mods.h"
#include "core/log.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
namespace logx {void Write(Level,const char*,...) {}}
int main(int argc,char** argv) {
    if(argc!=3)return 2;auto root=fs::absolute(argv[1]);std::string mode=argv[2];auto game=root/"game",mods=root/"mods";fs::create_directories(game);fs::create_directories(mods);
    std::string sha="2f57040bab2c8b2eb33ebc7aa93209ad30dd103f";
    auto make=[&](const char* folder,const char* id,bool required){auto path=mods/folder;fs::create_directories(path/"files/data");std::ofstream(path/"mod.json")<<"{\"format\":1,\"id\":\""<<id<<"\",\"version\":\"1.0.0\",\"layout\":1,\"required\":"<<(required?"true":"false")<<",\"affectsSaves\":true,\"game\":{\"xbeSha1\":\""<<sha<<"\"}}";std::ofstream(path/"files/data/probe.txt")<<id;return path;};
    std::vector<fs::path> extra;
    if(mode!="missing")extra.push_back(make("league","mvp27-league",true));
    make("optional","optional",false);
    std::ofstream(mods/"mods.json")<<"{\"format\":1,\"order\":[{\"id\":\"mvp27-league\",\"enabled\":false},{\"id\":\"optional\",\"enabled\":false}]}";
    kernel::mods::Init(game,mods,mode=="explicit"?extra:std::vector<fs::path>{},mode=="safe"||mode=="explicit",sha);
    if(kernel::mods::Required("mvp27-league")!=(mode!="missing") || kernel::mods::Enabled("optional"))throw std::runtime_error("Required/safe-mode policy failed");
    std::cout<<"PASS "<<mode<<": required league forced on, optional mods remain off, missing league detectable\n";
}
