#pragma once
#include "competition-qualification-model.h"
#include <bit>
#include <functional>
#include <map>
#include <set>
#include <tuple>

// Native counterpart of the locked conference graph authoring pipeline.
// This candidate only constructs series; the dated importer is separate.
namespace mvp::annual_schedule {
using Team=std::string;
using Edge=std::pair<Team,Team>;
using Rounds=std::vector<std::vector<unsigned>>;
using History=std::map<Edge,unsigned>;
inline void GraphRequire(bool ok,const char* why) {if(!ok)throw std::runtime_error(why);}
inline Edge Pair(Team a,Team b) {return a<b?Edge{a,b}:Edge{b,a};}
inline std::vector<std::vector<Edge>> Circle(std::vector<Team> ring) {
    if(ring.size()%2)ring.push_back("");
    GraphRequire(ring.size()>=2,"Conference circle requires at least two teams");
    std::vector<std::vector<Edge>> rounds;
    for(unsigned r=0;r+1<ring.size();++r) {
        std::vector<Edge> row;
        for(unsigned i=0;i<ring.size()/2;++i)
            if(!ring[i].empty()&&!ring[ring.size()-1-i].empty())row.push_back(Pair(ring[i],ring[ring.size()-1-i]));
        rounds.push_back(std::move(row));
        std::rotate(ring.begin()+1,ring.end()-1,ring.end());
    }
    return rounds;
}
inline std::vector<Edge> Orient(const std::vector<Edge>& edges,bool reverse) {
    std::map<Team,std::vector<std::pair<unsigned,Team>>> adjacency;
    std::map<Team,unsigned> degrees;
    auto work=edges;
    for(const auto& [a,b]:edges){++degrees[a];++degrees[b];}
    for(const auto& [t,n]:degrees)if(n%2)work.push_back({t,""});
    for(unsigned i=0;i<work.size();++i){const auto& [a,b]=work[i];adjacency[a].push_back({i,b});adjacency[b].push_back({i,a});}
    std::vector<bool> used(work.size());std::vector<Edge> directions(work.size());unsigned count=0;
    for(const auto& [start,n]:degrees){(void)n;std::vector<Team> stack{start};
        while(!stack.empty()) {
            const auto at=stack.back();auto& links=adjacency[at];
            while(!links.empty()&&used[links.back().first])links.pop_back();
            if(links.empty()){stack.pop_back();continue;}
            const auto [i,to]=links.back();links.pop_back();used[i]=true;++count;
            directions[i]=reverse?Edge{to,at}:Edge{at,to};stack.push_back(to);
        }
    }
    GraphRequire(count==work.size(),"Conference orientation omitted an edge");
    directions.resize(edges.size());return directions;
}
using EdgePriority=std::tuple<unsigned,std::array<uint8_t,32>,Edge>;
inline std::vector<Edge> DegreeGraph(const std::vector<Team>& teams,std::map<Team,int> remaining,
        const std::set<Edge>& allowed,const std::function<EdgePriority(const Edge&)>& priority) {
    std::map<Team,std::set<Team>> adjacent;
    for(const auto& [a,b]:allowed){adjacent[a].insert(b);adjacent[b].insert(a);}
    int sum=0;for(const auto& t:teams){GraphRequire(remaining[t]>=0,"Negative conference degree");sum+=remaining[t];}
    GraphRequire(sum%2==0,"Odd conference degree budget");
    std::vector<Edge> chosen;unsigned nodes=0;
    std::function<bool()> visit=[&] {
        GraphRequire(++nodes<=250000,"Conference degree search exceeded node budget");
        std::map<Team,std::vector<Team>> choices;Team selected;std::tuple<int,int,Team> best;
        for(const auto& t:teams)if(remaining[t]) {
            auto& options=choices[t];for(const auto& u:adjacent[t])if(remaining[u])options.push_back(u);
            if(int(options.size())<remaining[t])return false;
            std::sort(options.begin(),options.end(),[&](const Team& a,const Team& b){
                return std::tuple(priority(Pair(t,a)),-remaining[a],a)<std::tuple(priority(Pair(t,b)),-remaining[b],b);});
            const auto score=std::tuple(int(options.size())-remaining[t],-remaining[t],t);
            if(selected.empty()||score<best){selected=t;best=score;}
        }
        if(selected.empty())return true;
        const int need=remaining[selected];const auto options=choices[selected];std::vector<Team> partners;
        std::function<bool(unsigned)> choose=[&](unsigned next){
            if(int(partners.size())==need) {
                remaining[selected]=0;
                for(const auto& u:partners){--remaining[u];chosen.push_back(Pair(selected,u));}
                if(visit())return true;
                for(const auto& u:partners){++remaining[u];chosen.pop_back();}
                remaining[selected]=need;return false;
            }
            for(unsigned i=next;i<options.size();++i){
                if(int(partners.size()+options.size()-i)<need)break;
                partners.push_back(options[i]);if(choose(i+1))return true;partners.pop_back();
            }
            return false;
        };
        return choose(0);
    };
    GraphRequire(visit(),"No conference graph satisfies the requested degrees");return chosen;
}
inline Rounds Colour(const std::vector<Edge>& edges,unsigned maximum=13) {
    GraphRequire(!edges.empty(),"Empty conference graph");
    std::map<Team,unsigned> degrees;for(const auto& [a,b]:edges){++degrees[a];++degrees[b];}
    unsigned lower=0;Team start;
    for(const auto& [t,n]:degrees)if(n>lower){lower=n;start=t;}
    const auto half=degrees.size()/2;GraphRequire(half>0,"Invalid graph population");
    lower=std::max(lower,unsigned((edges.size()+half-1)/half));
    for(unsigned count=lower;count<=maximum;++count) {
        GraphRequire(count<32,"Conference colouring mask capacity exceeded");
        std::map<Team,uint32_t> used;std::vector<int> colours(edges.size(),-1);std::set<unsigned> pending;
        unsigned fixed=0,nodes=0;const uint32_t full=(1u<<count)-1;
        for(unsigned i=0;i<edges.size();++i){const auto& [a,b]=edges[i];
            if(a==start||b==start){colours[i]=int(fixed);used[a]|=1u<<fixed;used[b]|=1u<<fixed;++fixed;}
            else pending.insert(i);
        }
        std::function<bool()> search=[&]{
            if(++nodes>300000)return false;if(pending.empty())return true;
            unsigned selected=0;bool found=false;std::tuple<int,int,unsigned> best;
            for(auto i:pending){const auto& [a,b]=edges[i];const auto domain=full&~(used[a]|used[b]);
                const auto score=std::tuple(std::popcount(domain),-int(degrees[a]+degrees[b]),i);
                if(!found||score<best){found=true;selected=i;best=score;}
            }
            const auto& [a,b]=edges[selected];auto mask=full&~(used[a]|used[b]);if(!mask)return false;
            pending.erase(selected);
            while(mask){const auto bit=mask&(~mask+1);mask-=bit;used[a]|=bit;used[b]|=bit;colours[selected]=std::countr_zero(bit);
                if(search())return true;used[a]^=bit;used[b]^=bit;
            }
            colours[selected]=-1;pending.insert(selected);return false;
        };
        if(search()){Rounds result(count);for(unsigned i=0;i<colours.size();++i)result.at(colours[i]).push_back(i);return result;}
    }
    throw std::runtime_error("Conference graph cannot fit the bounded colouring/calendar budget");
}
enum class SeriesModel {Single,Double,PartialCircle,PartialDistance,Protected,MixedLength,Repeat,DividedDouble,Asymmetric};
struct ConferenceRule {
    uint32_t key;SeriesModel model;unsigned factor_count;
    std::vector<Team> teams;std::vector<std::vector<Team>> divisions;std::set<Edge> protected_pairs;
};
struct Series {Team home,away;unsigned games,round;};
struct ConferenceGraph {std::vector<Series> series;unsigned rounds;};
inline ConferenceGraph ConferenceSeries(const ConferenceRule& rule,unsigned year,const History& history={}) {
    GraphRequire(year>=2028,"Native annual graphs begin after the published2027 initial schedule");
    const auto offset=year-2027;const bool reverse=offset%2;const auto& teams=rule.teams;
    GraphRequire(teams.size()>=4&&teams.size()<=40&&std::set<Team>(teams.begin(),teams.end()).size()==teams.size(),"Invalid locked conference population");
    std::vector<Edge> edges,oriented;std::vector<unsigned> lengths;Rounds rounds;
    auto add=[&](const std::vector<Edge>& values,unsigned games=3){edges.insert(edges.end(),values.begin(),values.end());lengths.insert(lengths.end(),values.size(),games);};
    auto append_rows=[&](const std::vector<std::vector<Edge>>& rows){for(const auto& row:rows){std::vector<unsigned> indices;for(unsigned i=0;i<row.size();++i)indices.push_back(edges.size()+i);rounds.push_back(indices);add(row);}};
    auto rotate=[&](const std::vector<std::vector<Edge>>& rows,unsigned n){std::vector<std::vector<Edge>> result;for(unsigned i=0;i<n;++i)result.push_back(rows[(offset*n+i)%rows.size()]);return result;};
    auto flipped=[](std::vector<Edge> values){for(auto& e:values)std::swap(e.first,e.second);return values;};
    auto append_directed=[&](const std::vector<Edge>& values){oriented.insert(oriented.end(),values.begin(),values.end());};
    auto add_colours=[&](const std::vector<Edge>& values,unsigned base,unsigned maximum){auto more=Colour(values,maximum);for(auto& row:more){for(auto& i:row)i+=base;rounds.push_back(std::move(row));}};
    switch(rule.model) {
    case SeriesModel::Single:case SeriesModel::Double: {
        append_rows(Circle(teams));oriented=Orient(edges,reverse);
        if(rule.model==SeriesModel::Double){const auto prior=edges;const auto old_rounds=rounds;const auto n=edges.size();add(prior);append_directed(flipped(oriented));for(auto row:old_rounds){for(auto& i:row)i+=n;rounds.push_back(std::move(row));}}
        break;}
    case SeriesModel::PartialCircle:append_rows(rotate(Circle(teams),rule.factor_count));break;
    case SeriesModel::PartialDistance:
        for(unsigned i=0;i<rule.factor_count;++i){const auto distance=1+(offset*rule.factor_count+i)%((teams.size()-1)/2);for(unsigned j=0;j<teams.size();++j)add({Pair(teams[j],teams[(j+distance)%teams.size()])});}break;
    case SeriesModel::Protected: {
        const std::vector<Edge> fixed(rule.protected_pairs.begin(),rule.protected_pairs.end());add(fixed);
        std::set<Edge> allowed;std::map<Team,int> degrees;
        for(unsigned i=0;i<teams.size();++i){degrees[teams[i]]=8;for(unsigned j=i+1;j<teams.size();++j){auto e=Pair(teams[i],teams[j]);if(!rule.protected_pairs.count(e))allowed.insert(e);}}
        auto priority=[&](const Edge& e){auto at=history.find(e);return EdgePriority{at==history.end()?0:at->second,qualification_model::Sha256(std::to_string(year)+"|"+e.first+"|"+e.second),e};};
        const auto rotating=DegreeGraph(teams,degrees,allowed,priority);add(rotating);oriented=Orient(fixed,reverse);append_directed(Orient(rotating,reverse));break;}
    case SeriesModel::MixedLength: {
        const auto factors=Circle(teams),repeated=rotate(factors,3);std::vector<Edge> twice,four;
        for(const auto& row:repeated)twice.insert(twice.end(),row.begin(),row.end());
        for(const auto& row:factors)for(const auto& e:row)if(std::find(twice.begin(),twice.end(),e)==twice.end())four.push_back(e);
        append_rows(repeated);append_rows(repeated);const auto base=edges.size();add(four,4);add_colours(four,base,2);
        oriented=Orient(twice,reverse);append_directed(flipped(oriented));append_directed(Orient(four,reverse));break;}
    case SeriesModel::Repeat: {
        std::vector<Edge> repeated,other;const auto distance=1+offset%((teams.size()-1)/2);
        for(unsigned i=0;i<teams.size();++i)repeated.push_back(Pair(teams[i],teams[(i+distance)%teams.size()]));
        append_rows(Circle(teams));const auto base=edges;for(const auto& e:base)if(std::find(repeated.begin(),repeated.end(),e)==repeated.end())other.push_back(e);
        std::map<Edge,Edge> directions;auto assign=[&](const std::vector<Edge>& source){const auto values=Orient(source,reverse);for(unsigned i=0;i<source.size();++i)directions[source[i]]=values[i];};assign(repeated);assign(other);
        for(const auto& e:base)oriented.push_back(directions[e]);for(const auto& e:repeated){auto [a,b]=directions[e];oriented.push_back({b,a});}
        add(repeated);add_colours(repeated,base.size(),3);break;}
    case SeriesModel::DividedDouble: {
        GraphRequire(rule.divisions.size()==2&&rule.divisions[0].size()==4&&rule.divisions[1].size()==4,"ASUN division shape changed");
        std::vector<Edge> first,cross;for(const auto& group:rule.divisions)for(const auto& row:Circle(group))first.insert(first.end(),row.begin(),row.end());
        for(const auto& a:rule.divisions[0])for(const auto& b:rule.divisions[1])cross.push_back(Pair(a,b));
        add(first);add(first);add(cross);oriented=Orient(first,reverse);append_directed(flipped(oriented));append_directed(Orient(cross,reverse));
        rounds=Colour(first,3);const auto original=rounds;for(auto row:original){for(auto& i:row)i+=first.size();rounds.push_back(std::move(row));}add_colours(cross,2*first.size(),4);break;}
    case SeriesModel::Asymmetric: {
        GraphRequire(rule.divisions.size()==2&&rule.divisions[0].size()==5&&rule.divisions[1].size()==6,"CAA division shape changed");
        const auto north=rule.divisions[0];auto south=rule.divisions[1];std::rotate(south.begin(),south.begin()+offset%south.size(),south.end());
        std::vector<Edge> path,twice;for(unsigned i=1;i<south.size();++i)path.push_back(Pair(south[i-1],south[i]));
        for(const auto& group:{north,south})for(unsigned i=0;i<group.size();++i)for(unsigned j=i+1;j<group.size();++j){auto e=Pair(group[i],group[j]);if(std::find(path.begin(),path.end(),e)==path.end())twice.push_back(e);}
        add(twice);add(twice);add(path);std::map<Team,int> degrees;for(const auto& t:teams)degrees[t]=2;degrees[south.front()]=degrees[south.back()]=1;
        std::set<Edge> allowed;for(const auto& a:north)for(const auto& b:south)allowed.insert(Pair(a,b));
        // Zero priorities leave the explicit canonical-ID tie ordering intact.
        auto cross=DegreeGraph(teams,degrees,allowed,[](const Edge& e){return EdgePriority{0,{},e};});add(cross);
        oriented=Orient(twice,reverse);append_directed(flipped(oriented));path.insert(path.end(),cross.begin(),cross.end());append_directed(Orient(path,reverse));break;}
    }
    if(oriented.empty())oriented=Orient(edges,reverse);
    if(rounds.empty())rounds=Colour(edges);
    GraphRequire(oriented.size()==edges.size()&&lengths.size()==edges.size(),"Conference series representation mismatch");
    std::vector<int> assigned(edges.size(),-1);
    for(unsigned week=0;week<rounds.size();++week){std::set<Team> occupied;for(auto i:rounds[week]){
        GraphRequire(i<edges.size()&&assigned[i]<0,"Conference rounds duplicate an edge");assigned[i]=week;
        GraphRequire(occupied.insert(edges[i].first).second&&occupied.insert(edges[i].second).second,"Conference round double books a team");}}
    ConferenceGraph result{{},unsigned(rounds.size())};
    for(unsigned i=0;i<edges.size();++i){GraphRequire(assigned[i]>=0,"Conference rounds omitted an edge");result.series.push_back({oriented[i].first,oriented[i].second,lengths[i],unsigned(assigned[i])});}
    return result;
}
}
