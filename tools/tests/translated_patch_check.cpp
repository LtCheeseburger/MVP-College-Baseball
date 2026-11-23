#include "app/translated_patches.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace fs=std::filesystem;
void Write(const fs::path& p,const std::string& s){std::ofstream(p,std::ios::binary)<<s;}
std::string Read(const fs::path& p){std::ifstream f(p,std::ios::binary);return {(std::istreambuf_iterator<char>(f)),{}};}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    auto root=fs::path(argv[1]);fs::create_directories(root/"gen");fs::create_directories(root/"patches");
    auto file=root/"gen/x86_recomp.0.cpp",manifest=root/"patches/test.json";
    const std::string original="X86_FUNC(__imp__sub_11111111) {\n\t// 11111111 synthetic\n\tctx.eax = 7;\n}\n";
    auto hash=translated_patches::Sha256("\tctx.eax = 7;\n");
    auto json=[&](std::string digest,int count){return "{\"format\":1,\"condition\":\"test_active()\",\"includes\":[],\"edits\":[{\"at\":\"11111111\",\"old\":\"7\",\"new\":\"9\",\"functions\":[{\"function\":\"11111111\",\"translation_sha256\":\""+digest+"\",\"occurrences\":"+std::to_string(count)+"}]}]}";};
    Write(file,original);Write(manifest,"{\"format\":1,\"edits\":[]}");
    translated_patches::Apply(root/"gen",root/"patches");
    if(Read(file)!=original)throw std::runtime_error("empty manifest changed code");
    for(auto test:{json(std::string(64,'0'),1),json(hash,2)}) {
        Write(manifest,test);bool rejected=false;
        try{translated_patches::Apply(root/"gen",root/"patches");}catch(const std::exception&){rejected=true;}
        if(!rejected || Read(file)!=original)throw std::runtime_error("bad hash/count accepted or changed source");
    }
    Write(manifest,json(hash,1));auto revision=translated_patches::Revision(root/"patches");
    translated_patches::Apply(root/"gen",root/"patches");auto result=Read(file);
    if(result.find("ctx.eax = 9;")==std::string::npos || result.find("ctx.eax = 7;")==std::string::npos || result.find("if (test_active()) return sub_11111111_exp")==std::string::npos)throw std::runtime_error("missing clone/stock dispatch");
    translated_patches::Apply(root/"gen",root/"patches");if(Read(file)!=result)throw std::runtime_error("not idempotent");
    Write(manifest,json(hash,1)+"\n");if(translated_patches::Revision(root/"patches")==revision)throw std::runtime_error("content revision not changed");
    bool rejected=false;try{translated_patches::Apply(root/"gen",root/"patches");}catch(const std::exception&){rejected=true;}
    if(!rejected || Read(file)!=result)throw std::runtime_error("stale patched source accepted");
    Write(file,original);Write(manifest,"{\"format\":1,\"condition\":\"test_active()\",\"includes\":[],\"edits\":[{\"at\":\"11111111\",\"before\":\"ctx.ebx = 42;\",\"functions\":[{\"function\":\"11111111\",\"translation_sha256\":\""+hash+"\",\"occurrences\":1}]}]}");
    translated_patches::Apply(root/"gen",root/"patches");auto prefixed=Read(file);if(prefixed.find("ctx.ebx = 42;")>prefixed.find("ctx.eax = 7;")||prefixed.find("ctx.ebx = 42;",prefixed.find("ctx.ebx = 42;")+1)!=std::string::npos)throw std::runtime_error("before insertion leaked into original or wrong position");
    Write(file,original);Write(manifest,"{\"format\":1,\"condition\":\"test_active()\",\"includes\":[],\"edits\":[{\"at\":\"11111111\",\"replacement\":\"ctx.eax = 99;\",\"guard\":\"optional_feature()\",\"functions\":[{\"function\":\"11111111\",\"translation_sha256\":\""+hash+"\",\"occurrences\":1}]}]}");
    translated_patches::Apply(root/"gen",root/"patches");auto guarded=Read(file);
    if(guarded.find("if (optional_feature())")==std::string::npos||guarded.find("ctx.eax = 99;")==std::string::npos||guarded.find("ctx.eax = 7;",guarded.find("ctx.eax = 7;")+1)==std::string::npos)throw std::runtime_error("Guarded replacement lost local original fallback");
    std::cout<<"PASS empty no-op, bad hash/count atomic rejection, DLL clone + stock dispatch, idempotency, content revision, stale-output rejection, before insertion and guarded authored replacement\n";
}
