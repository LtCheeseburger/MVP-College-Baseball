#pragma once
#include "annual-conference-graphs.h"
#include "annual-schedule-calendar.h"
#include "annual-schedule-flow.h"
#include <numeric>
#include <cstdio>

namespace mvp::annual_schedule {
struct TeamInfo {Team id;std::string state;unsigned key,region,budget;};
struct ConferenceSeason {unsigned key;ConferenceGraph graph;Window window;bool saturday_only;};
enum class FixtureKind {Conference,Weekend,Midweek};
struct Fixture {int32_t day;unsigned minute,home,away,series;FixtureKind kind;unsigned key;int round;};
struct FixtureGenerator {
    unsigned year,attempt,maximum_pair_games;
    std::vector<TeamInfo> teams;
    std::vector<ConferenceSeason> conferences;
    std::map<Team,unsigned> index;
    std::map<unsigned,unsigned> conference_index;
    std::vector<unsigned> flow_conference_order;
    std::vector<int> left,home,away;
    std::vector<std::vector<std::pair<int32_t,unsigned>>> occupied;
    std::map<std::pair<unsigned,unsigned>,unsigned> pair_games;
    std::vector<Fixture> games;
    unsigned series=0;
    int32_t first,last;
    FixtureGenerator(unsigned y,unsigned seed,std::vector<TeamInfo> ts,std::vector<ConferenceSeason> cs,unsigned cap,std::vector<unsigned> flow_order={}):
        year(y),attempt(seed),maximum_pair_games(cap),teams(std::move(ts)),conferences(std::move(cs)),flow_conference_order(std::move(flow_order)) {
        GraphRequire(year>=2028&&year<=9998&&teams.size()==309&&conferences.size()==30&&cap>=3&&cap<=12,"Invalid annual fixture population/policy");
        first=conferences.front().window.first;last=first;
        for(unsigned i=0;i<conferences.size();++i){const auto& c=conferences[i];GraphRequire(conference_index.emplace(c.key,i).second&&c.window.first==first,"Duplicate conference or inconsistent first day");last=std::max(last,c.window.last);}
        if(flow_conference_order.empty())for(const auto& c:conferences)flow_conference_order.push_back(c.key);
        GraphRequire(flow_conference_order.size()==30&&std::set<unsigned>(flow_conference_order.begin(),flow_conference_order.end()).size()==30,"Invalid annual conference flow order");
        for(auto key:flow_conference_order)GraphRequire(conference_index.count(key),"Unknown conference in annual flow order");
        for(unsigned i=0;i<teams.size();++i){const auto& t=teams[i];GraphRequire(index.emplace(t.id,i).second&&conference_index.count(t.key)&&t.budget>0&&t.budget<=56&&t.budget%2==0,"Invalid annual team/budget");left.push_back(t.budget);}
        home.resize(teams.size());away.resize(teams.size());occupied.resize(teams.size());
    }
    const Window& Bounds(unsigned team)const{return conferences.at(conference_index.at(teams.at(team).key)).window;}
    static std::pair<unsigned,unsigned> PairIndex(unsigned a,unsigned b){return a<b?std::pair(a,b):std::pair(b,a);}
    std::string Iso(int32_t stamp)const {
        unsigned month=1;while(month<12&&stamp>=Day(year,month+1,1))++month;
        const auto d=stamp-Day(year,month,1)+1;char text[16];std::snprintf(text,sizeof(text),"%04u-%02u-%02d",year,month,d);return text;
    }
    std::array<uint8_t,32> Lot(std::initializer_list<std::string> parts)const {
        std::string text=std::to_string(year)+"|"+std::to_string(attempt);for(const auto& p:parts)text+="|"+p;return qualification_model::Sha256(text);
    }
    unsigned Geography(unsigned a,unsigned b)const {return teams[a].state==teams[b].state?0:teams[a].region==teams[b].region?1:2;}
    bool IsByu(unsigned t)const{return teams[t].id=="ncaa:77";}
    std::vector<std::pair<int32_t,unsigned>> Slots(int32_t friday,unsigned count,bool saturday,bool no_sunday)const {
        std::vector<std::pair<int,unsigned>> cadence;
        if(count==4||saturday)cadence={{1,2},{2,count-2}};
        else if(no_sunday)cadence=friday-1>=first?decltype(cadence){{-1,1},{0,1},{1,1}}:decltype(cadence){{0,1},{1,2}};
        else cadence={{0,1},{1,1},{2,1}};
        std::vector<std::pair<int32_t,unsigned>> result;
        for(const auto& [offset,n]:cadence)for(unsigned i=0;i<n;++i)result.push_back({friday+offset,n==2?(i?960:720):780});
        GraphRequire(result.size()==count,"Wrong annual series cadence");return result;
    }
    void Append(unsigned h,unsigned a,const std::vector<std::pair<int32_t,unsigned>>& slots,FixtureKind kind,unsigned key=0,int round=-1) {
        GraphRequire(h<teams.size()&&a<teams.size()&&h!=a,"Invalid annual fixture identities");++series;
        for(const auto& [day,minute]:slots){
            for(const auto t:{h,a}){GraphRequire(first<=day&&day<=Bounds(t).last,"Annual fixture crosses season boundary");
                GraphRequire(std::find(occupied[t].begin(),occupied[t].end(),std::pair(day,minute))==occupied[t].end(),"Annual fixture time conflict");
                occupied[t].push_back({day,minute});GraphRequire(--left[t]>=0,"Annual fixture budget exceeded");}
            ++home[h];++away[a];++pair_games[PairIndex(h,a)];games.push_back({day,minute,h,a,series,kind,key,round});
        }
    }
    void RecountHomes(){std::fill(home.begin(),home.end(),0);std::fill(away.begin(),away.end(),0);for(const auto& g:games){++home[g.home];++away[g.away];}}
    void ConferenceGames(){
        for(const auto& c:conferences){const auto start=c.window.last-2-7*int(c.graph.rounds-1);GraphRequire(start>=first,"Annual conference rounds do not fit season");
            for(const auto& s:c.graph.series){const auto h=index.at(s.home),a=index.at(s.away);GraphRequire(teams[h].key==c.key&&teams[a].key==c.key,"Conference series has foreign member");
                Append(h,a,Slots(start+7*s.round,s.games,c.saturday_only,IsByu(h)||IsByu(a)||(c.key==4&&s.round==c.graph.rounds-1)),FixtureKind::Conference,c.key,s.round);}
        }
    }
    void OrientWeekends(){
        std::map<unsigned,std::vector<unsigned>> groups;std::vector<int> conference_home(teams.size()),degree(teams.size());
        for(unsigned i=0;i<games.size();++i){const auto& g=games[i];if(g.kind==FixtureKind::Weekend)groups[g.series].push_back(i);else if(g.kind==FixtureKind::Conference)++conference_home[g.home];}
        for(const auto& [sid,rows]:groups){(void)sid;const auto& g=games[rows.front()];++degree[g.home];++degree[g.away];}
        FlowNetwork flow;std::vector<unsigned> nodes;std::vector<int> lower;std::vector<std::vector<FlowRef>> refs;
        for(unsigned t=0;t<teams.size();++t){nodes.push_back(flow.Node());refs.emplace_back();const int need=int(teams[t].budget/2)-conference_home[t];
            const auto low=std::max(0,(need-left[t]+2)/3),high=std::min(degree[t],need/3);GraphRequire(low<=high,"Annual weekend home budget infeasible");lower.push_back(low);
            for(int n=1;n<=high;++n){const int64_t before=2*(need-3*(n-1))-left[t],after=2*(need-3*n)-left[t];
                refs[t].push_back(flow.Add(nodes[t],1,1,after*after-before*before-(n<=low?1000000000LL:0)));}
        }
        std::map<unsigned,std::array<FlowRef,2>> choices;
        for(const auto& [sid,rows]:groups){GraphRequire(rows.size()==3,"Annual nonconference weekend has wrong length");const auto& g=games[rows.front()];const auto node=flow.Node();flow.Add(0,node,1);
            choices[sid]={flow.Add(node,nodes[g.home],1),flow.Add(node,nodes[g.away],1)};}
        GraphRequire(flow.MinimumCost(0,1).first==int(groups.size()),"Annual weekend home flow incomplete");
        for(unsigned t=0;t<teams.size();++t){int assigned=0;for(auto ref:refs[t])assigned+=flow.Remaining(ref)==0;GraphRequire(assigned>=lower[t],"Annual weekend minimum home budget unmet");}
        for(const auto& [sid,rows]:groups)if(flow.Remaining(choices[sid][1])==0)for(auto i:rows)std::swap(games[i].home,games[i].away);
        RecountHomes();
    }
    void WeekendGames(){
        for(auto friday=first;friday<=last;friday+=7){std::set<unsigned> available;
            for(unsigned t=0;t<teams.size();++t)if(left[t]>=3&&friday+2<=Bounds(t).last&&
                std::none_of(occupied[t].begin(),occupied[t].end(),[&](auto v){return friday-1<=v.first&&v.first<=friday+2;}))available.insert(t);
            while(available.size()>1){std::map<unsigned,std::vector<unsigned>> options;
                for(auto t:available)for(auto u:available)if(teams[t].key!=teams[u].key&&pair_games[PairIndex(t,u)]+3<=maximum_pair_games)options[t].push_back(u);
                const auto day=Iso(friday);
                const auto t=*std::min_element(available.begin(),available.end(),[&](auto a,auto b){return std::tuple(options[a].size(),-left[a],Lot({day,teams[a].id}))<std::tuple(options[b].size(),-left[b],Lot({day,teams[b].id}));});
                if(options[t].empty()){available.erase(t);continue;}
                const auto u=*std::min_element(options[t].begin(),options[t].end(),[&](auto a,auto b){
                    return std::tuple(Geography(t,a),pair_games[PairIndex(t,a)],-left[a],Lot({day,teams[t].id,teams[a].id}))<std::tuple(Geography(t,b),pair_games[PairIndex(t,b)],-left[b],Lot({day,teams[t].id,teams[b].id}));});
                const auto need_t=int(teams[t].budget/2)-home[t],need_u=int(teams[u].budget/2)-home[u];
                const bool t_home=need_t>need_u||(need_t==need_u&&Lot({teams[t].id,teams[u].id})<Lot({teams[u].id,teams[t].id}));
                Append(t_home?t:u,t_home?u:t,Slots(friday,3,false,IsByu(t)||IsByu(u)),FixtureKind::Weekend);available.erase(t);available.erase(u);
            }
        }
        OrientWeekends();
    }
    void MidweekGames();
    std::vector<Fixture> Generate(){
        GraphRequire(games.empty(),"Annual generator instance reused");ConferenceGames();WeekendGames();MidweekGames();
        for(unsigned t=0;t<teams.size();++t)GraphRequire(!left[t]&&home[t]==int(teams[t].budget/2)&&away[t]==home[t],"Annual fixture budget/home-away mismatch");
        std::sort(games.begin(),games.end(),[&](const auto& a,const auto& b){return std::tuple(a.day,a.minute,teams[a.home].id,teams[a.away].id)<std::tuple(b.day,b.minute,teams[b.home].id,teams[b.away].id);});
        return games;
    }
};
inline void FixtureGenerator::MidweekGames(){
    std::vector<int32_t> dates;
    for(auto day=first;day<=last;++day)if(Weekday(day)==1||Weekday(day)==2)dates.push_back(day);
    GraphRequire(dates.size()<=32,"Annual midweek date capacity outside arithmetic bounds");
    std::vector<std::vector<bool>> available(teams.size(),std::vector<bool>(dates.size()));std::vector<unsigned> counts(teams.size());
    int demand=0;int64_t denominator=1;
    for(unsigned t=0;t<teams.size();++t){demand+=left[t];
        for(unsigned d=0;d<dates.size();++d)if(dates[d]<=Bounds(t).last&&std::none_of(occupied[t].begin(),occupied[t].end(),[&](auto v){return v.first==dates[d];})){
            available[t][d]=true;++counts[t];}
        GraphRequire(left[t]<=int(counts[t]),"Insufficient annual midweek dates");
        if(counts[t]){const auto multiplier=int64_t(counts[t])/std::gcd(denominator,int64_t(counts[t]));
            GraphRequire(denominator<=1000000000000000LL/multiplier,"Annual quota denominator overflow");denominator*=multiplier;}
    }
    GraphRequire(demand%2==0&&denominator<=std::numeric_limits<int64_t>::max()/40000,"Annual midweek budget/arithmetic bound exceeded");
    const auto divisor=2*denominator;
    std::vector<int64_t> quota(dates.size());std::vector<int> capacity(dates.size()),games_per_date(dates.size());
    for(unsigned d=0;d<dates.size();++d){for(unsigned t=0;t<teams.size();++t)if(available[t][d]){++capacity[d];quota[d]+=left[t]*(denominator/counts[t]);}
        capacity[d]/=2;games_per_date[d]=std::min(capacity[d],int(quota[d]/divisor));}
    auto remainder=demand/2-std::accumulate(games_per_date.begin(),games_per_date.end(),0);
    std::vector<unsigned> order(dates.size());std::iota(order.begin(),order.end(),0);
    std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return std::tuple(-(quota[a]%divisor),Lot({"date",Iso(dates[a])}))<std::tuple(-(quota[b]%divisor),Lot({"date",Iso(dates[b])}));});
    for(auto d:order)if(remainder&&games_per_date[d]<capacity[d]){++games_per_date[d];--remainder;}
    GraphRequire(!remainder,"Annual midweek date capacity cannot satisfy budgets");
    FlowNetwork flow;
    std::map<std::pair<unsigned,unsigned>,unsigned> conf_nodes;std::vector<unsigned> date_nodes;
    for(unsigned d=0;d<dates.size();++d)date_nodes.push_back(flow.Node());
    auto conf_node=[&](unsigned key,unsigned d){const auto pair=std::pair(key,d);auto at=conf_nodes.find(pair);if(at!=conf_nodes.end())return at->second;const auto node=flow.Node();conf_nodes[pair]=node;return node;};
    std::vector<std::vector<FlowRef>> refs(teams.size(),std::vector<FlowRef>(dates.size(),{UINT32_MAX,0}));
    std::vector<unsigned> team_order(teams.size());std::iota(team_order.begin(),team_order.end(),0);
    std::stable_sort(team_order.begin(),team_order.end(),[&](auto a,auto b){return std::tuple(counts[a],Lot({"date-team",teams[a].id}))<std::tuple(counts[b],Lot({"date-team",teams[b].id}));});
    for(auto t:team_order){const auto node=flow.Node();flow.Add(0,node,left[t]);std::vector<unsigned> choices;
        for(unsigned d=0;d<dates.size();++d)if(available[t][d])choices.push_back(d);
        std::stable_sort(choices.begin(),choices.end(),[&](auto a,auto b){return Lot({"choice",teams[t].id,Iso(dates[a])})<Lot({"choice",teams[t].id,Iso(dates[b])});});
        for(auto d:choices)refs[t][d]=flow.Add(node,conf_node(teams[t].key,d),1);
    }
    for(auto key:flow_conference_order)for(unsigned d=0;d<dates.size();++d)flow.Add(conf_node(key,d),date_nodes[d],games_per_date[d]);
    for(unsigned d=0;d<dates.size();++d)flow.Add(date_nodes[d],1,2*games_per_date[d]);
    GraphRequire(flow.Maximum(0,1)==demand,"Exact annual midweek date allocation is infeasible");
    // Fix home quotas before selecting opponents. Orienting a preselected opponent
    // graph can make exact quotas impossible even when the date assignment fits.
    std::vector<std::vector<unsigned>> assigned(dates.size());
    FlowNetwork hosts;
    std::vector<unsigned> host_dates;
    std::map<std::pair<std::string,unsigned>,unsigned> state_nodes,state_counts;
    std::map<std::pair<unsigned,unsigned>,unsigned> region_nodes,region_counts;
    std::map<std::pair<std::string,unsigned>,unsigned> state_regions;
    std::vector<std::vector<FlowRef>> host_refs(teams.size(),std::vector<FlowRef>(dates.size(),{UINT32_MAX,0}));
    for(unsigned d=0;d<dates.size();++d){
        host_dates.push_back(hosts.Node());hosts.Add(host_dates.back(),1,games_per_date[d]);
        for(unsigned t=0;t<teams.size();++t)if(refs[t][d].node!=UINT32_MAX&&!flow.Remaining(refs[t][d])){
            assigned[d].push_back(t);
            const auto state=std::pair(teams[t].state,d);
            const auto region=std::pair(teams[t].region,d);
            if(!state_nodes.count(state)){state_nodes[state]=hosts.Node();state_regions[state]=teams[t].region;}
            if(!region_nodes.count(region))region_nodes[region]=hosts.Node();
            GraphRequire(state_regions.at(state)==teams[t].region,"Annual state has inconsistent travel region");
            ++state_counts[state];++region_counts[region];
        }
        GraphRequire(assigned[d].size()==unsigned(2*games_per_date[d]),"Annual midweek date assignment count differs");
    }
    int home_demand=0;
    for(unsigned t=0;t<teams.size();++t){
        const auto need=int(teams[t].budget/2)-home[t];
        GraphRequire(0<=need&&need<=left[t],"Annual single-game home budget infeasible");
        home_demand+=need;const auto node=hosts.Node();hosts.Add(0,node,need);
        for(unsigned d=0;d<dates.size();++d)if(refs[t][d].node!=UINT32_MAX&&!flow.Remaining(refs[t][d]))
            host_refs[t][d]=hosts.Add(node,state_nodes.at({teams[t].state,d}),1);
    }
    GraphRequire(home_demand==demand/2,"Annual single-game home budget total differs");
    // Convex costs favor equal hosts and visitors within each region/state/date.
    // They guide travel choices without weakening the exact per-team quotas.
    for(const auto& [key,node]:state_nodes){const int count=state_counts.at(key);
        for(int n=1;n<=count;++n){const int64_t before=2*(n-1)-count,after=2*n-count;
            hosts.Add(node,region_nodes.at({state_regions.at(key),key.second}),1,after*after-before*before);}
    }
    for(const auto& [key,node]:region_nodes){const int count=region_counts.at(key);
        for(int n=1;n<=count;++n){const int64_t before=2*(n-1)-count,after=2*n-count;
            hosts.Add(node,host_dates[key.second],1,10000*(after*after-before*before));}
    }
    GraphRequire(hosts.MinimumCost(0,1).first==home_demand,"Annual single-game home/date flow incomplete");
    for(unsigned d=0;d<dates.size();++d){const auto day=Iso(dates[d]);
        std::vector<unsigned> lhs,rhs;
        for(auto t:assigned[d])(hosts.Remaining(host_refs[t][d])?rhs:lhs).push_back(t);
        GraphRequire(lhs.size()==rhs.size()&&lhs.size()==unsigned(games_per_date[d]),"Annual date home/away assignment differs");
        std::vector<std::vector<int64_t>> costs(lhs.size(),std::vector<int64_t>(rhs.size(),-1));
        for(unsigned i=0;i<lhs.size();++i)for(unsigned j=0;j<rhs.size();++j){const auto t=lhs[i],u=rhs[j],prior=pair_games[PairIndex(t,u)];
            if(teams[t].key==teams[u].key||prior>=maximum_pair_games)continue;
            const auto tie=Lot({"pair",day,teams[t].id,teams[u].id});costs[i][j]=int64_t(Geography(t,u))*1000000+prior*1000+((unsigned(tie[0])*256+tie[1])%1000);}
        const auto matching=MinimumAssignment(costs);
        for(unsigned i=0;i<matching.size();++i)Append(lhs[i],rhs[matching[i]],{{dates[d],1080}},FixtureKind::Midweek);
    }
    GraphRequire(std::all_of(left.begin(),left.end(),[](int n){return n==0;}),"Annual midweek assignment left missing games");
}
}
