#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

// Candidate only. Inputs are canonical teams and completed results at an
// explicit snapshot cutoff. No guest pointers, original ranking-cache values,
// or mutable process-global state are part of this calculation.
namespace mvp::qualification_model {
inline void Require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
inline constexpr int64_t kRpiScale=1000000000000LL;
struct Team {uint32_t id,key,division;std::string canonical;bool conference_eligible=true;};
struct Game {uint32_t a,b,ra,rb,key;int32_t day;int8_t site;}; // site: +1 a's park, -1 b's park, 0 neutral
struct Record {uint32_t games=0,points=0;}; // two points per win, one per tie
inline int Compare(Record a,Record b){
 if(!a.games||!b.games)return (a.games>0)-(b.games>0);
 const uint64_t x=uint64_t(a.points)*b.games,y=uint64_t(b.points)*a.games;return (x>y)-(x<y);
}
inline uint32_t Ror(uint32_t v,unsigned n){return (v>>n)|(v<<(32-n));}
inline std::array<uint8_t,32> Sha256(const std::string& input){
 constexpr uint32_t k[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
 std::vector<uint8_t> bytes(input.begin(),input.end());const uint64_t bits=uint64_t(bytes.size())*8;bytes.push_back(128);while(bytes.size()%64!=56)bytes.push_back(0);for(int i=7;i>=0;--i)bytes.push_back(uint8_t(bits>>(8*i)));
 uint32_t h[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
 for(size_t at=0;at<bytes.size();at+=64){uint32_t w[64];for(unsigned i=0;i<16;++i)w[i]=(uint32_t(bytes[at+4*i])<<24)|(uint32_t(bytes[at+4*i+1])<<16)|(uint32_t(bytes[at+4*i+2])<<8)|bytes[at+4*i+3];
  for(unsigned i=16;i<64;++i)w[i]=w[i-16]+(Ror(w[i-15],7)^Ror(w[i-15],18)^(w[i-15]>>3))+w[i-7]+(Ror(w[i-2],17)^Ror(w[i-2],19)^(w[i-2]>>10));
  uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
  for(unsigned i=0;i<64;++i){const uint32_t t=z+(Ror(e,6)^Ror(e,11)^Ror(e,25))+((e&f)^(~e&g))+k[i]+w[i],u=(Ror(a,2)^Ror(a,13)^Ror(a,22))+((a&b)^(a&c)^(b&c));z=g;g=f;f=e;e=d+t;d=c;c=b;b=a;a=t+u;}
  h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
 }
 std::array<uint8_t,32> out{};for(unsigned i=0;i<32;++i)out[i]=uint8_t(h[i/4]>>(24-8*(i%4)));return out;
}
struct Snapshot {
 std::vector<Team> teams;
 std::vector<Game> games;
 std::array<int,512> index{};
 std::vector<Record> total,conference,pairs,conference_pairs;
 std::vector<int64_t> rpi,wp,owp,oowp;
 Snapshot(std::vector<Team> ts,std::vector<Game> gs,int32_t cutoff):teams(std::move(ts)){
  index.fill(-1);const auto n=teams.size();Require(n>0&&n<=512,"Qualification population outside capacity");
  for(size_t i=0;i<n;++i){const auto& t=teams[i];Require(t.id<512&&t.id&&index[t.id]<0&&!t.canonical.empty(),"Qualification duplicate/invalid team");index[t.id]=int(i);}
  Require(gs.size()<=100000,"Qualification result population outside reviewed arithmetic bounds");
  total.resize(n);conference.resize(n);pairs.resize(n*n);conference_pairs.resize(n*n);rpi.resize(n);wp.resize(n);owp.resize(n);oowp.resize(n);std::vector<int64_t> weighted_points(n),weighted_games(n);
  for(const auto& g:gs){Require(g.a<512&&g.b<512&&g.a!=g.b&&index[g.a]>=0&&index[g.b]>=0&&g.site>=-1&&g.site<=1,"Qualification invalid result identity/site");if(g.day>=cutoff)continue;games.push_back(g);
   const size_t a=index[g.a],b=index[g.b];const unsigned p=g.ra>g.rb?2:g.ra==g.rb?1:0;
   for(unsigned side=0;side<2;++side){const auto i=side?b:a,j=side?a:b;const auto points=side?2-p:p;++total[i].games;total[i].points+=points;++pairs[i*n+j].games;pairs[i*n+j].points+=points;
    if(g.key){Require(teams[i].key==g.key&&teams[j].key==g.key,"Conference result has foreign member");++conference[i].games;conference[i].points+=points;++conference_pairs[i*n+j].games;conference_pairs[i*n+j].points+=points;}
    // 0.7 home win/road loss, 1.3 road win/home loss, 1.0 neutral.
    // Ties are explicitly authored as half a win and half a loss.
    const int site=side?-g.site:g.site,w=site==1?7:site==-1?13:10,l=20-w;
    weighted_points[i]+=points*w;weighted_games[i]+=points*w+(2-points)*l;
   }
  }
  for(size_t i=0;i<n;++i){wp[i]=weighted_games[i]?weighted_points[i]*kRpiScale/weighted_games[i]:kRpiScale/2;int64_t sum=0;
   for(size_t j=0;j<n;++j)if(pairs[i*n+j].games){const auto removed=pairs[j*n+i],all=total[j];const auto count=all.games-removed.games,points=all.points-removed.points;const auto value=count?int64_t(points)*kRpiScale/(2*count):kRpiScale/2;sum+=int64_t(pairs[i*n+j].games)*value;}
   owp[i]=total[i].games?sum/total[i].games:kRpiScale/2;
  }
  for(size_t i=0;i<n;++i){int64_t sum=0;for(size_t j=0;j<n;++j)sum+=int64_t(pairs[i*n+j].games)*owp[j];oowp[i]=total[i].games?sum/total[i].games:kRpiScale/2;rpi[i]=(wp[i]+2*owp[i]+oowp[i])/4;}
 }
 Record Against(size_t i,const std::vector<size_t>& opponents)const{Record out;const auto n=teams.size();for(auto j:opponents){out.games+=conference_pairs[i*n+j].games;out.points+=conference_pairs[i*n+j].points;}return out;}
};

// Authored deterministic lot encoding: compact JSON array containing season,
// canonical conference, stage, sorted tied canonical IDs, then this team ID.
inline std::array<uint8_t,32> Lot(const Snapshot& s,const std::vector<size_t>& tied,size_t team,unsigned season,const std::string& conf,const std::string& stage){
 std::vector<std::string> ids;for(auto i:tied)ids.push_back(s.teams[i].canonical);std::sort(ids.begin(),ids.end());
 std::string material="["+std::to_string(season)+",\""+conf+"\",\""+stage+"\",[";for(size_t i=0;i<ids.size();++i){if(i)material+=',';material+='"'+ids[i]+'"';}material+="],\""+s.teams[team].canonical+"\"]";return Sha256(material);
}
enum class TiePolicy {CaaCommon,AsunRpi};
struct Qualifier {
 const Snapshot& s;unsigned season;std::string conference,stage="conference-qualification";TiePolicy policy;
 std::vector<size_t> members;
 std::vector<std::vector<size_t>> ranked_groups;
 Qualifier(const Snapshot& snap,unsigned year,std::string conf,uint32_t key,TiePolicy p):s(snap),season(year),conference(std::move(conf)),policy(p){
  for(size_t i=0;i<s.teams.size();++i)if(s.teams[i].key==key)members.push_back(i);
  Require(!members.empty(),"Qualification conference missing");auto sorted=members;std::sort(sorted.begin(),sorted.end(),[&](size_t a,size_t b){return Compare(s.conference[a],s.conference[b])>0;});
  for(auto i:sorted){if(ranked_groups.empty()||Compare(s.conference[ranked_groups.back()[0]],s.conference[i]))ranked_groups.push_back({});ranked_groups.back().push_back(i);}
 }
 std::vector<size_t> Resolve(std::vector<size_t> tied)const{
  if(tied.size()<2)return tied;
  std::vector<std::vector<Record>> criteria;
  std::vector<Record> mini;bool complete=true;for(auto i:tied){mini.push_back(s.Against(i,tied));for(auto j:tied)if(i!=j&&!s.conference_pairs[i*s.teams.size()+j].games)complete=false;}
  bool usable=true;if(policy==TiePolicy::CaaCommon)usable=tied.size()==2||complete;
  else {for(const auto r:mini)if(r.games!=mini.front().games)usable=false;}
  if(usable)criteria.push_back(mini);
  if(policy==TiePolicy::CaaCommon){for(const auto& ranked:ranked_groups){std::vector<size_t> common;for(auto j:ranked){if(std::find(tied.begin(),tied.end(),j)!=tied.end())continue;bool all=true;for(auto i:tied)all=all&&s.conference_pairs[i*s.teams.size()+j].games>0;if(all)common.push_back(j);}if(common.empty())continue;std::vector<Record> metric;for(auto i:tied)metric.push_back(s.Against(i,common));criteria.push_back(std::move(metric));}}
  for(const auto& metric:criteria){if(std::any_of(metric.begin(),metric.end(),[](Record r){return !r.games;}))continue;
   auto order=tied;std::sort(order.begin(),order.end(),[&](size_t a,size_t b){return Compare(metric[std::find(tied.begin(),tied.end(),a)-tied.begin()],metric[std::find(tied.begin(),tied.end(),b)-tied.begin()])>0;});
   std::vector<std::vector<size_t>> groups;for(auto i:order){const auto value=metric[std::find(tied.begin(),tied.end(),i)-tied.begin()];if(groups.empty()||Compare(value,metric[std::find(tied.begin(),tied.end(),groups.back()[0])-tied.begin()]))groups.push_back({});groups.back().push_back(i);}
   if(groups.size()>1){std::vector<size_t> out;for(auto& g:groups){auto v=Resolve(g);out.insert(out.end(),v.begin(),v.end());}return out;}
  }
  if(policy==TiePolicy::AsunRpi)return RpiOrder(tied);
  const auto pool=tied;std::sort(tied.begin(),tied.end(),[&](size_t a,size_t b){return Lot(s,pool,a,season,conference,stage)<Lot(s,pool,b,season,conference,stage);});return tied;
 }
 std::vector<size_t> RpiOrder(std::vector<size_t> teams)const{
  // A game-free team cannot silently satisfy a required selection snapshot.
  for(auto i:teams)Require(s.total[i].games>0,"Qualification requires populated RPI snapshot");
  const auto pool=teams;
  std::sort(teams.begin(),teams.end(),[&](size_t a,size_t b){if(s.rpi[a]!=s.rpi[b])return s.rpi[a]>s.rpi[b];std::vector<size_t> equal;for(auto i:pool)if(s.rpi[i]==s.rpi[a])equal.push_back(i);return Lot(s,equal,a,season,conference,stage)<Lot(s,equal,b,season,conference,stage);});return teams;
 }
 std::vector<size_t> Standings(std::vector<size_t> teams)const{
  std::sort(teams.begin(),teams.end(),[&](size_t a,size_t b){return Compare(s.conference[a],s.conference[b])>0;});std::vector<size_t> out;
  for(size_t first=0;first<teams.size();){size_t last=first+1;while(last<teams.size()&&!Compare(s.conference[teams[first]],s.conference[teams[last]]))++last;auto part=Resolve({teams.begin()+first,teams.begin()+last});out.insert(out.end(),part.begin(),part.end());first=last;}return out;
 }
 std::vector<uint32_t> Field(unsigned automatic_per_division,unsigned wildcards)const{
  Require(automatic_per_division*2+wildcards==6,"Unsupported divided field size");std::vector<size_t> divisions[2];for(auto i:members){Require(s.teams[i].division<2,"Missing authored division");if(s.teams[i].conference_eligible)divisions[s.teams[i].division].push_back(i);}
  for(auto& d:divisions)d=Standings(d);std::vector<size_t> selected;
  for(unsigned place=0;place<automatic_per_division;++place){std::vector<size_t> tier;for(const auto& d:divisions)if(place<d.size())tier.push_back(d[place]);tier=RpiOrder(tier);selected.insert(selected.end(),tier.begin(),tier.end());}
  std::vector<size_t> remaining;for(auto i:members)if(s.teams[i].conference_eligible&&std::find(selected.begin(),selected.end(),i)==selected.end())remaining.push_back(i);remaining=RpiOrder(remaining);
  for(auto i:remaining){if(selected.size()==6)break;selected.push_back(i);}Require(selected.size()==6,"Divisional field needs explicit reduced-bracket support");
  std::vector<uint32_t> ids;for(auto i:selected)ids.push_back(s.teams[i].id);return ids;
 }
};
}
