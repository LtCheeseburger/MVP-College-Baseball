#include "calendar_layout.h"
#include "game/expansion.h"
#include "kernel/modvfs.h"
#include <algorithm>
#include <sstream>
#include <string>

namespace {
size_t Indent(const std::string& line){return line.find_first_not_of(' ');}
std::string Position(std::string line,int top,int shift){
    const auto first=Indent(line);
    if(first==std::string::npos)return line;
    const auto colon=line.find(':',first);
    if(colon==std::string::npos)return line;
    const auto kind=line.substr(first,colon-first);
    if(kind!="GR"&&kind!="SH"&&kind!="SL"&&kind!="BU"&&kind!="TX"&&kind!="TL"&&kind!="TB")return line;
    std::vector<std::string> fields;std::istringstream in(line);std::string field;
    while(std::getline(in,field,','))fields.push_back(field);
    if(fields.size()<17)return line;
    fields[4]=std::to_string(top+(std::stoi(fields[4])+shift-top)*5/6);
    fields[6]=std::to_string(std::max(1,std::stoi(fields[6])*5/6));
    std::string result;
    for(size_t i=0;i<fields.size();++i){if(i)result+=',';result+=fields[i];}
    if(line.ends_with(','))result+=',';
    return result;
}
bool Apply(std::vector<uint8_t>& data){return expansion::Active()&&game::ExpandCalendarLayout(data);}
const bool registered=[] {
    kernel::modvfs::AddPatch("data/frontend/frontend.big","fes_calendarbase.fel",Apply);
    kernel::modvfs::AddPatch("data/frontend/frontend.big","fes_calendargame.fel",Apply);
    kernel::modvfs::AddPatch("data/frontend/frontend.big","fes_calendarpromo.fel",Apply);
    return true;
}();
}

bool game::ExpandCalendarLayout(std::vector<uint8_t>& data){
    const std::string text(data.begin(),data.end());
    const std::string eol=text.find("\r\n")!=std::string::npos?"\r\n":"\n";
    std::vector<std::string> lines;std::istringstream stream(text);std::string line;
    while(std::getline(stream,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();lines.push_back(line);}
    unsigned grids=0,clones=0;
    for(size_t i=0;i<lines.size();++i){
        const auto first=Indent(lines[i]);
        if(first==std::string::npos)continue;
        const auto name=lines[i].substr(first);
        if(!name.starts_with("GG:CELL,")&&!name.starts_with("GG:CELLHIGHLIGHTS,")&&!name.starts_with("GG:PROMOGRID,"))continue;
        const auto dimensions=lines[i].find(",-1,7,5,7,5,");
        if(dimensions==std::string::npos)return false;
        lines[i].replace(dimensions,std::string(",-1,7,5,7,5,").size(),",-1,7,6,7,6,");
        std::istringstream header(lines[i]);std::string field;
        for(unsigned n=0;n<5;++n)std::getline(header,field,',');
        const int top=std::stoi(field);
        ++grids;
        for(size_t j=i+1;j<lines.size()&&Indent(lines[j])>first;++j){
            const auto start=Indent(lines[j]);
            if(start==std::string::npos||!lines[j].substr(start).starts_with("GR:X"))continue;
            size_t end=j+1;while(end<lines.size()&&Indent(lines[end])>start)++end;
            const bool last=lines[j].find("Y4,")!=std::string::npos;
            std::vector<std::string> extra;
            if(last){
                for(size_t k=j;k<end;++k)extra.push_back(Position(lines[k],top,54));
                extra[0].replace(extra[0].find("Y4,"),3,"Y5,");++clones;
            }
            for(size_t k=j;k<end;++k)lines[k]=Position(lines[k],top,0);
            lines.insert(lines.begin()+end,extra.begin(),extra.end());
            j=end+extra.size()-1;
        }
    }
    if(!grids||clones!=7*grids)return false;
    std::string output;for(const auto& value:lines)output+=value+eol;
    data.assign(output.begin(),output.end());return true;
}
