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
static unsigned BE(const uint8_t* p){return unsigned(p[0])<<24|unsigned(p[1])<<16|unsigned(p[2])<<8|p[3];}
static void Check(bool v,const char* msg){if(!v)throw std::runtime_error(msg);}
int main(int argc,char** argv){try{
 Check(argc==3,"arguments");auto game=fs::absolute(argv[1]),source=fs::absolute(argv[2]);fs::create_directories(game/"mods");
 for(auto archive:{"models","ghead","control"}){
  auto folder=game/"data"/(std::string(archive)+"_big");fs::create_directories(folder);
  for(auto name:{"a.xsh","b.xsh"})fs::copy(source,folder/name,fs::copy_options::recursive|fs::copy_options::overwrite_existing);
  if(std::string(archive)=="ghead")for(auto& e:fs::directory_iterator(folder/"b.xsh"))if(e.path().extension()==".dds"){
   std::ofstream f(e.path(),std::ios::binary|std::ios::app);const char padding[17]={};f.write(padding,sizeof padding);break;
  }
  std::ofstream(folder/"_big.json")<<R"({"align":4,"order":["a.xsh","b.xsh"],"refpack":["a.xsh","b.xsh"]})";
 }
 kernel::mods::Init(game,game/"mods",{},true,"");
 for(auto archive:{"models","ghead"}){
  auto folder=game/"data"/(std::string(archive)+"_big");std::vector<uint8_t> expected,expectedB;Check(kernel::xsh::Build(folder/"a.xsh",expected)&&kernel::xsh::Build(folder/"b.xsh",expectedB),"reference conversion");
  auto view=kernel::modvfs::OpenLoose(game/"data"/(std::string(archive)+".big"));Check(bool(view),"archive index");
  uint8_t h[16];Check(view->Read(0,h,16)==16,"header read");std::vector<uint8_t> head(BE(h+12));Check(view->Read(0,head.data(),head.size())==head.size(),"index read");
  unsigned a=BE(head.data()+16),as=BE(head.data()+20);size_t n=24;while(head[n])++n;++n;unsigned b=BE(head.data()+n),bs=BE(head.data()+n+4);
  Check(as==expected.size()&&bs>=expectedB.size(),"unpacked indexed bounds");
  if(std::string(archive)=="ghead")Check(bs>expectedB.size(),"padding fixture did not reserve a larger extent");
  expectedB.resize(bs,0);
  std::vector<fs::path> hidden;for(auto& e:fs::directory_iterator(folder/"b.xsh"))if(e.path().extension()==".dds"){hidden.push_back(e.path());fs::rename(e.path(),e.path().string()+".deferred");}
  std::vector<uint8_t> got(as);Check(view->Read(a,got.data(),got.size())==got.size()&&got==expected,"requested texture bytes differ");
  uint8_t byte;Check(view->Read(b,&byte,1)==0,"unrequested texture was eagerly materialized");
  for(auto& f:hidden)fs::rename(f.string()+".deferred",f);
  got.resize(bs);Check(view->Read(b,got.data(),got.size())==got.size()&&got==expectedB,"deferred retry bytes differ");
  std::vector<uint8_t> partial(137);Check(view->Read(a+31,partial.data(),partial.size())==partial.size()&&!std::memcmp(partial.data(),expected.data()+31,partial.size()),"partial cached read");
  std::printf("PASS %s: lazy index, exact XSH bytes with safe trailing padding, unavailable unrequested entry, retry and partial reads\n",archive);
 }
 auto other=kernel::modvfs::OpenLoose(game/"data/control.big");Check(bool(other),"control archive");uint8_t h[32];other->Read(0,h,32);uint8_t magic[4];other->Read(BE(h+16),magic,4);Check(std::memcmp(magic,"SHPX",4)!=0,"other archive unexpectedly served unpacked");
 std::puts("PASS other archive retains existing packing behavior");return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
