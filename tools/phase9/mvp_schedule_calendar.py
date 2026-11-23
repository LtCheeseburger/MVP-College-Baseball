"""Dated regular-season fixtures; bounded failures never publish partial assets."""
from collections import Counter, defaultdict, deque
from datetime import date, timedelta
import hashlib
from .mvp_schedule_graphs import Unsatisfied, require, pair

REGIONS = [
    'ME NH VT MA RI CT NY NJ PA', 'DE MD DC VA WV NC SC',
    'GA FL AL MS TN KY', 'OH MI IN IL WI MN IA MO',
    'AR LA TX OK KS NE SD ND', 'MT WY CO NM UT ID NV AZ',
    'WA OR CA AK HI',
]
REGION = {state: i for i, group in enumerate(REGIONS) for state in group.split()}
BYU = 'ncaa:77'

def weekday_on_or_after(day, weekday):return day+timedelta(days=(weekday-day.weekday())%7)
def weekday_on_or_before(day, weekday):return day-timedelta(days=(day.weekday()-weekday)%7)

def season_windows(season, profiles, policy, bigwest):
    known=policy['first_permissible_date'].get(str(season))
    first=date.fromisoformat(known) if known else weekday_on_or_after(date(season,2,15),4)
    deadline=weekday_on_or_before(date(season,5,31),0)
    windows={};weekdays=['Monday','Tuesday','Wednesday','Thursday','Friday','Saturday','Sunday']
    for cid,p in profiles.items():
        event=p['tournament']['event_plan'];published=event.get('published_target_dates')
        if cid==bigwest['conference_id'] and season==bigwest['season']:published=bigwest['published_tournament_dates']
        if published and date.fromisoformat(published['start']).year==season:
            start,end=map(date.fromisoformat,[published['start'],published['end']]);origin='published_target_dates'
        else:
            span=event['maximum_span_days'];start=weekday_on_or_before(deadline-timedelta(days=span),weekdays.index(event['start_weekday']));end=start+timedelta(days=span-1);origin='authored_placement_of_profile_event_window'
        regular_end=weekday_on_or_before(start-timedelta(days=1),6)
        if cid=='ncaa-conf:865':
            regular_end=weekday_on_or_after(date(season,5,1),6)
            start=regular_end+timedelta(days=12);end=start+timedelta(days=3);origin='authored_weekday_aligned_ivy_2026_fallback'
        require(first<=regular_end<start<=end<deadline,'Invalid regular/postseason window '+cid)
        windows[cid]={'first':first,'last':regular_end,'event_start':start,'event_end':end,'event_origin':origin}
    return first,deadline,windows

def series_slots(friday, games, saturday_only=False, no_sunday=False, first=None):
    if games==4 or saturday_only:cadence=[(1,2),(2,games-2)]
    elif no_sunday:
        cadence=[(-1,1),(0,1),(1,1)] if friday-timedelta(days=1)>=first else [(0,1),(1,2)]
    else:cadence=[(0,1),(1,1),(2,1)]
    slots=[]
    for offset,count in cadence:
        for i in range(count):slots.append((friday+timedelta(days=offset),['12:00','16:00'][i] if count==2 else '13:00'))
    require(len(slots)==games,'Incorrect series cadence')
    return slots

class Flow:
    """Integer max flow for exact singleton home/away assignment."""
    def __init__(self):self.g=defaultdict(list)
    def add(self,a,b,cap):
        f=[b,cap,len(self.g[b])];r=[a,0,len(self.g[a])];self.g[a].append(f);self.g[b].append(r);return f
    def run(self,source,sink):
        total=0
        while True:
            level={source:0};q=deque([source])
            while q:
                a=q.popleft()
                for b,cap,_ in self.g[a]:
                    if cap and b not in level:level[b]=level[a]+1;q.append(b)
            if sink not in level:return total
            cursor=defaultdict(int)
            def send(a,amount):
                if a==sink:return amount
                while cursor[a]<len(self.g[a]):
                    e=self.g[a][cursor[a]];b,cap,back=e
                    if cap and level.get(b)==level[a]+1:
                        sent=send(b,min(amount,cap))
                        if sent:e[1]-=sent;self.g[b][back][1]+=sent;return sent
                    cursor[a]+=1
                return 0
            while True:
                sent=send(source,10**9)
                if not sent:break
                total+=sent

