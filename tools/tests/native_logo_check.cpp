#include "kernel/mods.h"
#include "kernel/modvfs.h"
#include "kernel/xsh.h"
#include "core/log.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <fstream>
#include <stdexcept>
namespace fs=std::filesystem;
namespace logx {void Write(Level,const char* fmt,...){va_list a;va_start(a,fmt);std::vprintf(fmt,a);va_end(a);std::puts("");}}
static unsigned U16(const uint8_t* p){return p[0]|unsigned(p[1])<<8;}
static unsigned U32(const uint8_t* p){return U16(p)|U16(p+2)<<16;}
int main(int argc,char** argv){try{
 if(argc!=5)return 2;auto game=fs::absolute(argv[1]);auto out=fs::absolute(argv[4]);fs::create_directories(out);
 kernel::mods::Init(game,out,{fs::absolute(argv[2])},false,"2f57040bab2c8b2eb33ebc7aa93209ad30dd103f");
 std::ifstream names(argv[3]);std::string name;uint64_t total=0;int count=0;
 while(std::getline(names,name)){
  if(!name.empty()&&name.back()=='\r')name.pop_back();if(name.empty())continue;
  auto folder=game/"data/frontend/logos_big"/name;
  std::vector<uint8_t> bytes;uint64_t size;
  if(!kernel::xsh::Size(folder,size)||!kernel::xsh::Build(folder,bytes)||size!=bytes.size()||std::memcmp(bytes.data(),"SHPX",4))throw std::runtime_error("native rebuild failed: "+name);
  if(U32(bytes.data()+8)!=6)throw std::runtime_error("native slot count changed");
  std::ofstream f(out/name,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());
  total+=bytes.size();count++;
 }
 auto view=kernel::modvfs::OpenLoose(game/"data/frontend/logos.big");if(!view)throw std::runtime_error("no virtual native archive");
 std::vector<uint8_t> big(view->Size());if(view->Read(0,big.data(),big.size())!=big.size())throw std::runtime_error("short virtual BIG read");
 std::ofstream f(out/"logos.big",std::ios::binary);f.write(reinterpret_cast<const char*>(big.data()),big.size());
 std::printf("PASS %d native folders rebuilt; XSH total=%llu virtual BIG=%llu\n",count,(unsigned long long)total,(unsigned long long)big.size());return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
