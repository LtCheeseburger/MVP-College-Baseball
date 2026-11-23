"""Compile the locked league into immutable inputs for native annual scheduling.

Canonical membership and the validated2027 series history are the only identity
and rotation authorities. This compiler does not change research or game files.
"""
import hashlib
import json
from collections import Counter
from datetime import date
from pathlib import Path

from .mvp_schedule_calendar import REGION


def configuration(root, schedule):
    data=root/'league-data/d1-2027'
    paths=[data/name for name in ('league.json','identities.json','competition-rules.json','schedule-policy.json','bigwest-2027-series-matrix.json','dynasty-policy.json')]
    league,ids,rules,policy,bigwest,dynasty=[json.loads(path.read_text()) for path in paths]
    manifest_path=schedule/'manifest.json';manifest=json.loads(manifest_path.read_text())
    sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    assert manifest['result']=='passed' and manifest['season']==2027
    for name,digest in manifest['inputs_sha256'].items():assert sha(root/name)==digest,name
    for name,digest in manifest['output_sha256'].items():assert sha(schedule/name)==digest,name
    conf_path=root/manifest['conference_fixture']/'expected-conferences.json'
    confs=json.loads(conf_path.read_text());graphs=json.loads((schedule/'conference-graphs.json').read_text())
    assert dynasty['postseason_eligibility']=='all_active_programs_eligible'
    teams=[]
    for team in league['teams']:
        cid=team['conference_id']
        teams.append(dict(id=team['id'],engine=ids['teams'][team['id']]['engine_index'],state=team['state'],region=REGION[team['state']],
            key=ids['conferences'][cid]['engine_key'],budget=policy['conference_budget_overrides'].get(cid,{}).get('games',policy['default_regular_season_games'])))
    assert {t['engine'] for t in teams}==set(range(1,311))-{102}
    models={'single_round_robin_series':'Single','double_round_robin_series':'Double','protected_partial_round_robin_series':'Protected',
        'mixed_length_round_robin':'MixedLength','round_robin_plus_repeat_series':'Repeat','divisional_double_round_robin_plus_cross_series':'DividedDouble','asymmetric_divisional_series':'Asymmetric'}
    days=['Monday','Tuesday','Wednesday','Thursday','Friday','Saturday','Sunday'];conferences=[]
    stamp=lambda value:29221+(date.fromisoformat(value)-date(2028,1,1)).days
    for conf in confs:
        cid=conf['conference_id'];profile=rules['conferences'][cid];regular=profile['regular_season'];event=profile['tournament']['event_plan'];factor=0
        if regular['model']=='partial_round_robin_series':
            rotation=regular['rotation_policy'];model={'rotating_circle_method_perfect_matching_factors':'PartialCircle','rotating_circulant_distance_factors':'PartialDistance'}[rotation['selection']];factor=rotation['selection_factor_count']
        else:model=models[regular['model']]
        groups=profile['division']['groups']
        if model=='Asymmetric':groups=sorted(groups,key=lambda g:g['id']!='north')
        divisions=[sorted(g['team_ids'],key=lambda t:ids['teams'][t]['engine_index']) for g in groups]
        published=event.get('published_target_dates')
        if cid==bigwest['conference_id']:published=bigwest['published_tournament_dates']
        history=Counter(tuple(sorted((s['home'],s['away']))) for s in graphs[cid]['series'])
        identity=ids['conferences'][cid]
        conferences.append(dict(key=conf['engine_key'],canonical=cid,model=model,factor=factor,teams=graphs[cid]['teams'],divisions=divisions,
            protected=sorted({tuple(sorted((t,u))) for t,others in regular.get('protected_opponents',{}).items() for u in others}),
            history=[dict(pair=list(pair),series=count) for pair,count in sorted(history.items())],
            regular=identity['regular_slot'],tournament=identity['tournament_slot'],abbreviation=conf['row']['conf_abbr'],stem=conf['row']['conf_schedulename'],
            event=[conf['engine_key'],event['maximum_span_days'],days.index(event['start_weekday']),date.fromisoformat(published['start']).year if published else 0,
                stamp(published['start']) if published else 0,stamp(published['end']) if published else 0,int(cid=='ncaa-conf:865')],
            saturday_only=regular['calendar_policy']['series_window']=='Saturday-Sunday'))
    assert len(conferences)==30 and len({c['key'] for c in conferences})==30
    paths += [manifest_path,conf_path,schedule/'conference-graphs.json',Path(__file__).resolve()]
    return dict(teams=teams,conferences=conferences,flow_order=[ids['conferences'][cid]['engine_key'] for cid in rules['conferences']],
        first_dates={year:stamp(value) for year,value in policy['first_permissible_date'].items() if year.isdigit()},
        max_pair_games=policy['nonconference']['maximum_games_against_same_opponent'],inputs_sha256={str(path.relative_to(root)):sha(path) for path in paths})


