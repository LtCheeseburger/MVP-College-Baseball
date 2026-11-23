#include "common/imported_classes.h"
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <iterator>
#include <algorithm>
int main(int argc,char** argv){
    const std::string original="0 first_name,9 year,;\r\n090000001,0 First,1 Graduate,9 3,10 5,;\r\n090000002,0 Other,9 2,10 5,;\r\n";
    std::vector<uint8_t> bytes(original.begin(),original.end());
    if(!game::RestoreGraduateClasses(bytes,"key,class\r\n90000001,4\r\n"))throw std::runtime_error("Graduate not restored");
    const std::string patched(bytes.begin(),bytes.end());
    if(patched.find("Graduate,9 4,10 5") == patched.npos||patched.find("Other,9 2,10 5")==patched.npos)throw std::runtime_error("Class patch touched another field");
    const auto once=bytes;
    if(game::RestoreGraduateClasses(bytes,"key,class\n90000001,4\n")||bytes!=once)throw std::runtime_error("Repeated overlay changed data");
    for(const auto* ledger:{"key,class\n90000001,4\n90000003,4\n","key,class\n90000001,4\n90000001,4\n","key,class\n90000001,5\n","key,class\nnot-a-key,4\n"}){
        bytes.assign(original.begin(),original.end());
        if(game::RestoreGraduateClasses(bytes,ledger)||std::string(bytes.begin(),bytes.end())!=original)throw std::runtime_error("Malformed/stale overlay was not atomic");
    }
    if(argc==3){
        std::ifstream tableFile(argv[1],std::ios::binary),ledgerFile(argv[2],std::ios::binary);
        if(!tableFile||!ledgerFile)throw std::runtime_error("Full-roster test inputs are missing");
        std::string table((std::istreambuf_iterator<char>(tableFile)),{}),ledger((std::istreambuf_iterator<char>(ledgerFile)),{});
        bytes.assign(table.begin(),table.end());
        if(!game::RestoreGraduateClasses(bytes,ledger)||bytes.size()!=table.size())throw std::runtime_error("Full-roster overlay failed");
        size_t changes=0;
        for(size_t i=0;i<bytes.size();++i)if(bytes[i]!=uint8_t(table[i])){
            if(table[i]!='3'||bytes[i]!='4'||i<3||table.substr(i-3,3)!=",9 ")throw std::runtime_error("Full-roster overlay changed a non-class byte");
            ++changes;
        }
        if(changes!=341)throw std::runtime_error("Full-roster graduate population differs");
        std::cout<<"PASS real 7515-player table: exactly 341 graduate class bytes restored, all other bytes preserved\n";
    }
    std::cout<<"PASS graduate restoration, other-field preservation, idempotence and atomic rejection of stale/invalid/duplicate keys\n";
}
