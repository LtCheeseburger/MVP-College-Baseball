"""Compile the kit's authored tournament graphs against this league and annual calendar.

Pure profile compilation adapted from the kit's probe-*-postseason scripts.
No private proof paths, translated functions, or donor wide storage are used.
"""
import json
from datetime import date
from pathlib import Path
import sys

def four_team_de(rules, identities, windows, output):
    profiles=[]
    for key in ('ncaa-conf:30184','ncaa-conf:819','ncaa-conf:846','ncaa-conf:865'):
     r=rules['conferences'][key]['tournament'];ids=identities['conferences'][key];w=windows[key];start=date.fromisoformat(w['event_start'])
     if r['field_size']!=4 or len(r['stages'])!=1 or r['stages'][0]['routing']['template']!='double_elimination_4' or r['eligibility']['conference_ineligible_team_ids'] or (date.fromisoformat(w['event_end'])-start).days!=3:raise ValueError('DE4 authored profile differs')
     profiles.append((ids['engine_key'],ids['regular_slot'],ids['tournament_slot'],29221+(start-date(2028,1,1)).days))
    graph=rules['bracket_templates']['double_elimination_4']['games'];routes=[[[0xFFFFFFFF,2] for _ in range(2)] for _ in graph]
    if len(graph)!=7:raise ValueError('Unexpected authored graph length')
    for i,g in enumerate(graph):
     if g['game']!=i+1 or (g.get('condition') is not None and (i!=6 or g['condition']!='play_only_if_undefeated_finalist_lost_previous_final')):raise ValueError('Unsupported graph condition')
     for side,token in enumerate(g['participants']):
      if token[0]=='s':continue
      number=int(token[1:])-1
      if token[0] not in 'wl' or not 0<=number<i or routes[number][token[0]=='l']!=[0xFFFFFFFF,2]:raise ValueError('Graph forward reference/fanout')
      routes[number][token[0]=='l']=[i,side]
    def cpp(value):return '{'+','.join(cpp(v) if isinstance(v,list) else str(v)+'u' for v in value)+'}'
    config=output/'four-team-de-config.h';config.write_text('#pragma once\nnamespace mvp::four_team_de { struct Profile {uint32_t key,regular,slot,start;}; inline constexpr Profile kProfiles[]='+cpp([list(p) for p in profiles])+'; inline constexpr uint32_t kRoutes[7][2][2]='+cpp(routes)+'; }\n')
    return [p[0] for p in profiles]

def six_team_de(rules, identities, windows, output):
    profiles=[]
    for key in ('ncaa-conf:820','ncaa-conf:826','ncaa-conf:884'):
     r=rules['conferences'][key]['tournament'];ids=identities['conferences'][key];w=windows[key];start=date.fromisoformat(w['event_start'])
     if r['field_size']!=6 or len(r['stages'])!=1 or r['stages'][0]['routing']['template']!='double_elimination_6' or r['qualification']['mode']!='overall_standings' or r['eligibility']['conference_ineligible_team_ids'] or (date.fromisoformat(w['event_end'])-start).days!=3:raise ValueError('DE6 authored profile differs')
     profiles.append((ids['engine_key'],ids['regular_slot'],ids['tournament_slot'],29221+(start-date(2028,1,1)).days))
    graph=rules['bracket_templates']['double_elimination_6']['games'];initial=[[0xFFFFFFFF]*2 for _ in graph];routes=[[[0xFFFFFFFF,2] for _ in range(2)] for _ in graph]
    if len(graph)!=11:raise ValueError('Unexpected authored graph length')
    for i,g in enumerate(graph):
     if g['game']!=i+1 or (g.get('condition') is not None and (i!=10 or g['condition']!='play_only_if_undefeated_finalist_lost_previous_final')):raise ValueError('Unsupported graph condition')
     for side,token in enumerate(g['participants']):
      number=int(token[1:])-1
      if token[0]=='s':
       if not 0<=number<6:raise ValueError('Seed bounds')
       initial[i][side]=number
      elif token[0] in 'wl':
       if not 0<=number<i or routes[number][token[0]=='l']!=[0xFFFFFFFF,2]:raise ValueError('Graph forward reference/fanout')
       routes[number][token[0]=='l']=[i,side]
      else:raise ValueError('Unknown authored token')
    def cpp(value):return '{'+','.join(cpp(v) if isinstance(v,list) else str(v)+'u' for v in value)+'}'
    config=output/'six-team-de-config.h';config.write_text('#pragma once\nnamespace mvp::six_team_de { struct Profile {uint32_t key,regular,slot,start;}; inline constexpr Profile kProfiles[]='+cpp([list(p) for p in profiles])+'; inline constexpr uint32_t kInitialSeeds[11][2]='+cpp(initial)+'; inline constexpr uint32_t kRoutes[11][2][2]='+cpp(routes)+'; }\n')
    return [p[0] for p in profiles]

