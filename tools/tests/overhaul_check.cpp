// Headless integration checks using the production mod VFS and overhaul helper.
#include "common/overhaul.h"
#include "kernel/mods.h"
#include "core/log.h"
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <stdexcept>
namespace fs = std::filesystem;
namespace logx {
void Write(Level, const char* format, ...) {
    va_list args; va_start(args,format); std::vprintf(format,args); va_end(args); std::puts("");
}
}
static void Check(bool pass, const char* label) {
    std::printf("%s %s\n",pass?"PASS":"FAIL",label);
    if (!pass) throw std::runtime_error(label);
}
int main(int argc,char** argv) {
    try {
        if(argc<5)return 2;
        fs::path game=fs::absolute(argv[1]), mods=fs::absolute(argv[2]);
        bool safe=std::string(argv[3])=="safe", expected=std::string(argv[4])=="on";
        std::vector<fs::path> extra;
        for(int i=5;i<argc;i++)extra.push_back(fs::absolute(argv[i]));
        kernel::mods::Init(game,mods,extra,safe,"2f57040bab2c8b2eb33ebc7aa93209ad30dd103f");
        Check(overhaul::Active()==expected,"activation follows enabled id plus mounted marker");
        Check(overhaul::Active()==expected,"activation remains latched");
        if(safe)Check(!kernel::mods::Active(),"safe mode rejects saved and explicit mods");
        std::vector<uint8_t> data;
        auto file=overhaul::Root()/"probe.txt";
        bool read=overhaul::Load(file,data);
        Check(read==expected,"overhaul helper refuses reads when disabled");
        if(expected) {
            Check(std::string(data.begin(),data.end())=="mod content","VFS reads mod content instead of conflicting disk file");
            Check(overhaul::Text(file)=="mod content","text uses mounted bytes");
            auto first=overhaul::DiskFile(file), second=overhaul::DiskFile(file);
            Check(!first.empty() && !second.empty() && first!=second,"file-only readers receive distinct reserved temp paths");
            std::ifstream in(first,std::ios::binary); std::string text((std::istreambuf_iterator<char>(in)),{});in.close();
            Check(text=="mod content","materialized file matches mounted bytes");
            overhaul::ReleaseDiskFile(first);overhaul::ReleaseDiskFile(second);
            Check(!fs::exists(first) && !fs::exists(second),"temporary files removed after reader closes");
            auto native=game/"native.txt";
            Check(overhaul::DiskFile(native)==native,"native files need no temp copy");
            overhaul::ReleaseDiskFile(native);
            Check(fs::exists(native),"cleanup never deletes a native file");
            Check(overhaul::DiskFile(game/"missing").empty(),"missing media returns an empty path");
        } else {
            Check(data.empty() && overhaul::Text(file).empty() && overhaul::DiskFile(file).empty(),"disabled text/video helpers cannot read loose overhaul files");
        }
        auto record=kernel::mods::SaveRecord();
        if(expected)Check(record.find("\"affectsSaves\": false")!=std::string::npos,"UI mod is recorded as not affecting saves");
        return 0;
    } catch(const std::exception& e) { std::fprintf(stderr,"%s\n",e.what());return 1; }
}
