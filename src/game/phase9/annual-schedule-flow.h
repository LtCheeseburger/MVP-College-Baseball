#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <stdexcept>
#include <vector>

// Integer solvers used by the annual schedule generator. All capacities,
// forbidden assignments and lower-bound checks remain explicit to callers.
namespace mvp::annual_schedule {
struct FlowRef {unsigned node,edge;};
struct FlowEdge {unsigned to,back;int capacity;int64_t cost;};
struct FlowNetwork {
    std::vector<std::vector<FlowEdge>> graph;
    explicit FlowNetwork(unsigned nodes=2):graph(nodes){}
    unsigned Node(){graph.emplace_back();return graph.size()-1;}
    FlowRef Add(unsigned a,unsigned b,int capacity,int64_t cost=0){
        if(a==b||a>=graph.size()||b>=graph.size()||capacity<0)throw std::invalid_argument("Invalid schedule flow edge");
        const FlowRef ref{a,unsigned(graph[a].size())};
        const auto back=unsigned(graph[b].size());
        graph[a].push_back({b,back,capacity,cost});graph[b].push_back({a,ref.edge,0,-cost});return ref;
    }
    int Remaining(FlowRef ref)const{return graph.at(ref.node).at(ref.edge).capacity;}
    int Maximum(unsigned source,unsigned sink){
        int total=0;std::vector<int> levels(graph.size());std::vector<unsigned> cursor(graph.size());
        for(;;){
            std::fill(levels.begin(),levels.end(),-1);std::fill(cursor.begin(),cursor.end(),0);
            std::deque<unsigned> queue{source};levels[source]=0;
            while(!queue.empty()){const auto a=queue.front();queue.pop_front();for(const auto& edge:graph[a])
                if(edge.capacity&&levels[edge.to]<0){levels[edge.to]=levels[a]+1;queue.push_back(edge.to);}}
            if(levels[sink]<0)return total;
            std::function<int(unsigned,int)> send=[&](unsigned a,int amount){
                if(a==sink)return amount;
                while(cursor[a]<graph[a].size()){
                    auto& edge=graph[a][cursor[a]];
                    if(edge.capacity&&levels[edge.to]==levels[a]+1){const auto sent=send(edge.to,std::min(amount,edge.capacity));
                        if(sent){edge.capacity-=sent;graph[edge.to][edge.back].capacity+=sent;return sent;}}
                    ++cursor[a];
                }
                return 0;
            };
            while(const auto sent=send(source,1000000000))total+=sent;
        }
    }
    std::pair<int,int64_t> MinimumCost(unsigned source,unsigned sink){
        int total=0;int64_t cost=0;const auto infinity=std::numeric_limits<int64_t>::max()/4;
        std::vector<int64_t> distance(graph.size());std::vector<FlowRef> previous(graph.size());std::vector<bool> queued(graph.size());
        for(;;){
            std::fill(distance.begin(),distance.end(),infinity);std::fill(queued.begin(),queued.end(),false);
            std::deque<unsigned> queue{source};queued[source]=true;distance[source]=0;
            while(!queue.empty()){const auto a=queue.front();queue.pop_front();queued[a]=false;
                for(unsigned i=0;i<graph[a].size();++i){const auto& e=graph[a][i];
                    if(e.capacity&&distance[e.to]>distance[a]+e.cost){distance[e.to]=distance[a]+e.cost;previous[e.to]={a,i};
                        if(!queued[e.to]){queued[e.to]=true;queue.push_back(e.to);}}
                }
            }
            if(distance[sink]==infinity)return {total,cost};
            int sent=1000000000;
            for(auto at=sink;at!=source;at=previous[at].node){const auto ref=previous[at];sent=std::min(sent,graph[ref.node][ref.edge].capacity);}
            for(auto at=sink;at!=source;at=previous[at].node){const auto ref=previous[at];auto& edge=graph[ref.node][ref.edge];edge.capacity-=sent;graph[at][edge.back].capacity+=sent;}
            total+=sent;cost+=int64_t(sent)*distance[sink];
        }
    }
};
inline std::vector<unsigned> MinimumAssignment(const std::vector<std::vector<int64_t>>& costs){
    const auto n=costs.size();for(const auto& row:costs)if(row.size()!=n)throw std::invalid_argument("Non-square schedule assignment");
    if(!n)return {};
    // Negative costs denote forbidden opponents; allowed travel/repeat costs
    // are nonnegative. Potentials stay below the explicit infinity bound.
    constexpr int64_t infinity=1000000000000000000LL;
    std::vector<int64_t> u(n+1),v(n+1);std::vector<unsigned> matching(n+1),via(n+1);
    for(unsigned row=1;row<=n;++row){
        matching[0]=row;unsigned column=0;std::vector<int64_t> distance(n+1,infinity);std::vector<bool> seen(n+1);
        for(;;){
            seen[column]=true;const auto current=matching[column];int64_t delta=infinity;unsigned next=0;
            for(unsigned j=1;j<=n;++j){if(seen[j])continue;const auto value=costs[current-1][j-1];
                const auto reduced=value<0?infinity:value-u[current]-v[j];
                if(reduced<distance[j]){distance[j]=reduced;via[j]=column;}
                if(distance[j]<delta){delta=distance[j];next=j;}
            }
            if(delta>=infinity)throw std::runtime_error("No complete permitted schedule opponent assignment");
            for(unsigned j=0;j<=n;++j){if(seen[j]){u[matching[j]]+=delta;v[j]-=delta;}else distance[j]-=delta;}
            column=next;if(!matching[column])break;
        }
        for(;;){const auto prior=via[column];matching[column]=matching[prior];column=prior;if(!column)break;}
    }
    std::vector<unsigned> result(n);for(unsigned j=1;j<=n;++j)result[matching[j]-1]=j-1;
    for(unsigned i=0;i<n;++i)if(costs[i][result[i]]<0)throw std::runtime_error("Schedule assignment used a forbidden edge");
    return result;
}
}