def large_de(rules, identities, windows, output):
    profiles=[]
    calendar={7:([0,0,0,1,1,1,1,2,2,2,3,3,3],[540,780,1020,540,720,900,1080,540,780,1020,600,840,1080]),8:([0,0,1,1,2,2,3,3,5,5,6,6,7,8,9],[780,1020,780,1020,780,1020,780,1020,780,1020,780,1020,780,780,780])}
    for key in ('ncaa-conf:905','ncaa-conf:923'):
     r=rules['conferences'][key]['tournament'];ids=identities['conferences'][key];w=windows[key];start=date.fromisoformat(w['event_start']);field=r['field_size'];graph=rules['bracket_templates']['double_elimination_'+str(field)]['games'];count=len(graph)
     if field not in (7,8) or len(r['stages'])!=1 or r['stages'][0]['routing']['template']!='double_elimination_'+str(field) or r['qualification']['mode']!='overall_standings' or r['eligibility']['conference_ineligible_team_ids'] or count!=2*field-1:raise ValueError('Authored large DE profile differs')
     days,minutes=calendar[field]
     if days[-1]!=(date.fromisoformat(w['event_end'])-start).days:raise ValueError('Authored event/calendar span differs')
     initial=[[0xFFFFFFFF]*2 for _ in range(15)];routes=[[[0xFFFFFFFF,2] for _ in range(2)] for _ in range(15)]
     for i,g in enumerate(graph):
      if g['game']!=i+1 or (g.get('condition') is not None and (i!=count-1 or g['condition']!='play_only_if_undefeated_finalist_lost_previous_final')):raise ValueError('Unsupported graph condition')
      for side,token in enumerate(g['participants']):
       number=int(token[1:])-1
       if token[0]=='s':
        if not 0<=number<field:raise ValueError('Seed bounds')
        initial[i][side]=number
       elif token[0] in 'wl':
        if not 0<=number<i or routes[number][token[0]=='l']!=[0xFFFFFFFF,2]:raise ValueError('Graph forward reference/fanout')
        routes[number][token[0]=='l']=[i,side]
       else:raise ValueError('Unknown authored token')
     profiles.append([ids['engine_key'],ids['regular_slot'],ids['tournament_slot'],29221+(start-date(2028,1,1)).days,field,count,2*field+2,initial,routes,days+[0]*(15-count),minutes+[0]*(15-count)])
    def cpp(value):return '{'+','.join(cpp(v) if isinstance(v,list) else str(v)+'u' for v in value)+'}'
    config=output/'large-de-config.h';config.write_text('#pragma once\nnamespace mvp::large_de { struct Profile {uint32_t key,regular,slot,start,field,games,capacity,initial[15][2],routes[15][2][2],days[15],minutes[15];}; inline constexpr Profile kProfiles[]='+cpp(profiles)+'; }\n')
    return [p[0] for p in profiles]

