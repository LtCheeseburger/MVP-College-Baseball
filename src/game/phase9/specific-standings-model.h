#pragma once
#include "competition-qualification-model.h"
#include <memory>
#include <tuple>

// Separate candidate: the fifteen shared-policy and CAA/ASUN models remain
// unchanged. Inputs carry completed results and explicit snapshot cutoffs.
namespace mvp::specific_standings {
using qualification_model::Require;
using qualification_model::Team;
struct Result {uint32_t a,b,ra,rb,key;int32_t day;int8_t site;uint32_t minute,serial;};
struct Metric {int64_t n=0,d=1,extra=0;bool valid=false;};
inline Metric Number(int64_t n){return {n,1,0,true};}
inline int Compare(Metric a,Metric b){
 if(a.valid!=b.valid)return int(a.valid)-int(b.valid);if(!a.valid)return 0;
 const auto x=a.n*b.d,y=b.n*a.d;if(x!=y)return (x>y)-(x<y);
 return (a.extra>b.extra)-(a.extra<b.extra);
}
inline bool Supported(uint32_t key){switch(key){case 1:case 4:case 10:case 11:case 12:case 16:case 17:case 21:case 22:case 23:case 24:case 26:case 29:return true;default:return false;}}
struct Row {size_t other;uint32_t points,rf,ra;int32_t day;uint32_t minute,serial;};
using Group=std::vector<size_t>;
struct Resolver {
 const std::vector<Team>& teams;const std::vector<Result>& games;uint32_t key,year;std::string canonical;
 int32_t rpi_cutoff,nonconference_cutoff;bool provisional;unsigned series_games;
 std::array<int,512> index{};Group members;std::vector<std::vector<Row>> results,nonconference;
 std::vector<Metric> primary;std::vector<Group> ranked;mutable std::unique_ptr<qualification_model::Snapshot> rpi;
 Resolver(const std::vector<Team>& ts,const std::vector<Result>& gs,uint32_t k,uint32_t season,std::string name,
          int32_t rpi_day,int32_t outside_day,bool partial=false,unsigned series=3):
  teams(ts),games(gs),key(k),year(season),canonical(std::move(name)),rpi_cutoff(rpi_day),
  nonconference_cutoff(outside_day),provisional(partial),series_games(series){
  Require(Supported(key)&&!teams.empty()&&teams.size()<=512&&games.size()<=100000&&series_games>0&&series_games<=10,"Specific standings: invalid policy/population");
  index.fill(-1);results.resize(teams.size());nonconference.resize(teams.size());primary.resize(teams.size());
  for(size_t i=0;i<teams.size();++i){const auto& t=teams[i];Require(t.id&&t.id<512&&index[t.id]<0&&!t.canonical.empty(),"Specific standings: team identity");index[t.id]=int(i);if(t.key==key)members.push_back(i);}
  Require(!members.empty()&&members.size()<=40,"Specific standings: conference size");
  for(const auto& g:games){Require(g.a<512&&g.b<512&&g.a!=g.b&&index[g.a]>=0&&index[g.b]>=0&&g.ra<=255&&g.rb<=255&&g.site>=-1&&g.site<=1,"Specific standings: invalid result");
   const auto a=size_t(index[g.a]),b=size_t(index[g.b]);Require(!g.key||(teams[a].key==g.key&&teams[b].key==g.key),"Specific standings: foreign conference result");
   const unsigned points=g.ra>g.rb?2:g.ra==g.rb?1:0;
   for(unsigned side=0;side<2;++side){const auto t=side?b:a,o=side?a:b;Row row{o,side?2-points:points,side?g.rb:g.ra,side?g.ra:g.rb,g.day,g.minute,g.serial};
    if(g.key==key)results[t].push_back(row);
    else if(!g.key&&g.day<nonconference_cutoff&&teams[t].key==key&&teams[o].key!=key)nonconference[t].push_back(row);
   }
  }
  for(auto t:members)primary[t]=Percentage(t,members);
  ranked=Partition(members,primary);
  if(key==22){std::vector<Group> split;for(const auto& group:ranked){auto parts=HeadGroups(group);split.insert(split.end(),parts.begin(),parts.end());}ranked=std::move(split);}
 }
 static bool Has(const Group& group,size_t value){return std::find(group.begin(),group.end(),value)!=group.end();}
 std::vector<Row> Against(size_t t,const Group& pool,bool outside=false)const{
  std::vector<Row> out;for(const auto& r:(outside?nonconference[t]:results[t]))if(Has(pool,r.other))out.push_back(r);return out;
 }
 Metric Percentage(size_t t,const Group& pool,bool zero=false,bool extreme=false)const{
  uint64_t points=0,count=0;for(const auto& r:results[t])if(Has(pool,r.other)){points+=r.points;++count;}
  if(!count)return zero?Number(0):Metric{};
  return {int64_t(points),int64_t(2*count),extreme?(points==2*count?int64_t(count):points==0?-int64_t(count):0):0,true};
 }
 std::vector<Group> Partition(Group values,const std::vector<Metric>& metric)const{
  std::sort(values.begin(),values.end(),[&](auto a,auto b){return Compare(metric[a],metric[b])>0;});
  std::vector<Group> out;for(auto t:values){if(out.empty()||Compare(metric[t],metric[out.back()[0]]))out.push_back({});out.back().push_back(t);}return out;
 }
 std::vector<Group> HeadGroups(const Group& group)const{
  if(group.size()<2)return {group};std::vector<Metric> values(teams.size());
  for(auto t:group){values[t]=Percentage(t,group);if(!values[t].valid)return {group};}
  auto parts=Partition(group,values);if(parts.size()==1)return parts;
  std::vector<Group> out;for(const auto& part:parts){auto sub=HeadGroups(part);out.insert(out.end(),sub.begin(),sub.end());}return out;
 }
 Group Draw(Group tied)const{
  // Reuse only the previously proved byte-level deterministic lot encoding.
  qualification_model::Snapshot lottery(teams,{},0);const auto pool=tied;
  std::sort(tied.begin(),tied.end(),[&](auto a,auto b){return qualification_model::Lot(lottery,pool,a,year,canonical,"conference-qualification")<qualification_model::Lot(lottery,pool,b,year,canonical,"conference-qualification");});return tied;
 }
 Group Resolve(Group tied,const Group* seeding_pool=nullptr,bool start_common=false)const{
  if(tied.size()<2)return tied;const bool multi=tied.size()>2;
  bool complete=true;Group common;
  for(auto t:tied)for(auto o:tied)if(t!=o&&Against(t,{o}).empty())complete=false;
  for(auto o:members)if(!Has(tied,o)){bool all=true;for(auto t:tied)all=all&&!Against(t,{o}).empty();if(all)common.push_back(o);}
  struct Criterion {std::vector<Metric> values;bool common_restart=false;};std::vector<Criterion> metrics;
  auto pct=[&](const Group& pool,bool zero=false,bool extreme=false,bool restart=false){std::vector<Metric> v(teams.size());for(auto t:tied)v[t]=Percentage(t,pool,zero,extreme);metrics.push_back({std::move(v),restart});};
  auto wins=[&](const Group& pool){std::vector<Metric> v(teams.size());for(auto t:tied){const auto rows=Against(t,pool);if(!rows.empty()){int n=0;for(const auto& r:rows)n+=r.points==2;v[t]=Number(n);}}metrics.push_back({std::move(v)});};
  auto runs=[&](const Group& pool,int cap=0,int mode=0,bool prefix=false){std::vector<std::vector<Row>> rows(teams.size());size_t count=SIZE_MAX;
   for(auto t:tied){rows[t]=Against(t,pool);std::sort(rows[t].begin(),rows[t].end(),[](const auto& a,const auto& b){return std::tie(a.day,a.minute,a.serial)<std::tie(b.day,b.minute,b.serial);});count=std::min(count,rows[t].size());}
   std::vector<Metric> v(teams.size());for(auto t:tied){const auto n=prefix?count:rows[t].size();if(!n)continue;int64_t value=0;for(size_t i=0;i<n;++i){const auto& r=rows[t][i];if(mode==1)value+=cap?std::min<int>(cap,r.rf):r.rf;else if(mode==2)value-=cap?std::min<int>(cap,r.ra):r.ra;else{const int diff=int(r.rf)-int(r.ra);value+=cap?std::clamp(diff,-cap,cap):diff;}}v[t]=Number(value);}metrics.push_back({std::move(v)});
  };
  auto ranked_metric=[&](bool common_only,bool count_wins=false,bool extreme=false){for(const auto& group:ranked){Group pool;for(auto o:group)if(!Has(tied,o)&&(!seeding_pool||Has(*seeding_pool,o))&&(!common_only||Has(common,o)))pool.push_back(o);if(pool.empty())continue;
   if(count_wins)wins(pool);else pct(pool,key==12,extreme,key==23&&!complete);}};
  if(!start_common){
   if(key==10||key==26)wins(tied);
   else if(multi&&(key==1||key==4)&&!complete){std::vector<Metric> v(teams.size());for(auto t:tied){bool winner=true,loser=true;
     for(auto o:tied)if(o!=t){const auto r=Percentage(t,{o});winner=winner&&r.valid&&r.n*2>r.d;loser=loser&&r.valid&&r.n*2<r.d;}
     int advantage=0;if(key==4)advantage=winner;
     else if(winner||loser){const auto current=Percentage(t,tied);bool dominates=true;
      for(auto o:tied)if(o!=t){int64_t points=0,count=0,missing=0;for(const auto& row:Against(o,tied)){points+=row.points;++count;}for(auto z:tied)if(z!=o&&Against(o,{z}).empty())missing+=series_games;
       const Metric possible{points+(winner?2*missing:0),2*(count+missing),0,true};dominates=dominates&&(winner?Compare(current,possible)>0:Compare(current,possible)<0);}
      if(dominates)advantage=winner?1:-1;
     }v[t]=Number(advantage);
    }metrics.push_back({std::move(v)});
   }else if(!multi||(key!=16&&key!=11)||complete)pct(tied,key==12);
   if(key==4&&(!multi||complete)){std::vector<Metric> v(teams.size());for(auto t:tied){int64_t points=0,count=0;for(auto o:tied)if(o!=t){auto rows=Against(t,{o});if(rows.empty())continue;const auto first=std::min_element(rows.begin(),rows.end(),[](const auto& a,const auto& b){return std::tie(a.day,a.minute,a.serial)<std::tie(b.day,b.minute,b.serial);});points+=first->points;++count;}if(count)v[t]={points,2*count,0,true};}metrics.push_back({std::move(v)});}
  }
  if(key==16||key==11)pct(common);
  if(key==17){Group higher;for(auto t:members)if(Compare(primary[t],primary[tied[0]])>0)higher.push_back(t);pct(higher);}
  if(key!=22||multi)ranked_metric(key==16||key==1||key==17||key==4||key==24||key==23||key==11,key==10||key==26,key==16);
  if(key==16){runs(tied,10);runs(common,10);for(const auto& group:ranked){Group pool;for(auto t:group)if(Has(common,t))pool.push_back(t);if(!pool.empty())runs(pool,10);}}
  else if(key==1){if(!multi)runs(tied);runs(common);runs(members);}
  else if(key==17){runs(members,3);runs(members,3,1);runs(members,3,2);}
  else if(key==22&&multi)runs(tied,8);
  else if(key==29){runs(tied,0,2,true);runs(members,0,2,true);}
  else if(key==11){std::vector<Metric> v(teams.size());for(auto t:tied){Group opponents;for(const auto& r:results[t])if(!Has(opponents,r.other))opponents.push_back(r.other);int64_t points=0,count=0;for(auto o:opponents)for(const auto& r:results[o]){points+=r.points;++count;}if(count)v[t]={points,2*count,0,true};}metrics.push_back({std::move(v)});}
  else if(key==10){Group equal;for(size_t o=0;o<teams.size();++o){size_t count=SIZE_MAX;bool all=true;for(auto t:tied){const auto n=Against(t,{o},true).size();all=all&&n>0&&(count==SIZE_MAX||count==n);count=n;}if(all)equal.push_back(o);}std::vector<Metric> v(teams.size());for(auto t:tied){int64_t points=0,count=0;for(const auto& r:Against(t,equal,true)){points+=r.points;++count;}if(count)v[t]={points,2*count,0,true};}metrics.push_back({std::move(v)});}
  for(const auto& metric:metrics){bool valid=true;for(auto t:tied)valid=valid&&metric.values[t].valid;if(!valid)continue;auto groups=Partition(tied,metric.values);if(groups.size()<2)continue;
   if(key==21&&multi){const auto winner=Resolve(groups.front(),seeding_pool).front();Group remaining;for(auto t:tied)if(t!=winner)remaining.push_back(t);auto rest=Resolve(remaining,seeding_pool);rest.insert(rest.begin(),winner);return rest;}
   Group out;for(const auto& group:groups){auto sub=Resolve(group,seeding_pool,metric.common_restart&&group.size()>2);out.insert(out.end(),sub.begin(),sub.end());}return out;
  }
  if(key==10||key==23||(key==22&&multi)){
   if(!rpi){std::vector<qualification_model::Game> gs;gs.reserve(games.size());for(const auto& g:games)gs.push_back({g.a,g.b,g.ra,g.rb,g.key,g.day,g.site});rpi=std::make_unique<qualification_model::Snapshot>(teams,std::move(gs),rpi_cutoff);}
   bool populated=true;for(auto t:tied)populated=populated&&rpi->total[t].games>0;
   Require(populated||provisional,"Specific standings: required populated RPI snapshot missing");
   if(populated){std::vector<Metric> v(teams.size());for(auto t:tied)v[t]=Number(rpi->rpi[t]);auto groups=Partition(tied,v);if(groups.size()>1){Group out;for(const auto& g:groups){auto sub=Resolve(g,seeding_pool);out.insert(out.end(),sub.begin(),sub.end());}return out;}}
  }
  return Draw(tied);
 }
 std::vector<uint32_t> Order(unsigned field_size=0)const{
  const auto groups=Partition(members,primary);Group ordered;for(const auto& group:groups){auto sub=Resolve(group);ordered.insert(ordered.end(),sub.begin(),sub.end());}
  if(key==26&&field_size){Require(field_size<=ordered.size(),"Specific standings: field size");Group selected(ordered.begin(),ordered.begin()+field_size),seeded;
   for(const auto& group:groups){Group part;for(auto t:group)if(Has(selected,t))part.push_back(t);auto sub=Resolve(part,&selected);seeded.insert(seeded.end(),sub.begin(),sub.end());}
   seeded.insert(seeded.end(),ordered.begin()+field_size,ordered.end());ordered=std::move(seeded);
  }
  std::vector<uint32_t> out;for(auto i:ordered)out.push_back(teams[i].id);return out;
 }
};
}
