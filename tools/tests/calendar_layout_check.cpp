#include "common/calendar_layout.cpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace expansion {bool Active(){return true;}}
namespace kernel::modvfs {void AddPatch(const char*,const char*,Patch){}}
int main(int argc,char** argv){
    if(argc!=2)return 2;
    std::ifstream in(argv[1],std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),{});
    if(!game::ExpandCalendarLayout(data))throw std::runtime_error("Calendar resource expansion failed");
    std::string text(data.begin(),data.end());
    for(unsigned x=0;x<7;++x){
        const auto name="GR:X"+std::to_string(x)+"Y5,";
        const auto first=text.find(name);
        if(first==std::string::npos)throw std::runtime_error("Sixth row is missing a day cell, highlight or promotion widget");
    }
    if(text.find(",-1,7,5,7,5,")!=std::string::npos||text.find(",-1,7,6,7,6,0,")==std::string::npos)
        throw std::runtime_error("Native navigation dimensions or subsequent flags differ");
    const auto once=data;
    if(game::ExpandCalendarLayout(data)||data!=once)throw std::runtime_error("Repeated resource patch changed the calendar");
    std::ofstream("../../artifacts/postseason-freeze/calendar-expanded.fel",std::ios::binary).write(text.data(),text.size());
    std::cout<<"PASS 42 native day/highlight cells, six-row grid dimensions, retained flags and idempotence\n";
}
