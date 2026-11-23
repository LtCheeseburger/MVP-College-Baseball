#pragma once
#include <charconv>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace game {
// Overlay source graduate classes only while the five-class feature is active.
// The stock-compatible database remains unchanged on disk and when it is off.
inline bool RestoreGraduateClasses(std::vector<uint8_t>& bytes,std::string_view ledger) {
    std::map<uint32_t,bool> graduates;
    while(!ledger.empty()) {
        auto end=ledger.find('\n');auto line=ledger.substr(0,end);
        ledger=end==ledger.npos?std::string_view{}:ledger.substr(end+1);
        if(!line.empty()&&line.back()=='\r')line.remove_suffix(1);
        if(line.empty()||line=="key,class")continue;
        auto comma=line.find(',');uint32_t key=0,year=0;
        if(comma==line.npos)return false;
        auto k=std::from_chars(line.data(),line.data()+comma,key,16);
        auto y=std::from_chars(line.data()+comma+1,line.data()+line.size(),year);
        if(k.ec!=std::errc{}||k.ptr!=line.data()+comma||y.ec!=std::errc{}||y.ptr!=line.data()+line.size()||!key||year!=4||!graduates.emplace(key,false).second)return false;
    }
    if(graduates.empty())return false;
    std::string result(bytes.begin(),bytes.end());bool changed=false;
    for(size_t start=result.find('\n')+1;start&&start<result.size();) {
        auto end=result.find('\n',start);if(end==result.npos)end=result.size();
        auto comma=result.find(',',start);uint32_t key=0;
        if(comma<end) {
            auto parsed=std::from_chars(result.data()+start,result.data()+comma,key,16);
            if(parsed.ec==std::errc{}&&parsed.ptr==result.data()+comma)if(auto it=graduates.find(key);it!=graduates.end()) {
                if(it->second)return false;
                auto at=result.find(",9 ",comma);
                if(at>=end||at+5>=end||result[at+4]!=','||(result[at+3]!='3'&&result[at+3]!='4'))return false;
                changed|=result[at+3]!='4';result[at+3]='4';it->second=true;
            }
        }
        start=end==result.size()?end:end+1;
    }
    for(const auto& [key,seen]:graduates)if(!seen)return false;
    if(!changed)return false;
    bytes.assign(result.begin(),result.end());return true;
}
}
