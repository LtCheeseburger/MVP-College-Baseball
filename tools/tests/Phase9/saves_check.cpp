// Exercise the real save_safety filesystem transactions in an isolated folder.
#include "kernel/save_safety.cpp"
#include <iostream>
#include <stdexcept>
namespace {std::string record=R"({"format":1,"mods":[]})";}
namespace kernel::mods {std::string SaveRecord(){return record;}}
namespace app {Settings& Get(){static Settings settings;return settings;}}
namespace logx {void Write(Level,const char*,...) {}}
int main(int argc,char** argv){
    using namespace kernel::saves;
    if(argc!=2)throw std::runtime_error("Provide an empty test folder");
    const fs::path root=fs::absolute(argv[1]);if(fs::exists(root))throw std::runtime_error("Test folder must be new");
    fs::create_directories(root);g_saves=root/"saves";g_backups=root/"backups";g_history=root/"history";g_lastgood=root/"lastgood";g_savemods=g_saves/".mvp27-league-stamps";
    DynastySave save;std::string reason;
    if(NewestDynasty(save))throw std::runtime_error("Empty index");
    std::vector<uint8_t> first(300,1),second(300,2);
    if(!PublishDynastyAutosave(first,reason)||!NewestDynasty(save)||save.name!=u"MVP27 Auto (Dynasty)")throw std::runtime_error("Native slot publication/index: "+reason);
    auto slot=save.slot;auto rel=slot.lexically_relative(g_saves);
    LimitContinueTo(slot);
    if(!ContinueEntryVisible(slot.parent_path(),slot.filename().string())||ContinueEntryVisible(slot.parent_path(),"another-save")||ContinueEntryVisible(g_saves/"F/UDATA/4541008d",slot.filename().string())||!ContinueEntryVisible(root/"unrelated","anything"))throw std::runtime_error("Continue directory isolation");
    LimitContinueTo({});if(!ContinueEntryVisible(slot.parent_path(),"another-save"))throw std::runtime_error("Continue filter not cleared");
    record=R"({"format":1,"mods":[{"id":"mvp27-league","version":"2.0.0","affectsSaves":true}]})";
    if(NewestDynasty(save))throw std::runtime_error("Incompatible save accepted");
    const auto originalSlot=slot;
    if(!PublishDynastyAutosave(second,reason)||!NewestDynasty(save)||save.slot==originalSlot)
        throw std::runtime_error("New league did not receive a separate autosave: "+reason);
    {std::ifstream in(originalSlot/"data.dat",std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});
     if(bytes!=first)throw std::runtime_error("Older league autosave overwritten");}
    auto title=g_saves/"E/UDATA/4541008d/TitleMeta.xbx";
    fs::create_directories(title.parent_path());{std::ofstream out(title);out<<"title metadata";}
    if(!Allow(title,true,reason)||!Allow(title,false,reason)||!SlotOf(title,nullptr).empty())
        throw std::runtime_error("Shared title metadata incorrectly treated as a league save");
    // Browsing incompatible native metadata is safe; loading/writing data is not.
    if(!Allow(slot/"SaveMeta.xbx",false,reason)||!Allow(slot/"saveimage.xbx",false,reason)||!Allow(slot,false,reason))throw std::runtime_error("Metadata inspection blocked");
    if(Allow(slot/"data.dat",false,reason)||Allow(slot/"SaveMeta.xbx",true,reason))throw std::runtime_error("Metadata bypass changed payload/write protection");
    auto options=g_saves/"E/UDATA/4541008d/EB8970ABAE91/data.dat";
    fs::create_directories(options.parent_path());{std::ofstream out(options);out<<"native options";}
    if(!Allow(options,false,reason)||!Allow(options,true,reason))throw std::runtime_error("Native options incorrectly league-bound");
    auto foreign=g_saves/"E/UDATA/11111111/EB8970ABAE91/data.dat";
    fs::create_directories(foreign.parent_path());{std::ofstream out(foreign);out<<"foreign payload";}
    if(Allow(foreign,false,reason))throw std::runtime_error("Options exemption leaked across titles");
    record=R"({"format":1,"mods":[]})";
    if(!fs::exists(g_lastgood/rel/"data.dat"))throw std::runtime_error("Last-good copy absent");
    if(!PublishDynastyAutosave(second,reason)||!fs::exists(g_history/rel))throw std::runtime_error("Autosave replacement/history: "+reason);
    auto bytes=[](const fs::path& path){std::ifstream in(path,std::ios::binary);return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),{});};
    if(bytes(slot/"data.dat")!=second||bytes(g_lastgood/rel/"data.dat")!=second)throw std::runtime_error("Committed bytes differ");
    fs::remove(slot/"mvp27-autosave.txt");
    if(PublishDynastyAutosave(first,reason)||bytes(slot/"data.dat")!=second)throw std::runtime_error("Unowned slot overwritten");
    {std::ofstream out(slot/"data.dat",std::ios::binary|std::ios::trunc);}
    if(NewestDynasty(save))throw std::runtime_error("Incomplete save indexed");
    std::cout<<"PASS native slot publication, newest dynasty index, last-good/history, collision refusal, incomplete exclusion\n";
}