def cpp_config(config):
    def values(rows):return '{'+','.join(json.dumps(value) if isinstance(value,str) else str(int(value)) for value in rows)+'}'
    lines=['#pragma once\n#include "annual-schedule-fixtures.h"\nnamespace mvp::annual_schedule::authored {\n',
        'struct ConferenceData {ConferenceRule rule;EventRule event;unsigned regular,tournament;std::string abbreviation,stem;bool saturday;History history;};\n',
        'inline std::vector<TeamInfo> Teams(){return {'+','.join(values([t['id'],t['state'],t['key'],t['region'],t['budget']]) for t in config['teams'])+'};}\n',
        'inline const std::map<Team,unsigned> kEngine='+ '{'+','.join(values([t['id'],t['engine']]) for t in config['teams'])+'};\n',
        'inline const std::vector<unsigned> kFlowOrder='+values(config['flow_order'])+';\n',
        'inline std::vector<ConferenceData> Conferences(){std::vector<ConferenceData> out;\n']
    for c in config['conferences']:
        rule='{'+f'{c["key"]},SeriesModel::{c["model"]},{c["factor"]},'+values(c['teams'])+',{'+','.join(values(g) for g in c['divisions'])+'},{'+','.join(values(pair) for pair in c['protected'])+'}}'
        history='{'+','.join('{'+values(row['pair'])+','+str(row['series'])+'}' for row in c['history'])+'}'
        lines.append('out.push_back({'+rule+','+values(c['event'])+f',{c["regular"]},{c["tournament"]},'+json.dumps(c['abbreviation'])+','+json.dumps(c['stem'])+','+str(int(c['saturday_only']))+','+history+'});\n')
    lines.append('return out;}\ninline int32_t FirstDate(unsigned year){')
    for year,day in config['first_dates'].items():lines.append(f'if(year=={year})return {day};')
    lines.append('return 0;}\n')
    lines.append('''struct Season {unsigned year,attempt;std::vector<ConferenceSeason> conferences;std::vector<Fixture> fixtures;std::vector<std::string> rejected_attempts;};
inline Season Generate(unsigned year){
 GraphRequire(year>=2028&&year<=9998,"Annual generator year outside native calendar bounds");
 auto data=Conferences();std::vector<ConferenceSeason> selected;
 // Reconstruct deterministic series history from the locked2027 start. No
 // unsaved host cache or additional save sidecar is required after restart.
 for(unsigned season=2028;season<=year;++season){
  for(auto& conf:data){auto graph=ConferenceSeries(conf.rule,season,conf.history);
   for(const auto& s:graph.series)++conf.history[Pair(s.home,s.away)];
   if(season==year)selected.push_back({conf.rule.key,std::move(graph),SeasonWindow(season,conf.event,FirstDate(season)),conf.saturday});
  }
 }
 Season result{year,0,std::move(selected),{}, {}};
 for(unsigned attempt=0;attempt<16;++attempt){try{
''')
    lines.append(f'  FixtureGenerator generator(year,attempt,Teams(),result.conferences,{config["max_pair_games"]},kFlowOrder);\n')
    lines.append('''  result.fixtures=generator.Generate();result.attempt=attempt;return result;
 }catch(const std::exception& e){result.rejected_attempts.push_back(e.what());}}
 std::string why="Annual generator exhausted16 explicit attempts";for(const auto& error:result.rejected_attempts)why+="; "+error;throw std::runtime_error(why);
}
}
''')
    return ''.join(lines)