class CostFlow:
    """Successive shortest augmenting paths with exact integer costs."""
    def __init__(self):self.g=defaultdict(list)
    def add(self,a,b,capacity,cost):
        edge=[b,capacity,cost,len(self.g[b])];reverse=[a,0,-cost,len(self.g[a])]
        self.g[a].append(edge);self.g[b].append(reverse);return edge
    def run(self,source,sink):
        total=cost=0
        while True:
            distance={source:0};previous={};queue=deque([source]);queued={source}
            while queue:
                a=queue.popleft();queued.remove(a)
                for i,(b,cap,weight,_) in enumerate(self.g[a]):
                    if cap and distance.get(b,10**30)>distance[a]+weight:
                        distance[b]=distance[a]+weight;previous[b]=(a,i)
                        if b not in queued:queued.add(b);queue.append(b)
            if sink not in distance:return total,cost
            sent=10**9;at=sink
            while at!=source:
                a,i=previous[at];sent=min(sent,self.g[a][i][1]);at=a
            at=sink
            while at!=source:
                a,i=previous[at];e=self.g[a][i];e[1]-=sent;self.g[at][e[3]][1]+=sent;at=a
            total+=sent;cost+=sent*distance[sink]

def minimum_cost_assignment(costs):
    """Exact square assignment with integer dual potentials; None is forbidden."""
    n=len(costs)
    require(all(len(row)==n for row in costs),'Non-square opponent cost matrix')
    if not n:return []
    infinity=10**18;u=[0]*(n+1);v=[0]*(n+1);matching=[0]*(n+1);via=[0]*(n+1)
    for row in range(1,n+1):
        matching[0]=row;column=0;distance=[infinity]*(n+1);seen=[False]*(n+1)
        while True:
            seen[column]=True;current=matching[column];delta=infinity;next_column=0
            for j in range(1,n+1):
                if seen[j]:continue
                value=costs[current-1][j-1]
                reduced=infinity if value is None else value-u[current]-v[j]
                if reduced<distance[j]:distance[j]=reduced;via[j]=column
                if distance[j]<delta:delta=distance[j];next_column=j
            require(delta<infinity,'No complete permitted opponent assignment')
            for j in range(n+1):
                if seen[j]:u[matching[j]]+=delta;v[j]-=delta
                else:distance[j]-=delta
            column=next_column
            if matching[column]==0:break
        while column:
            previous=via[column];matching[column]=matching[previous];column=previous
    answer=[0]*n
    for j in range(1,n+1):answer[matching[j]-1]=j-1
    require(all(costs[i][j] is not None for i,j in enumerate(answer)),'Assignment used a forbidden edge')
    return answer

def orient_singles(games,home_needed):
    flow=Flow();refs={};degree=Counter(t for g in games for t in (g['home'],g['away']))
    require(sum(home_needed.values())==len(games),'Singleton home budget sum mismatch')
    for t,count in home_needed.items():
        require(0<=count<=degree[t],f'Impossible remaining home budget {t}: {count}/{degree[t]}')
        flow.add(('team',t),'sink',count)
    for i,g in enumerate(games):
        node=('game',i);flow.add('source',node,1)
        refs[i]=[flow.add(node,('team',t),1) for t in (g['home'],g['away'])]
    require(flow.run('source','sink')==len(games),'Singleton opponents cannot satisfy exact home/away budgets')
    for i,g in enumerate(games):
        if refs[i][1][1]==0:g['home'],g['away']=g['away'],g['home']

