"""Canonical conference series graphs from the locked competition profiles.

No runtime certification is implied. Published matrices override authored graph
selection. Stable engine indices only order algorithms; canonical IDs persist.
"""
from collections import Counter, defaultdict, deque
from itertools import combinations
import hashlib

class Unsatisfied(ValueError):
    pass

def require(ok,why):
    if not ok:raise Unsatisfied(why)

def pair(a,b):return tuple(sorted((a,b)))

def circle(teams):
    ring=list(teams)
    if len(ring)%2:ring.append(None)
    rounds=[]
    for _ in range(len(ring)-1):
        rounds.append([pair(ring[i],ring[-1-i]) for i in range(len(ring)//2) if ring[i] is not None and ring[-1-i] is not None])
        ring=[ring[0],ring[-1],*ring[1:-1]]
    return rounds

def degree_graph(teams,degrees,allowed,priority=lambda e:e,max_nodes=250000):
    """Exact bounded degree feasibility search; no truncated solution returned."""
    allowed=set(map(lambda e:pair(*e),allowed));remaining=dict(degrees);chosen=[];nodes=0
    adj={t:{b if a==t else a for a,b in allowed if t in (a,b)} for t in teams}
    def visit():
        nonlocal nodes
        nodes+=1
        if nodes>max_nodes:raise Unsatisfied('Degree search exceeded explicit node budget')
        active=[t for t in teams if remaining[t]]
        if not active:return True
        choices={t:sorted((u for u in adj[t] if remaining[u]),key=lambda u:(priority(pair(t,u)),-remaining[u],u)) for t in active}
        if any(len(choices[t])<remaining[t] for t in active):return False
        t=min(active,key=lambda t:(len(choices[t])-remaining[t],-remaining[t],t));need=remaining[t]
        for partners in combinations(choices[t],need):
            remaining[t]=0
            for u in partners:remaining[u]-=1;chosen.append(pair(t,u))
            if visit():return True
            for u in partners:remaining[u]+=1;chosen.pop()
            remaining[t]=need
        return False
    require(sum(degrees.values())%2==0 and all(v>=0 for v in degrees.values()),'Invalid degree budget')
    require(visit(),'No graph satisfies all requested degrees')
    return chosen

def balanced_orientation(edges,reverse=False):
    """Euler orientation with a temporary odd-degree hub, preserving multiplicity."""
    adjacency=defaultdict(list);degrees=Counter();work=list(edges)
    for a,b in work:degrees[a]+=1;degrees[b]+=1
    hub=object()
    for t in sorted(degrees):
        if degrees[t]%2:work.append((t,hub))
    for i,(a,b) in enumerate(work):adjacency[a].append((i,b));adjacency[b].append((i,a))
    used=set();directions={}
    for start in sorted(degrees):
        stack=[start]
        while stack:
            at=stack[-1]
            while adjacency[at] and adjacency[at][-1][0] in used:adjacency[at].pop()
            if not adjacency[at]:stack.pop();continue
            i,to=adjacency[at].pop();used.add(i);directions[i]=(to,at) if reverse else (at,to);stack.append(to)
    require(len(used)==len(work),'Orientation omitted an edge')
    return [directions[i] for i in range(len(edges))]

def edge_colours(edges,minimum=None,maximum=13,max_nodes=300000):
    """Exact bounded edge colouring of small conference multigraphs."""
    degrees=Counter(t for e in edges for t in e);vertices=sorted(degrees)
    lower=max(max(degrees.values()),(len(edges)+(len(vertices)//2)-1)//(len(vertices)//2),minimum or 0)
    for count in range(lower,maximum+1):
        used={t:0 for t in vertices};colours=[-1]*len(edges);full=(1<<count)-1;nodes=0
        # Colour the edges at one vertex distinctly to eliminate colour symmetry.
        start=max(vertices,key=lambda t:degrees[t]);fixed=[i for i,e in enumerate(edges) if start in e]
        for c,i in enumerate(fixed):
            colours[i]=c
            for t in edges[i]:used[t]|=1<<c
        pending=set(range(len(edges)))-set(fixed)
        def search():
            nonlocal nodes
            nodes+=1
            if nodes>max_nodes:return False
            if not pending:return True
            domains={i:full&~(used[edges[i][0]]|used[edges[i][1]]) for i in pending}
            i=min(pending,key=lambda i:(domains[i].bit_count(),-(degrees[edges[i][0]]+degrees[edges[i][1]]),i))
            mask=domains[i]
            if not mask:return False
            a,b=edges[i];pending.remove(i)
            while mask:
                bit=mask&-mask;mask-=bit;c=bit.bit_length()-1
                used[a]|=bit;used[b]|=bit;colours[i]=c
                if search():return True
                used[a]^=bit;used[b]^=bit
            colours[i]=-1;pending.add(i);return False
        if search():return [[i for i,c in enumerate(colours) if c==week] for week in range(count)]
    raise Unsatisfied('Conference graph cannot be coloured within the bounded search/calendar budget')

def conference_series(conf,profile,identities,season,bigwest=None,history=None):
    """Return canonical home/away series and explicit conference-round byes."""
    engine=lambda t:identities['teams'][t]['engine_index']
    by_engine={v['engine_index']:k for k,v in identities['teams'].items() if v.get('active',True)}
    teams=sorted((by_engine[i] for i in conf['members']),key=engine)
    r=profile['regular_season'];model=r['model'];offset=season-2027
    require(offset>=0,'Rotation starts in spring2027')
    history=history or {};edges=[];lengths=[];fixed_rounds=None;oriented=None
    def add(es,n=3):
        edges.extend(es);lengths.extend([n]*len(es))
    def rotate(rows,n):return [rows[(offset*n+i)%len(rows)] for i in range(n)]
    if conf['engine_key']==5 and season==2027:
        require(bigwest and bigwest['season']==season,'Published Big West2027 matrix is required')
        oriented=[];fixed_rounds=[]
        for week in bigwest['weeks']:
            ids=[]
            for s in week['series']:
                ids.append(len(edges));add([pair(s['home'],s['away'])]);oriented.append((s['home'],s['away']))
            fixed_rounds.append(ids)
    elif model in ('single_round_robin_series','double_round_robin_series'):
        rows=circle(teams);add([e for row in rows for e in row]);fixed_rounds=[];cursor=0
        for row in rows:fixed_rounds.append(list(range(cursor,cursor+len(row))));cursor+=len(row)
        oriented=balanced_orientation(edges,offset%2==1)
        if model=='double_round_robin_series':
            n=len(edges);add(list(edges));oriented += [(b,a) for a,b in oriented];fixed_rounds += [[i+n for i in row] for row in fixed_rounds]
    elif model=='partial_round_robin_series':
        policy=r['rotation_policy'];n=policy['selection_factor_count']
        if policy['selection']=='rotating_circle_method_perfect_matching_factors':
            rows=rotate(circle(teams),n);add([e for row in rows for e in row]);fixed_rounds=[];cursor=0
            for row in rows:fixed_rounds.append(list(range(cursor,cursor+len(row))));cursor+=len(row)
        elif policy['selection']=='rotating_circulant_distance_factors':
            distances=[1+(offset*n+i)%((len(teams)-1)//2) for i in range(n)]
            for d in distances:add([pair(t,teams[(i+d)%len(teams)]) for i,t in enumerate(teams)])
        else:raise Unsatisfied('Unknown partial rotation policy')
    elif model=='protected_partial_round_robin_series':
        protected=sorted({pair(t,u) for t,us in r['protected_opponents'].items() for u in us});add(protected)
        allowed=set(combinations(teams,2));allowed={pair(*e) for e in allowed}-set(protected)
        records=history.get('pairs',{})
        count=lambda e:records.get('|'.join(e),{}).get('series',0)
        priority=lambda e:(count(e),hashlib.sha256((str(season)+'|'+'|'.join(e)).encode()).hexdigest())
        rotating=degree_graph(teams,{t:8 for t in teams},allowed,priority);add(rotating)
        # Preserve one protected home and four rotating home series separately.
        oriented=balanced_orientation(protected,offset%2==1)+balanced_orientation(rotating,offset%2==1)
    elif model=='mixed_length_round_robin':
        factors=circle(teams);repeat=rotate(factors,3);repeat_edges=[e for row in repeat for e in row]
        four=[e for row in factors for e in row if e not in repeat_edges]
        add(repeat_edges);add(repeat_edges);add(four,4)
        first=balanced_orientation(repeat_edges,offset%2==1)
        oriented=first+[(b,a) for a,b in first]+balanced_orientation(four,offset%2==1)
        fixed_rounds=[];cursor=0
        for row in repeat+repeat:fixed_rounds.append(list(range(cursor,cursor+len(row))));cursor+=len(row)
        fixed_rounds += [[cursor+i for i in row] for row in edge_colours(four,maximum=2)]
    elif model=='round_robin_plus_repeat_series':
        n=len(teams);distance=1+offset%((n-1)//2);repeat=[pair(t,teams[(i+distance)%n]) for i,t in enumerate(teams)]
        base=[e for row in circle(teams) for e in row];other=[e for e in base if e not in repeat];add(base);add(repeat)
        directions=dict(zip(repeat,balanced_orientation(repeat,offset%2==1)));directions.update(zip(other,balanced_orientation(other,offset%2==1)))
        oriented=[directions[e] for e in base]+[(directions[e][1],directions[e][0]) for e in repeat]
        fixed_rounds=[];cursor=0
        for row in circle(teams):fixed_rounds.append(list(range(cursor,cursor+len(row))));cursor+=len(row)
        fixed_rounds += [[cursor+i for i in row] for row in edge_colours(repeat,maximum=3)]
    elif model=='divisional_double_round_robin_plus_cross_series':
        groups=[sorted(g['team_ids'],key=engine) for g in profile['division']['groups']];require(list(map(len,groups))==[4,4],'ASUN group shape changed')
        first=[e for group in groups for row in circle(group) for e in row];add(first);add(first)
        cross=[pair(a,b) for a in groups[0] for b in groups[1]];add(cross)
        directed=balanced_orientation(first,offset%2==1);oriented=directed+[(b,a) for a,b in directed]+balanced_orientation(cross,offset%2==1)
        fixed_rounds=edge_colours(edges[:len(first)],maximum=3);fixed_rounds += [[i+len(first) for i in row] for row in fixed_rounds]
        fixed_rounds += [[i+2*len(first) for i in row] for row in edge_colours(cross,maximum=4)]
    elif model=='asymmetric_divisional_series':
        groups={g['id']:sorted(g['team_ids'],key=engine) for g in profile['division']['groups']};north,south=groups['north'],groups['south'];south=south[offset%6:]+south[:offset%6]
        path=[pair(a,b) for a,b in zip(south,south[1:])]
        twice=[pair(*e) for group in [north,south] for e in combinations(group,2) if pair(*e) not in path]
        add(twice);add(twice);add(path)
        degree={t:2 for t in north+south};degree[south[0]]=degree[south[-1]]=1
        cross=degree_graph(north+south,degree,{pair(a,b) for a in north for b in south});add(cross)
        # Single path plus cross graph has degree2 at every member: orient it
        # as cycles; divisional paired series are exact home-and-away.
        once=balanced_orientation(twice,offset%2==1);oriented=once+[(b,a) for a,b in once]+balanced_orientation(path+cross,offset%2==1)
    else:raise Unsatisfied('Unknown conference model '+model)
    if oriented is None:oriented=balanced_orientation(edges,offset%2==1)
    require(len(oriented)==len(edges)==len(lengths),'Series construction mismatch')
    if fixed_rounds is None:fixed_rounds=edge_colours(edges)
    require(sorted(i for row in fixed_rounds for i in row)==list(range(len(edges))),'Round partition mismatch')
    rounds={i:week for week,row in enumerate(fixed_rounds) for i in row}
    result=[{'home':h,'away':a,'games':lengths[i],'round':rounds[i]} for i,(h,a) in enumerate(oriented)]
    validate_conference(teams,profile,result)
    return {'conference_id':conf['conference_id'],'season':season,'series':result,'rounds':len(fixed_rounds),
            'byes':{str(w):[t for t in teams if all(t not in (result[i]['home'],result[i]['away']) for i in row)] for w,row in enumerate(fixed_rounds)},
            'teams':teams,'opponent_history_applied':bool(history) and model=='protected_partial_round_robin_series',
            'home_away_history_applied':False,'official_matrix_applied':conf['engine_key']==5 and season==2027}

def validate_conference(teams,profile,series):
    r=profile['regular_season'];games=Counter();hosts=Counter();visits=Counter();opponents=defaultdict(set);counts=Counter();used=set()
    for s in series:
        h,a=s['home'],s['away'];require(h in teams and a in teams and h!=a,'Invalid conference participant');counts[pair(h,a)]+=1
        for t,u in [(h,a),(a,h)]:
            require((t,s['round']) not in used,'Team has two series in one round');used.add((t,s['round']));games[t]+=s['games'];opponents[t].add(u)
        hosts[h]+=1;visits[a]+=1
    def fits(value,budget):return budget['min']<=value<=budget['max'] if isinstance(budget,dict) else value==budget
    for t in teams:
        require(games[t]==r['games_per_team'],f'{t} conference game budget {games[t]} != {r["games_per_team"]}')
        require(hosts[t]+visits[t]==r['series_per_team'],f'{t} conference series budget')
        require(fits(hosts[t],r['home_series_per_team']) and fits(visits[t],r['away_series_per_team']),f'{t} conference home/away imbalance {hosts[t]}/{visits[t]}')
        require(fits(len(opponents[t]),r['distinct_opponents_per_team']),f'{t} conference opponent budget')
    for t,others in r.get('protected_opponents',{}).items():
        require(set(others)<=opponents[t],f'{t} missing protected opponent')
    if r['model']=='single_round_robin_series':require(set(counts.values())=={1} and len(counts)==len(teams)*(len(teams)-1)//2,'Round robin missing pair')
    if r['model']=='double_round_robin_series':require(set(counts.values())=={2} and len(counts)==len(teams)*(len(teams)-1)//2,'Double round robin missing pair')
    model=r['model']
    for t in teams:
        own=[s for s in series if t in (s['home'],s['away'])]
        require(all(s['games'] in (3,4) for s in own),'Unsupported conference series length')
        if model!='mixed_length_round_robin':require(all(s['games']==r['series_games'] for s in own),'Conference series length differs from profile')
        if model=='protected_partial_round_robin_series':
            protected=set(r['protected_opponents'][t])
            require(sum(s['home']==t and s['away'] in protected for s in own)==1,'Protected home series imbalance')
            require(sum(s['away']==t and s['home'] in protected for s in own)==1,'Protected away series imbalance')
        if model=='mixed_length_round_robin':
            require(Counter(s['games'] for s in own)=={3:6,4:2},'Patriot mixed-length budget')
            require(Counter(counts[pair(t,u)] for u in opponents[t])=={2:3,1:2},'Patriot repeat opponent budget')
        if model=='round_robin_plus_repeat_series':
            require(Counter(counts[pair(t,u)] for u in opponents[t])=={2:2,1:len(teams)-3},'America East repeat opponent budget')
    if model in ('mixed_length_round_robin','round_robin_plus_repeat_series','double_round_robin_series','divisional_double_round_robin_plus_cross_series','asymmetric_divisional_series'):
        for (a,b),n in counts.items():
            require(n in (1,2),'Unexpected repeated conference pair')
            if n==2:require(sum(s['home']==a and s['away']==b for s in series)==1,'Repeated pair lacks home-and-away venues')
    if model in ('divisional_double_round_robin_plus_cross_series','asymmetric_divisional_series'):
        divisions={t:g['id'] for g in profile['division']['groups'] for t in g['team_ids']}
        require(set(divisions)==set(teams),'Division partition does not match conference')
        if model=='divisional_double_round_robin_plus_cross_series':
            for a,b in combinations(teams,2):require(counts[pair(a,b)]==(2 if divisions[a]==divisions[b] else 1),'ASUN within/cross pair multiplicity')
        else:
            south_variants=Counter();single_edges=[]
            for t in teams:
                own=[s for s in series if t in (s['home'],s['away'])];cross=[s for s in own if divisions[s['home']]!=divisions[s['away']]]
                within=[u for u in teams if u!=t and divisions[u]==divisions[t]]
                multiplicities=Counter(counts[pair(t,u)] for u in within)
                if divisions[t]=='north':
                    require(multiplicities=={2:4} and len(cross)==2,'CAA North divisional/cross budget')
                    require(sum(s['home']==t for s in cross)==1,'CAA North cross home/away budget')
                else:
                    south_variants[(len(cross),tuple(sorted(multiplicities.items())))]+=1
            require(south_variants=={(1,((1,1),(2,4))):2,(2,((1,2),(2,3))):4},'CAA South asymmetric division budget')
            single_edges=[(a,b) for (a,b),n in counts.items() if n==1 and divisions[a]==divisions[b]=='south']
            degree=Counter(t for e in single_edges for t in e);seen=set();pending=[next(iter(degree))]
            while pending:
                t=pending.pop()
                if t in seen:continue
                seen.add(t);pending.extend(b if a==t else a for a,b in single_edges if t in (a,b))
            require(len(single_edges)==5 and len(seen)==6 and Counter(degree.values())=={1:2,2:4},'CAA single-meeting edges are not one six-team path')
    return {'games':dict(games),'home_series':dict(hosts),'away_series':dict(visits),'distinct_opponents':{t:len(v) for t,v in opponents.items()}}