def single_elimination(rules, identities, windows, output):
    profiles=[]
    calendar={16:([0,0,0,0,1,1,1,1,2,2,3,3,4,4,5],[540,720,900,1080,540,720,900,1080,780,1020,780,1020,780,1020,780]),12:([0,0,1,1,1,2,2,2,3,3,4],[780,1020,600,840,1080,600,840,1080,780,1020,780])}
    authored_graphs={}
    for key in ('ncaa-conf:821','ncaa-conf:25354','ncaa-conf:911'):
     r=rules['conferences'][key]['tournament'];ids=identities['conferences'][key];w=windows[key];start=date.fromisoformat(w['event_start']);field=r['field_size']
     if field not in (12,16) or any(v['kind']!='single_elimination' for v in r['stages']) or r['qualification']['mode']!='overall_standings' or r['eligibility']['conference_ineligible_team_ids']:raise ValueError('Authored single-elimination profile differs')
     graph=r.get('game_graph')
     if graph is None:
      graph=[];prior=[]
      for stage_index,stage in enumerate(r['stages']):
       routing=stage['routing']
       if routing['template']!='single_elimination_seeded' or routing['subsequent_pairing']!='fixed_bracket_no_reseed':raise ValueError('Unsupported SE stage routing')
       tokens=[]
       for i in range(1,stage['entrants']+1):
        ref=routing['slot_assignment'][str(i)].split(':')
        if ref[0]=='seed':tokens.append('s'+ref[1])
        elif ref[:2]==['stage',str(stage_index-1)] and ref[2]=='advancer':tokens.append(prior[int(ref[3])-1])
        else:raise ValueError('Unrecognized authored stage input')
       first=True
       while len(tokens)>stage['advancers']:
        pairs=[(tokens[i],tokens[-1-i]) for i in range(len(tokens)//2)] if first else list(zip(tokens[::2],tokens[1::2]))
        next_round=[]
        for pair in pairs:
         graph.append({'game':len(graph)+1,'participants':list(pair)});next_round.append('w'+str(len(graph)))
        tokens=next_round;first=False
       prior=tokens
     count=len(graph);authored_graphs[key]=graph
     if count!=field-1:raise ValueError('Single-elimination game count')
     days,minutes=calendar[field]
     if days[-1]!=(date.fromisoformat(w['event_end'])-start).days:raise ValueError('Authored event/calendar span differs')
     initial=[[0xFFFFFFFF]*2 for _ in range(15)];routes=[[[0xFFFFFFFF,2] for _ in range(2)] for _ in range(15)]
     for i,g in enumerate(graph):
      if g['game']!=i+1 or g.get('condition') is not None:raise ValueError('Unsupported graph condition')
      for side,token in enumerate(g['participants']):
       number=int(token[1:])-1
       if token[0]=='s':
        if not 0<=number<field:raise ValueError('Seed bounds')
        initial[i][side]=number
       elif token[0]=='w':
        if not 0<=number<i or routes[number][token[0]=='l']!=[0xFFFFFFFF,2]:raise ValueError('Graph forward reference/fanout')
        routes[number][token[0]=='l']=[i,side]
       else:raise ValueError('Unknown authored token')
     profiles.append([ids['engine_key'],ids['regular_slot'],ids['tournament_slot'],29221+(start-date(2028,1,1)).days,field,count,count+3,initial,routes,days+[0]*(15-count),minutes+[0]*(15-count)])
    def cpp(value):return '{'+','.join(cpp(v) if isinstance(v,list) else str(v)+'u' for v in value)+'}'
    config=output/'single-elimination-config.h';config.write_text('#pragma once\nnamespace mvp::single_elimination { struct Profile {uint32_t key,regular,slot,start,field,games,capacity,initial[15][2],routes[15][2][2],days[15],minutes[15];}; inline constexpr Profile kProfiles[]='+cpp(profiles)+'; }\n')
    return [p[0] for p in profiles]

def hybrid_postseason(rules, identities, windows, output):
    profiles=[]
    calendar={};authored_graphs={}
    keys=('ncaa-conf:845','ncaa-conf:881','ncaa-conf:902','ncaa-conf:904','ncaa-conf:922','ncaa-conf:5486')
    for key in keys:
     r=rules['conferences'][key]['tournament'];ids=identities['conferences'][key];w=windows[key];start=date.fromisoformat(w['event_start']);field=r['field_size'];stages=r['stages']
     if field not in (5,6,8) or len(stages)!=2 or stages[0]['kind']!='single_elimination' or stages[1]['kind']!='double_elimination' or r['qualification']['mode']!='overall_standings' or r['eligibility']['conference_ineligible_team_ids'] or any(v['routing'].get('pairing_adjustment') for v in stages):raise ValueError('Authored hybrid profile differs')
     first=stages[0];refs=first['routing']['participants'];playins=first['entrants']//2
     if first['advancers']!=playins or refs!=['seed:'+str(v) for v in first['seed_pool']]:raise ValueError('Unsupported play-in structure')
     pairs=[[refs[i],refs[-1-i]] for i in range(playins)]
     graph=[{'game':i+1,'participants':['s'+v.split(':')[1] for v in pair]} for i,pair in enumerate(pairs)]
     second=stages[1];de_field=second['entrants']
     if de_field not in (4,6) or second['advancers']!=1 or second['routing']['template']!='double_elimination_'+str(de_field) or second['routing']['carry_prior_stage_losses']:raise ValueError('Unsupported DE continuation')
     refs=second['routing']['slot_assignment'];template=rules['bracket_templates']['double_elimination_'+str(de_field)]['games']
     for node in template:
      tokens=[]
      for token in node['participants']:
       if token[0]=='s':
        ref=refs[token[1:]].split(':')
        if ref[0]=='seed':tokens.append('s'+ref[1])
        elif ref[:3]==['stage','0','advancer']:tokens.append('w'+ref[3])
        else:raise ValueError('Unsupported hybrid slot input')
       else:tokens.append(token[0]+str(int(token[1:])+playins))
      graph.append({**node,'game':node['game']+playins,'participants':tokens})
     count=len(graph);authored_graphs[key]=graph;span=(date.fromisoformat(w['event_end'])-start).days
     if (field,span)==(5,3):days,minutes=[0,1,1,2,2,2,3,3],[780,600,840,540,780,1020,660,900]
     elif (field,span)==(5,4):days,minutes=[0,1,1,2,2,3,4,4],[780,600,840,600,840,780,660,900]
     elif (field,span)==(6,3):days,minutes=[0,0,1,1,2,2,2,3,3],[600,840,600,840,540,780,1020,660,900]
     elif (field,span)==(8,4):days,minutes=[0,0,1,1,1,2,2,2,3,3,3,4,4],[600,840,540,780,1020,540,780,1020,540,780,1020,660,900]
     else:raise ValueError('Unreviewed event window')
     if len(days)!=count or days[-1]!=span:raise ValueError('Hybrid calendar count/span')
     calendar[key]=dict(days=days,minutes=minutes,field=field,playins=playins,de_field=de_field)
     initial=[[0xFFFFFFFF]*2 for _ in range(15)];routes=[[[0xFFFFFFFF,2] for _ in range(2)] for _ in range(15)]
     for i,g in enumerate(graph):
      if g['game']!=i+1 or (g.get('condition') is not None and (i!=count-1 or g['condition']!='play_only_if_undefeated_finalist_lost_previous_final')):raise ValueError('Unsupported graph condition')
      for side,token in enumerate(g['participants']):
       number=int(token[1:])-1
       if token[0]=='s':
        if not 0<=number<field:raise ValueError('Seed bounds')
        initial[i][side]=number
       elif token[0] in 'wl':
        if not 0<=number<i or routes[number][token[0]=='l']!=[0xFFFFFFFF,2]:raise ValueError('Graph forward reference/fanout')
        routes[number][token[0]=='l']=[i,side]
       else:raise ValueError('Unknown authored token')
     profiles.append([ids['engine_key'],ids['regular_slot'],ids['tournament_slot'],29221+(start-date(2028,1,1)).days,field,count,count+3,playins,de_field,initial,routes,days+[0]*(15-count),minutes+[0]*(15-count)])
    def cpp(value):return '{'+','.join(cpp(v) if isinstance(v,list) else str(v)+'u' for v in value)+'}'
    config=output/'hybrid-postseason-config.h';config.write_text('#pragma once\nnamespace mvp::hybrid_postseason { struct Profile {uint32_t key,regular,slot,start,field,games,capacity,playins,de_field,initial[15][2],routes[15][2][2],days[15],minutes[15];}; inline constexpr Profile kProfiles[]='+cpp(profiles)+'; }\n')
    return [p[0] for p in profiles]

def twin_brackets(rules, identities, windows, output):
    profiles=[]
    calendar={};authored_graphs={}
    keys=('ncaa-conf:818','ncaa-conf:916')
    for key in keys:
     r=rules['conferences'][key]['tournament'];ids=identities['conferences'][key];w=windows[key];start=date.fromisoformat(w['event_start']);field=r['field_size'];stages=r['stages']
     playins=2 if key=='ncaa-conf:818' else 0;stage=stages[-1];groups=stage['routing']['groups']
     if stage['kind']!='two_double_elimination_brackets' or stage['teams_per_bracket']!=4 or stage['advancers']!=2 or r['championship_final']!='single_game' or r['qualification']['mode']!='overall_standings' or r['eligibility']['conference_ineligible_team_ids'] or r['reseed']:raise ValueError('Authored twin-bracket shape differs')
     graph=[]
     if playins:
      if stages[0]['opening_seed_pairs']!=[[8,9],[7,10]]:raise ValueError('Sun Belt play-in order differs')
      graph=[{'game':i+1,'participants':['s'+str(v) for v in pair]} for i,pair in enumerate(stages[0]['opening_seed_pairs'])]
      order=[(0,0),(0,1),(1,0),(1,1)]+[(group,game) for game in range(2,7) for group in range(2)]
     else:order=[(0,1),(0,0),(1,1),(1,0)]+[(group,game) for game in range(2,7) for group in range(2)]
     positions={pair:playins+i for i,pair in enumerate(order)}
     template=rules['bracket_templates']['double_elimination_4']['games']
     for group,game in order:
      tokens=[]
      for token in template[game]['participants']:
       if token[0]=='s':
        ref=groups[group][int(token[1:])-1].split(':')
        if ref[0]=='seed':tokens.append('s'+ref[1])
        elif ref[:3]==['stage','0','advancer'] and playins:tokens.append('w'+ref[3])
        else:raise ValueError('Unsupported group seed source')
       else:tokens.append(token[0]+str(positions[group,int(token[1:])-1]+1))
      graph.append({'game':len(graph)+1,'participants':tokens,'group':group,**({'condition':template[game]['condition']} if 'condition' in template[game] else {})})
     first_final=[positions[g,5] for g in range(2)];resets=[positions[g,6] for g in range(2)]
     graph.append({'game':len(graph)+1,'participants':['c0','c1'],'stage':'single_game_championship'})
     if not playins and [g['participants'] for g in graph[:4]]!=[['s'+str(v) for v in pair] for pair in stage['opening_seed_pairs']]:raise ValueError('SWAC published opening order differs')
     count=len(graph);authored_graphs[key]=graph;span=(date.fromisoformat(w['event_end'])-start).days
     # At the fixed site, four-game days use 10am/1pm/4pm/7pm. These are project times.
     group_days=[0]*4+[1]*4+[2]*2+[3]*4+[4]
     group_minutes=[600,780,960,1140]*2+[780,960]+[600,780,960,1140]+[840]
     days=([0,0]+[d+1 for d in group_days]) if playins else group_days
     minutes=([780,960]+group_minutes) if playins else group_minutes
     if len(days)!=count or days[-1]!=span:raise ValueError('Unreviewed event span')
     calendar[key]=dict(days=days,minutes=minutes,field=field,playins=playins,first_final=first_final,resets=resets)
     initial=[[0xFFFFFFFF]*2 for _ in range(17)];routes=[[[0xFFFFFFFF,2] for _ in range(2)] for _ in range(17)]
     for i,g in enumerate(graph[:-1]):
      for side,token in enumerate(g['participants']):
       number=int(token[1:])-1
       if token[0]=='s':initial[i][side]=number
       else:
        if not 0<=number<i or routes[number][token[0]=='l']!=[0xFFFFFFFF,2]:raise ValueError('Forward route or duplicate source')
        routes[number][token[0]=='l']=[i,side]
     for group,game in enumerate(resets):routes[game][0]=[count-1,group]
     profiles.append([ids['engine_key'],ids['regular_slot'],ids['tournament_slot'],29221+(start-date(2028,1,1)).days,field,count,count+3,playins,first_final,resets,initial,routes,days+[0]*(17-count),minutes+[0]*(17-count)])
    def cpp(value):return '{'+','.join(cpp(v) if isinstance(v,list) else str(v)+'u' for v in value)+'}'
    config=output/'twin-bracket-config.h';config.write_text('#pragma once\nnamespace mvp::twin_bracket { struct Profile {uint32_t key,regular,slot,start,field,games,capacity,playins,first_final[2],reset[2],initial[17][2],routes[17][2][2],days[17],minutes[17];}; inline constexpr Profile kProfiles[]='+cpp(profiles)+'; }\n')
    return [p[0] for p in profiles]

def southland_postseason(rules, identities, windows, output):
    profiles=[]
    calendar={};authored_graphs={}
    keys=('ncaa-conf:914',)
    for key in keys:
     r=rules['conferences'][key]['tournament'];ids=identities['conferences'][key];w=windows[key];start=date.fromisoformat(w['event_start']);field=r['field_size'];stages=r['stages'];stage=stages[0];groups=stage['routing']['groups']
     if field!=8 or stage['kind']!='two_double_elimination_brackets' or stage['teams_per_bracket']!=4 or stage['advancers']!=2 or stages[1]['kind']!='best_of_three_final_series' or stages[1]['routing']['wins_to_advance']!=2 or stages[1]['routing']['maximum_games_per_pair']!=3 or r['championship_final']!='best_of_three' or r['qualification']['mode']!='overall_standings' or r['eligibility']['conference_ineligible_team_ids'] or r['reseed']:raise ValueError('Authored Southland shape differs')
     order=[(0,0),(0,1),(1,0),(1,1)]+[(group,game) for game in range(2,7) for group in range(2)]
     positions={pair:i for i,pair in enumerate(order)};template=rules['bracket_templates']['double_elimination_4']['games'];graph=[]
     for group,game in order:
      tokens=[]
      for token in template[game]['participants']:
       if token[0]=='s':
        ref=groups[group][int(token[1:])-1].split(':')
        if ref[0]!='seed':raise ValueError('Unsupported Southland seed source')
        tokens.append('s'+ref[1])
       else:tokens.append(token[0]+str(positions[group,int(token[1:])-1]+1))
      graph.append({'game':len(graph)+1,'participants':tokens,'group':group,**({'condition':template[game]['condition']} if 'condition' in template[game] else {})})
     first_final=[positions[g,5] for g in range(2)];resets=[positions[g,6] for g in range(2)]
     graph.extend([{'game':15,'participants':['c0','c1'],'stage':'championship_series'},{'game':16,'participants':['w15','l15'],'stage':'championship_series'},{'game':17,'participants':['w16','l16'],'stage':'championship_series','condition':'series_not_clinched'}])
     count=len(graph);authored_graphs[key]=graph;span=(date.fromisoformat(w['event_end'])-start).days
     days=[0]*4+[1]*4+[2]*2+[3]*4+[7,8,9];minutes=[600,780,960,1140]*2+[780,960]+[600,780,960,1140]+[780]*3
     if len(days)!=count or span!=9:raise ValueError('Unreviewed Southland event span')
     calendar[key]=dict(days=days,minutes=minutes,field=field,first_final=first_final,resets=resets,championship_series=[14,15,16])
     initial=[[0xFFFFFFFF]*2 for _ in range(17)];routes=[[[0xFFFFFFFF,2] for _ in range(2)] for _ in range(17)]
     for i,g in enumerate(graph):
      for side,token in enumerate(g['participants']):
       if token[0]=='c':continue
       number=int(token[1:])-1
       if token[0]=='s':initial[i][side]=number
       else:
        if not 0<=number<i or routes[number][token[0]=='l']!=[0xFFFFFFFF,2]:raise ValueError('Forward route or duplicate source')
        routes[number][token[0]=='l']=[i,side]
     for group,game in enumerate(resets):routes[game][0]=[14,group]
     profiles.append([ids['engine_key'],ids['regular_slot'],ids['tournament_slot'],29221+(start-date(2028,1,1)).days,field,count,count+3,0,first_final,resets,initial,routes,days,minutes])
    def cpp(value):return '{'+','.join(cpp(v) if isinstance(v,list) else str(v)+'u' for v in value)+'}'
    config=output/'southland-postseason-config.h';config.write_text('#pragma once\nnamespace mvp::southland_postseason { struct Profile {uint32_t key,regular,slot,start,field,games,capacity,playins,first_final[2],reset[2],initial[17][2],routes[17][2][2],days[17],minutes[17];}; inline constexpr Profile kProfiles[]='+cpp(profiles)+'; }\n')
    return [p[0] for p in profiles]

def staggered_postseason(rules, identities, windows, output):
    profiles=[]
    calendar={};authored_graphs={}
    keys=('ncaa-conf:823','ncaa-conf:912')
    for key in keys:
     r=rules['conferences'][key]['tournament'];ids=identities['conferences'][key];w=windows[key];start=date.fromisoformat(w['event_start']);field=r['field_size'];stages=r['stages']
     if field!=8 or len(stages)!=2 or stages[0]['kind']!='single_elimination' or stages[1]['kind']!='two_staggered_three_team_double_elimination_brackets' or r['championship_final']!='single_game' or r['qualification']['mode']!='overall_standings' or r['eligibility']['conference_ineligible_team_ids'] or r['reseed']:raise ValueError('Authored staggered shape differs')
     first,second=stages;groups=second['routing']['groups'];pairs=first['opening_seed_pairs']
     if len(pairs)!=2 or sorted(v for pair in pairs for v in pair)!=[5,6,7,8] or first['advancers']!=2 or second['advancers']!=2 or second['teams_per_bracket']!=3 or second['routing']['carry_prior_stage_losses']:raise ValueError('Unsupported staggered groups/play-ins')
     graph=[{'game':i+1,'participants':['s'+str(v) for v in pair],'stage':'playin'} for i,pair in enumerate(pairs)]
     def token(ref):
      parts=ref.split(':')
      if parts[0]=='seed':return 's'+parts[1]
      if parts[:3]==['stage','0','advancer']:return 'w'+parts[3]
      raise ValueError('Unsupported staggered source')
     for step in range(5):
      for group in range(2):
       g=groups[group];early=3+group;second_game=5+group;loser_game=7+group;first_final=9+group
       tokens=([token(v) for v in g['opening']] if step==0 else ['w'+str(early),token(g['late_entry'])] if step==1 else ['l'+str(early),'l'+str(second_game)] if step==2 else ['w'+str(second_game),'w'+str(loser_game)] if step==3 else ['w'+str(first_final),'l'+str(first_final)])
       graph.append({'game':len(graph)+1,'participants':tokens,'group':group,**({'condition':'play_only_if_undefeated_finalist_lost_previous_final'} if step==4 else {})})
     graph.append({'game':13,'participants':['c0','c1'],'stage':'championship'})
     count=len(graph);authored_graphs[key]=graph;span=(date.fromisoformat(w['event_end'])-start).days
     days=([0,0,1,1,2,2,2,2,3,3,3,3,4] if key=='ncaa-conf:823' else [0,0,1,1,2,2,3,3,4,4,4,4,5])
     minutes=[600,840,600,840,600,780,960,1140,600,780,960,1140,780]
     first_final=[8,9];resets=[10,11]
     if len(days)!=count or days[-1]!=span:raise ValueError('Unreviewed staggered event span')
     calendar[key]=dict(days=days,minutes=minutes,field=field,first_final=first_final,resets=resets,playins=2)
     initial=[[0xFFFFFFFF]*2 for _ in range(13)];routes=[[[0xFFFFFFFF,2] for _ in range(2)] for _ in range(13)]
     for i,g in enumerate(graph):
      for side,t in enumerate(g['participants']):
       if t[0]=='c':continue
       number=int(t[1:])-1
       if t[0]=='s':initial[i][side]=number
       else:
        if not 0<=number<i or routes[number][t[0]=='l']!=[0xFFFFFFFF,2]:raise ValueError('Forward route or duplicate source')
        routes[number][t[0]=='l']=[i,side]
     for group,game in enumerate(resets):routes[game][0]=[12,group]
     profiles.append([ids['engine_key'],ids['regular_slot'],ids['tournament_slot'],29221+(start-date(2028,1,1)).days,field,count,count+3,2,first_final,resets,initial,routes,days,minutes])
    def cpp(value):return '{'+','.join(cpp(v) if isinstance(v,list) else str(v)+'u' for v in value)+'}'
    config=output/'staggered-postseason-config.h';config.write_text('#pragma once\nnamespace mvp::staggered_postseason { struct Profile {uint32_t key,regular,slot,start,field,games,capacity,playins,first_final[2],reset[2],initial[13][2],routes[13][2][2],days[13],minutes[13];}; inline constexpr Profile kProfiles[]='+cpp(profiles)+'; }\n')
    return [p[0] for p in profiles]

def bigten_postseason(rules, identities, windows, output):
    profiles=[]
    calendar={};authored_graphs={}
    keys=('ncaa-conf:827',)
    for key in keys:
     r=rules['conferences'][key]['tournament'];ids=identities['conferences'][key];w=windows[key];start=date.fromisoformat(w['event_start']);field=r['field_size'];stages=r['stages']
     if field!=12 or len(stages)!=2 or stages[0]['kind']!='double_elimination_qualifier' or stages[1]['kind']!='single_elimination' or r['qualification']['mode']!='overall_standings' or r['eligibility']['conference_ineligible_team_ids'] or r['reseed']:raise ValueError('Authored Big Ten shape differs')
     groups=stages[0]['routing']['seed_groups']
     if groups!=[[5,8,9,12],[6,7,10,11]] or stages[0]['advancers']!=4 or stages[0]['routing']['games_per_group']!=5 or stages[1]['entering_bye_seeds']!=[1,2,3,4] or stages[1]['routing']['carry_prior_stage_losses']:raise ValueError('Unsupported Big Ten qualifiers')
     if stages[1]['routing']['slot_assignment']!={str(i):('seed:'+str(i) if i<=4 else 'stage:0:advancer:'+str(i-4)) for i in range(1,9)}:raise ValueError('Big Ten final field slots differ')
     # Two four-team five-game qualifiers, each sending its undefeated winner
     # and last one-loss survivor to the authored eight-position final field.
     graph=[]
     for group,seeds in enumerate(groups):
      for left,right in ((0,3),(1,2)):graph.append({'game':len(graph)+1,'participants':['s'+str(seeds[left]),'s'+str(seeds[right])],'group':group})
     for group in range(2):graph.append({'game':len(graph)+1,'participants':['w'+str(1+2*group),'w'+str(2+2*group)],'group':group})
     for group in range(2):graph.append({'game':len(graph)+1,'participants':['l'+str(1+2*group),'l'+str(2+2*group)],'group':group})
     for group in range(2):graph.append({'game':len(graph)+1,'participants':['l'+str(5+group),'w'+str(7+group)],'group':group})
     slots=['s1','s2','s3','s4','w5','w9','w6','w10']
     for left,right in ((0,7),(3,4),(1,6),(2,5)):graph.append({'game':len(graph)+1,'participants':[slots[left],slots[right]],'stage':'quarterfinal'})
     graph.extend([{'game':15,'participants':['w11','w12'],'stage':'semifinal'},{'game':16,'participants':['w13','w14'],'stage':'semifinal'},{'game':17,'participants':['w15','w16'],'stage':'final'}])
     count=len(graph);authored_graphs[key]=graph;span=(date.fromisoformat(w['event_end'])-start).days
     days=[0]*4+[1]*4+[2]*2+[3]*4+[4]*2+[5]
     minutes=[600,780,960,1140]*2+[720,960]+[600,780,960,1140]+[780,1020,780]
     if count!=17 or len(days)!=count or span!=5:raise ValueError('Unreviewed Big Ten event span')
     calendar[key]=dict(days=days,minutes=minutes,field=field,qualifier_games=10,qualifier_outputs=[4,8,5,9],single_elimination_start=10)
     initial=[[0xFFFFFFFF]*2 for _ in range(17)];routes=[[[0xFFFFFFFF,2] for _ in range(2)] for _ in range(17)]
     for i,g in enumerate(graph):
      for side,t in enumerate(g['participants']):
       number=int(t[1:])-1
       if t[0]=='s':initial[i][side]=number
       else:
        if not 0<=number<i or routes[number][t[0]=='l']!=[0xFFFFFFFF,2]:raise ValueError('Forward route or duplicate source')
        routes[number][t[0]=='l']=[i,side]
     profiles.append([ids['engine_key'],ids['regular_slot'],ids['tournament_slot'],29221+(start-date(2028,1,1)).days,field,count,count+3,initial,routes,days,minutes])
    def cpp(value):return '{'+','.join(cpp(v) if isinstance(v,list) else str(v)+'u' for v in value)+'}'
    config=output/'bigten-postseason-config.h';config.write_text('#pragma once\nnamespace mvp::bigten_postseason { struct Profile {uint32_t key,regular,slot,start,field,games,capacity,initial[17][2],routes[17][2][2],days[17],minutes[17];}; inline constexpr Profile kProfiles[]='+cpp(profiles)+'; }\n')
    return [p[0] for p in profiles]

def cusa_postseason(rules, identities, windows, output):
    profiles=[]
    calendar={};authored_graphs={}
    keys=('ncaa-conf:24312',)
    for key in keys:
     r=rules['conferences'][key]['tournament'];ids=identities['conferences'][key];w=windows[key];start=date.fromisoformat(w['event_start']);field=r['field_size'];stages=r['stages']
     if field!=9 or len(stages)!=2 or stages[0]['kind']!='round_robin_pools' or stages[1]['kind']!='single_elimination' or r['qualification']['mode']!='overall_standings' or r['eligibility']['conference_ineligible_team_ids'] or r['reseed']:raise ValueError('Authored CUSA shape differs')
     pools=stages[0]['pools']
     if pools!=[[1,6,9],[2,5,8],[3,4,7]] or stages[0]['wildcards']!=1 or not stages[0]['pool_winners_advance'] or stages[0]['routing']['pool_ranking']!=['pool_win_pct','head_to_head','highest_original_seed'] or stages[0]['routing']['wildcard_ranking']!=['pool_win_pct','highest_original_seed']:raise ValueError('Unsupported CUSA pool qualification')
     if stages[1]['routing']['carry_prior_stage_losses'] or stages[1]['routing']['opening_pairing']!='highest_vs_lowest_original_seed':raise ValueError('CUSA semifinal pairing differs')
     graph=[]
     for first,second in ((0,1),(0,2),(1,2)):
      for group,seeds in enumerate(pools):graph.append(dict(game=len(graph)+1,participants=['s'+str(seeds[first]),'s'+str(seeds[second])],pool=group))
     graph.extend([dict(game=10,participants=['q1','q4']),dict(game=11,participants=['q2','q3']),dict(game=12,participants=['w10','w11'])])
     count=len(graph);authored_graphs[key]=graph;span=(date.fromisoformat(w['event_end'])-start).days
     days=[0]*3+[1]*3+[2]*3+[3]*2+[4];minutes=[600,840,1080]*3+[780,1020,780]
     if count!=12 or len(days)!=count or span!=4:raise ValueError('Unreviewed CUSA event span')
     calendar[key]=dict(days=days,minutes=minutes,field=field,pool_games=9,pools=pools,single_elimination_start=9)
     initial=[[0xFFFFFFFF]*2 for _ in range(count)];routes=[[[0xFFFFFFFF,2] for _ in range(2)] for _ in range(count)]
     for i,g in enumerate(graph):
      for side,t in enumerate(g['participants']):
       number=int(t[1:])-1
       if t[0]=='s':initial[i][side]=number
       elif t[0]=='w':routes[number][0]=[i,side]
     profiles.append([ids['engine_key'],ids['regular_slot'],ids['tournament_slot'],29221+(start-date(2028,1,1)).days,field,count,count+3,initial,routes,days,minutes,[[v-1 for v in group] for group in pools]])
    def cpp(value):return '{'+','.join(cpp(v) if isinstance(v,list) else str(v)+'u' for v in value)+'}'
    config=output/'cusa-postseason-config.h';config.write_text('#pragma once\nnamespace mvp::cusa_postseason { struct Profile {uint32_t key,regular,slot,start,field,games,capacity,initial[12][2],routes[12][2][2],days[12],minutes[12],pools[3][3];}; inline constexpr Profile kProfiles[]='+cpp(profiles)+'; }\n')
    return [p[0] for p in profiles]

def maac_postseason(rules, identities, windows, output):
    profiles=[]
    calendar={};authored_graphs={}
    keys=('ncaa-conf:871',)
    for key in keys:
     r=rules['conferences'][key]['tournament'];ids=identities['conferences'][key];w=windows[key];start=date.fromisoformat(w['event_start']);field=r['field_size'];stages=r['stages'];routing=stages[0]['routing']
     if field!=6 or len(stages)!=1 or stages[0]['kind']!='double_elimination' or r['qualification']['mode']!='overall_standings' or r['eligibility']['conference_ineligible_team_ids'] or not r['reseed']:raise ValueError('Authored MAAC shape differs')
     if stages[0]['opening_seed_pairs']!=[[3,6],[4,5]] or stages[0]['first_round_bye_seeds']!=[1,2] or routing['template']!='reseeded_six_double_elimination' or routing['reseed']!='within_each_loss_bucket_highest_vs_lowest_after_each_round' or routing['bye_priority']!='fewest_prior_byes_then_highest_original_seed' or routing['unequal_bucket_pairing']!='play_lower_loss_bucket_until_two_finalists_then_final_reset':raise ValueError('MAAC reseeding authoring differs')
     count=11;span=(date.fromisoformat(w['event_end'])-start).days
     days=[0,0,0,0,1,1,1,2,2,3,3];minutes=[540,720,900,1080,540,780,1020,660,900,660,900]
     if span!=3:raise ValueError('Unreviewed MAAC event span')
     calendar[key]=dict(days=days,minutes=minutes,field=field,round_widths=[2,2,1,2,1,1,1,1],byes='fewest prior byes then highest original seed')
     initial=[[2,5],[3,4]]+[[0xFFFFFFFF]*2 for _ in range(9)]
     profiles.append([ids['engine_key'],ids['regular_slot'],ids['tournament_slot'],29221+(start-date(2028,1,1)).days,field,count,count+3,initial,days,minutes])
    def cpp(value):return '{'+','.join(cpp(v) if isinstance(v,list) else str(v)+'u' for v in value)+'}'
    config=output/'maac-postseason-config.h';config.write_text('#pragma once\nnamespace mvp::maac_postseason { struct Profile {uint32_t key,regular,slot,start,field,games,capacity,initial[11][2],days[11],minutes[11];}; inline constexpr Profile kProfiles[]='+cpp(profiles)+'; }\n')
    return [p[0] for p in profiles]

def mac_postseason(rules, identities, windows, output):
    profiles=[]
    calendar={};authored_graphs={}
    keys=('ncaa-conf:875',)
    for key in keys:
     r=rules['conferences'][key]['tournament'];ids=identities['conferences'][key];w=windows[key];start=date.fromisoformat(w['event_start']);field=r['field_size'];stages=r['stages'];routing=stages[0]['routing']
     if field!=6 or len(stages)!=1 or stages[0]['kind']!='double_elimination' or r['qualification']['mode']!='overall_standings' or r['eligibility']['conference_ineligible_team_ids'] or r['reseed']:raise ValueError('Authored MAC shape differs')
     if stages[0]['opening_seed_pairs']!=[[3,6],[4,5]] or stages[0]['first_round_bye_seeds']!=[1,2] or routing['template']!='mac_2026_dynamic_six' or routing['first_winner_round']!='seed_1_vs_lower_ranked_opening_winner_seed_2_vs_other' or routing['loser_rounds']!=['L1_vs_L2','W3_vs_lower_ranked_L4_L5_then_winner_vs_other','if_seeds_1_and_2_both_lose_play_each_other_then_winner_vs_W3','prior_bye_exception_requires_that_L4_or_L5_team_in_game_6']:raise ValueError('MAC dynamic authoring differs')
     count=11;span=(date.fromisoformat(w['event_end'])-start).days
     days=[0,0,1,1,1,2,2,2,3,3,4];minutes=[750,1080,600,840,1080,600,840,1080,750,1080,720]
     if span!=4:raise ValueError('Unreviewed MAC event span')
     calendar[key]=dict(days=days,minutes=minutes,field=field,game6='both top seeds lose, otherwise prior bye, otherwise lower original seed')
     initial=[[2,5],[3,4]]+[[0xFFFFFFFF]*2 for _ in range(9)]
     profiles.append([ids['engine_key'],ids['regular_slot'],ids['tournament_slot'],29221+(start-date(2028,1,1)).days,field,count,count+3,initial,days,minutes])
    def cpp(value):return '{'+','.join(cpp(v) if isinstance(v,list) else str(v)+'u' for v in value)+'}'
    config=output/'mac-postseason-config.h';config.write_text('#pragma once\nnamespace mvp::mac_postseason { struct Profile {uint32_t key,regular,slot,start,field,games,capacity,initial[11][2],days[11],minutes[11];}; inline constexpr Profile kProfiles[]='+cpp(profiles)+'; }\n')
    return [p[0] for p in profiles]

def division_postseason(rules, identities, windows, output):
    profiles=[]
    calendar={};authored_graphs={}
    keys=('ncaa-conf:837','ncaa-conf:920')
    for key in keys:
     r=rules['conferences'][key]['tournament'];ids=identities['conferences'][key];w=windows[key];start=date.fromisoformat(w['event_start']);field=r['field_size'];stages=r['stages']
     if field!=6 or r['qualification']['mode']!='division_places_then_rpi' or r['eligibility']['conference_ineligible_team_ids']:raise ValueError('Authored divided qualification differs')
     playins=0 if key=='ncaa-conf:837' else 2;de_field=6 if not playins else 4
     if not playins:
      if len(stages)!=1 or stages[0]['routing']['template']!='double_elimination_6':raise ValueError('CAA graph differs')
      graph=rules['bracket_templates']['double_elimination_6']['games']
     else:
      if len(stages)!=2 or stages[0]['opening_seed_pairs']!=[[3,6],[4,5]] or stages[0]['routing']['pairing_adjustment']!='swap_5_and_6_if_needed_to_avoid_same_authored_division':raise ValueError('ASUN graph/pair adjustment differs')
      graph=[{'game':1,'participants':['s3','s6']},{'game':2,'participants':['s4','s5']}]
      refs=stages[1]['routing']['slot_assignment']
      for node in rules['bracket_templates']['double_elimination_4']['games']:
       tokens=[]
       for token in node['participants']:
        if token[0]=='s':
         ref=refs[token[1:]].split(':');tokens.append('s'+ref[1] if ref[0]=='seed' else 'w'+ref[3])
        else:tokens.append(token[0]+str(int(token[1:])+playins))
       graph.append({**node,'game':node['game']+playins,'participants':tokens})
     count=len(graph);authored_graphs[key]=graph;span=(date.fromisoformat(w['event_end'])-start).days
     if span!=3:raise ValueError('Unreviewed divided event window')
     if not playins:days,minutes=[0,0,0,1,1,1,2,2,2,3,3],[600,840,1080]*3+[660,900]
     else:days,minutes=[0,0,1,1,2,2,2,3,3],[600,840,600,840,540,780,1020,660,900]
     if len(days)!=count or days[-1]!=span:raise ValueError('Hybrid calendar count/span')
     calendar[key]=dict(days=days,minutes=minutes,field=field,playins=playins,de_field=de_field)
     initial=[[0xFFFFFFFF]*2 for _ in range(15)];routes=[[[0xFFFFFFFF,2] for _ in range(2)] for _ in range(15)]
     for i,g in enumerate(graph):
      if g['game']!=i+1 or (g.get('condition') is not None and (i!=count-1 or g['condition']!='play_only_if_undefeated_finalist_lost_previous_final')):raise ValueError('Unsupported graph condition')
      for side,token in enumerate(g['participants']):
       number=int(token[1:])-1
       if token[0]=='s':
        if not 0<=number<field:raise ValueError('Seed bounds')
        initial[i][side]=number
       elif token[0] in 'wl':
        if not 0<=number<i or routes[number][token[0]=='l']!=[0xFFFFFFFF,2]:raise ValueError('Graph forward reference/fanout')
        routes[number][token[0]=='l']=[i,side]
       else:raise ValueError('Unknown authored token')
     profiles.append([ids['engine_key'],ids['regular_slot'],ids['tournament_slot'],29221+(start-date(2028,1,1)).days,field,count,count+3,playins,de_field,initial,routes,days+[0]*(15-count),minutes+[0]*(15-count)])
    def cpp(value):return '{'+','.join(cpp(v) if isinstance(v,list) else str(v)+'u' for v in value)+'}'
    config=output/'division-postseason-config.h';config.write_text('#pragma once\nnamespace mvp::division_postseason { struct Profile {uint32_t key,regular,slot,start,field,games,capacity,playins,de_field,initial[15][2],routes[15][2][2],days[15],minutes[15];}; inline constexpr Profile kProfiles[]='+cpp(profiles)+'; }\n')
    return [p[0] for p in profiles]

GENERATORS=[four_team_de,six_team_de,large_de,single_elimination,hybrid_postseason,twin_brackets,southland_postseason,staggered_postseason,bigten_postseason,cusa_postseason,maac_postseason,mac_postseason,division_postseason]

def main():
    league,fixtures,output=map(Path,sys.argv[1:]);output.mkdir(parents=True,exist_ok=True)
    rules=json.loads((league/"competition-rules.json").read_text(encoding="utf-8-sig"))
    identities=json.loads((league/"identities.json").read_text(encoding="utf-8-sig"))
    windows=json.loads(fixtures.read_text(encoding="utf-8-sig"))["windows"]
    seen=[]
    for generator in GENERATORS:
        keys=generator(rules,identities,windows,output);seen+=keys
        print(generator.__name__,keys)
    if len(set(seen))!=len(seen):raise ValueError("Overlapping tournament profiles")
    print("Compiled",len(seen),"conference graphs; Patriot series is a separate native owner")

if __name__=="__main__":main()