def orient_weekend_series(fixtures, budget, remaining_singles):
    groups=defaultdict(list);conference_home=Counter()
    for g in fixtures:
        if g['kind']=='nonconference_series':groups[g['series_id']].append(g)
        elif g['kind']=='conference':conference_home[g['home']]+=1
    degree=Counter(t for gs in groups.values() for t in (gs[0]['home'],gs[0]['away']))
    flow=CostFlow();refs={};bounds={};team_slots={}
    for t in budget:
        need=budget[t]//2-conference_home[t]
        low=max(0,(need-remaining_singles[t]+2)//3);high=min(degree[t],need//3)
        require(low<=high,f'No feasible series home budget for {t}')
        bounds[t]=(low,high);team_slots[t]=[]
        # Convex cost keeps the remaining singles close to half home. Mandatory
        # lower-bound slots dominate all possible secondary costs combined.
        for slot in range(1,high+1):
            before=2*(need-3*(slot-1))-remaining_singles[t]
            after=2*(need-3*slot)-remaining_singles[t]
            cost=after*after-before*before-(10**9 if slot<=low else 0)
            team_slots[t].append(flow.add(('team',t),'sink',1,cost))
    for sid,gs in groups.items():
        require(len(gs)==3,'Nonconference series must have three games')
        node=('series',sid);flow.add('source',node,1,0)
        refs[sid]=[flow.add(node,('team',t),1,0) for t in (gs[0]['home'],gs[0]['away'])]
    require(flow.run('source','sink')[0]==len(groups),'Cannot satisfy weekend home budgets')
    require(all(sum(e[1]==0 for e in team_slots[t])>=bounds[t][0] for t in budget),'Cannot satisfy minimum weekend home budgets')
    for sid,gs in groups.items():
        if refs[sid][1][1]==0:
            for g in gs:g['home'],g['away']=g['away'],g['home']

def generate_fixtures(season,conferences,profiles,graphs,league,policy,bigwest,attempt=0):
    first,deadline,windows=season_windows(season,profiles,policy,bigwest)
    teams={t['id']:t for t in league['teams']};member={t:c['conference_id'] for c in conferences for t in graphs[c['conference_id']]['teams']}
    require(set(member)==set(teams),'Fixture membership differs from locked active league')
    budget={t:policy['conference_budget_overrides'].get(member[t],{}).get('games',policy['default_regular_season_games']) for t in teams}
    left=dict(budget);home=Counter();away=Counter();occupied=defaultdict(list);fixtures=[];pair_games=Counter();series_id=0
    def append(h,a,slots,kind,cid=None,round_index=None):
        nonlocal series_id
        series_id+=1
        for day,time in slots:
            for t in (h,a):
                require(first<=day<=windows[member[t]]['last'],f'Game outside regular window for {t}')
                require((day,time) not in occupied[t],f'Conflicting game for {t}')
                occupied[t].append((day,time));left[t]-=1
            require(min(left[h],left[a])>=0,'Regular budget exceeded')
            home[h]+=1;away[a]+=1;pair_games[pair(h,a)]+=1
            fixtures.append({'date':day.isoformat(),'time':time,'home':h,'away':a,'kind':kind,'conference_id':cid,'series_id':series_id,'conference_round':round_index})
    for c in conferences:
        cid=c['conference_id'];graph=graphs[cid];profile=profiles[cid];last_friday=windows[cid]['last']-timedelta(days=2)
        start=last_friday-timedelta(weeks=graph['rounds']-1)
        require(start>=first,'Conference rounds do not fit season '+cid)
        for s in graph['series']:
            friday=start+timedelta(weeks=s['round']);no_sunday=BYU in (s['home'],s['away']) or (c['engine_key']==4 and s['round']==graph['rounds']-1)
            slots=series_slots(friday,s['games'],profile['regular_season']['calendar_policy']['series_window']=='Saturday-Sunday',no_sunday,first)
            append(s['home'],s['away'],slots,'conference',cid,s['round'])
    states={t:row.get('state','') for t,row in teams.items()}
    def geo(a,b):return 0 if states[a]==states[b] else 1 if REGION.get(states[a],-1)==REGION.get(states[b],-2) else 2
    def lot(*parts):return hashlib.sha256(('|'.join(map(str,(season,attempt,*parts)))).encode()).digest()
    fridays=[];day=first
    while day<=max(w['last'] for w in windows.values()):fridays.append(day);day+=timedelta(weeks=1)
    # Fill unused weekends before singleton midweeks. A full conference bye is
    # usable for nonconference play, without adding intraconference games.
    for friday in fridays:
        available={t for t in teams if left[t]>=3 and friday+timedelta(days=2)<=windows[member[t]]['last'] and not any(friday-timedelta(days=1)<=d<=friday+timedelta(days=2) for d,_ in occupied[t])}
        while len(available)>1:
            options={t:[u for u in available if member[t]!=member[u] and pair_games[pair(t,u)]+3<=policy['nonconference']['maximum_games_against_same_opponent']] for t in available}
            t=min(available,key=lambda t:(len(options[t]),-left[t],lot(friday,t)))
            if not options[t]:available.remove(t);continue
            u=min(options[t],key=lambda u:(geo(t,u),pair_games[pair(t,u)],-left[u],lot(friday,t,u)))
            # Leave enough singleton headroom for exact final home totals.
            need=lambda x:budget[x]//2-home[x]
            h,a=(t,u) if need(t)>need(u) or (need(t)==need(u) and lot(t,u)<lot(u,t)) else (u,t)
            slots=series_slots(friday,3,no_sunday=BYU in (t,u),first=first)
            append(h,a,slots,'nonconference_series');available.remove(t);available.remove(u)
    orient_weekend_series(fixtures,budget,left)
    home=Counter(g['home'] for g in fixtures);away=Counter(g['away'] for g in fixtures)
    # Match remaining games on Tue/Wed. Most-constrained teams go first and
    # reserve later available days; random retry seeds are finite and recorded.
    midweeks=[];day=first
    while day<=max(w['last'] for w in windows.values()):
        if day.weekday() in (1,2):midweeks.append(day)
        day+=timedelta(days=1)
    dates={t:{d for d in midweeks if d<=windows[member[t]]['last'] and all(od!=d for od,_ in occupied[t])} for t in teams}
    for t in teams:require(left[t]<=len(dates[t]),f'Insufficient midweek dates {t}: need {left[t]}, available {len(dates[t])}')
    # Every date receives an even number of teams. A conference may occupy at
    # most half those slots, guaranteeing cross-conference pairability before
    # the separate repeated-opponent constraint is checked.
    from fractions import Fraction
    demand=sum(left.values());require(demand%2==0,'Odd remaining game budget')
    quotas={d:sum((Fraction(left[t],len(dates[t])) for t in teams if d in dates[t]),Fraction())/2 for d in midweeks}
    capacity={d:sum(d in dates[t] for t in teams)//2 for d in midweeks}
    games_per_date={d:min(capacity[d],int(quotas[d])) for d in midweeks}
    remainder=demand//2-sum(games_per_date.values())
    order=sorted(midweeks,key=lambda d:(-(quotas[d]-int(quotas[d])),lot('date',d)))
    for d in order:
        if remainder and games_per_date[d]<capacity[d]:games_per_date[d]+=1;remainder-=1
    require(remainder==0,'Midweek date capacity cannot satisfy all budgets')
    flow=Flow();date_refs={}
    for t in sorted(teams,key=lambda t:(len(dates[t]),lot('date-team',t))):
        flow.add('source',('team',t),left[t])
        for d in sorted(dates[t],key=lambda d:lot('choice',t,d)):
            date_refs[t,d]=flow.add(('team',t),('conf-date',member[t],d),1)
    for cid in profiles:
        for d in midweeks:flow.add(('conf-date',cid,d),('date',d),games_per_date[d])
    for d in midweeks:flow.add(('date',d),'sink',2*games_per_date[d])
    require(flow.run('source','sink')==demand,'Exact midweek date allocation is infeasible for these weekend series')
    singles=[]
    for day in midweeks:
        assigned=[t for t in teams if (t,day) in date_refs and date_refs[t,day][1]==0]
        require(len(assigned)==2*games_per_date[day],'Date allocation count mismatch')
        # Split neighboring states across both sides, so a conference-sorted
        # partition does not force unnecessary long-distance midweek travel.
        ordered=sorted(assigned,key=lambda t:(REGION.get(states[t],99),states[t],lot('partition',day,t)))
        lhs,rhs=ordered[::2],ordered[1::2]
        costs=[[None if member[t]==member[u] or pair_games[pair(t,u)]>=policy['nonconference']['maximum_games_against_same_opponent']
                else geo(t,u)*1000000+pair_games[pair(t,u)]*1000+int.from_bytes(lot('pair',day,t,u)[:2],'big')%1000
                for u in rhs] for t in lhs]
        matching=minimum_cost_assignment(costs)
        for i,j in enumerate(matching):
            t,u=lhs[i],rhs[j]
            append(t,u,[(day,'18:00')],'nonconference_midweek');singles.append(fixtures[-1])
    require(not any(left.values()),'Exact date assignment left unscheduled games')
    # Reassign only single-game hosts; series remain at one park.
    for g in singles:home[g['home']]-=1;away[g['away']]-=1
    orient_singles(singles,{t:budget[t]//2-home[t] for t in teams})
    for g in singles:home[g['home']]+=1;away[g['away']]+=1
    require(all(home[t]==away[t]==budget[t]//2 for t in teams),'Final home/away budget mismatch')
    return {'season':season,'attempt':attempt,'first':first.isoformat(),'selection_deadline':deadline.isoformat(),
            'windows':{c:{k:v.isoformat() if isinstance(v,date) else v for k,v in w.items()} for c,w in windows.items()},
            'budgets':budget,'games':sorted(fixtures,key=lambda g:(g['date'],g['time'],g['home'],g['away'])),
            'exceptions':[{'team_id':t,'regular_games':budget[t],'reason':policy['conference_budget_overrides'][member[t]]['reason']} for t in teams if budget[t]!=policy['default_regular_season_games']],
            'travel_exceptions':[{'date':g['date'],'home':g['home'],'away':g['away'],'reason':'cross_region_midweek'} for g in singles if geo(g['home'],g['away'])==2]}
