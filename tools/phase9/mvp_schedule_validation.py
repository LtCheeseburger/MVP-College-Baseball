"""Independent validation of dated canonical regular-season fixtures."""
from collections import Counter, defaultdict
from datetime import date, timedelta
from .mvp_schedule_graphs import require, pair, validate_conference
from .mvp_schedule_calendar import BYU, series_slots

def validate_fixtures(schedule, conferences, graphs, profiles, policy):
    member={t:c['conference_id'] for c in conferences for t in graphs[c['conference_id']]['teams']}
    require(set(schedule['budgets'])==set(member),'Schedule budgets omit or add an active team')
    counts=Counter();home=Counter();away=Counter();conf_games=Counter();days=defaultdict(list);series=defaultdict(list);pairs=Counter()
    require(schedule['season']>=2027,'Invalid season')
    for g in schedule['games']:
        h,a=g['home'],g['away'];d=date.fromisoformat(g['date'])
        require(h in member and a in member and h!=a,'Inactive, null or self opponent')
        require(d.year==schedule['season'],'Wrong calendar year')
        for t in (h,a):
            w=schedule['windows'][member[t]];require(w['first']<=g['date']<=w['last']<w['event_start'],'Game overlaps postseason or season start')
            counts[t]+=1;days[t,g['date']].append(g)
        home[h]+=1;away[a]+=1;series[g['series_id']].append(g)
        if g['kind']=='conference':
            require(member[h]==member[a]==g['conference_id'],'Wrong conference game owner')
            conf_games[h]+=1;conf_games[a]+=1
        else:
            require(g['kind'] in ('nonconference_series','nonconference_midweek'),'Unknown fixture type')
            require(member[h]!=member[a] and g['conference_id'] is None,'Nonconference game is intraconference')
            pairs[pair(h,a)]+=1
        if g['kind']=='nonconference_midweek':require(d.weekday() in (1,2) and g['time']=='18:00','Midweek placement/time')
        require(BYU not in (h,a) or d.weekday()!=6,'BYU Sunday game')
    for (team,day),gs in days.items():
        require(len(gs)<=2 and len({g['time'] for g in gs})==len(gs),'Overlapping or excessive daily fixtures')
        require(len({(g['home'],g['away'],g['series_id']) for g in gs})==1,'Doubleheader changes opponent or park')
        require(len(gs)==1 or sorted(g['time'] for g in gs)==['12:00','16:00'],'Doubleheader timing')
    conf_series=defaultdict(list)
    for sid,gs in series.items():
        head=gs[0];kind=head['kind']
        require(all((g['home'],g['away'],g['kind'],g['conference_id'],g['conference_round'])==(head['home'],head['away'],kind,head['conference_id'],head['conference_round']) for g in gs),'Series changes identity/site')
        if kind=='nonconference_midweek':require(len(gs)==1,'Repeated midweek series ID');continue
        require(len(gs) in (3,4),'Wrong series game count')
        dates=[date.fromisoformat(g['date']) for g in gs];start=min(dates)
        friday=start+timedelta(days=4-start.weekday())
        cid=head['conference_id'];key=next((c['engine_key'] for c in conferences if c['conference_id']==cid),None)
        saturday=kind=='conference' and profiles[cid]['regular_season']['calendar_policy']['series_window']=='Saturday-Sunday'
        no_sunday=BYU in (head['home'],head['away']) or (key==4 and head['conference_round']==graphs[cid]['rounds']-1)
        expected=series_slots(friday,len(gs),saturday,no_sunday,date.fromisoformat(schedule['first']))
        require(sorted((g['date'],g['time']) for g in gs)==sorted((d.isoformat(),t) for d,t in expected),'Series cadence differs from policy')
        if kind=='conference':conf_series[cid].append({'home':head['home'],'away':head['away'],'games':len(gs),'round':head['conference_round']})
    for c in conferences:
        cid=c['conference_id'];validate_conference(graphs[cid]['teams'],profiles[cid],conf_series[cid])
        canonical=lambda s:(s['home'],s['away'],s['games'],s['round'])
        require(sorted(map(canonical,conf_series[cid]))==sorted(map(canonical,graphs[cid]['series'])),'Dated games changed authored conference graph')
    per_team={}
    for t in member:
        budget=policy['conference_budget_overrides'].get(member[t],{}).get('games',policy['default_regular_season_games'])
        require(schedule['budgets'][t]==budget==counts[t],'Team regular game budget')
        require(home[t]==away[t]==budget//2,'Team home/away budget')
        require(conf_games[t]==profiles[member[t]]['regular_season']['games_per_team'],'Team conference game budget')
        per_team[t]={'games':counts[t],'home':home[t],'away':away[t],'conference_games':conf_games[t],'nonconference_games':counts[t]-conf_games[t]}
    require(max(pairs.values())<=policy['nonconference']['maximum_games_against_same_opponent'],'Repeated nonconference opponent budget')
    return {'result':'passed','active_teams':len(member),'conferences':len(conferences),'games':len(schedule['games']),
            'conference_games':sum(conf_games.values())//2,'nonconference_games':sum(pairs.values()),
            'every_team_budget_checked':True,'every_fixture_checked':True,'per_team':per_team,
            'explicit_budget_exceptions':len(schedule['exceptions']),'cross_region_midweek_exceptions':len(schedule['travel_exceptions']),
            'unsatisfied_constraints':[],'runtime_import_verified':False,'annual_runtime_generation_verified':False}
