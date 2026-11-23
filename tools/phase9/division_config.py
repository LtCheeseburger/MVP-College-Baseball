"""Compile locked membership and authored host rules for qualification probes."""
import json
from datetime import date, timedelta

def configuration(root, schedule):
    league=json.loads((root/'league.json').read_text(encoding="utf-8-sig"))
    identities=json.loads((root/'identities.json').read_text(encoding="utf-8-sig"))
    rules=json.loads((root/'competition-rules.json').read_text(encoding="utf-8-sig"))
    fixture=json.loads((schedule).read_text(encoding="utf-8-sig"))
    teams=[];selections=[];venues=[];divisions={}
    for key in ('ncaa-conf:837','ncaa-conf:920'):
        record=rules['conferences'][key];identity=identities['conferences'][key]
        for i,group in enumerate(record['division']['groups']):
            for team in group['team_ids']:divisions[team]=i
        cutoff=date.fromisoformat(fixture['windows'][key]['last'])+timedelta(days=1)
        selections.append(dict(key=identity['engine_key'],regular=identity['regular_slot'],cutoff=29221+(cutoff-date(2028,1,1)).days,season=2027,canonical=key))
    for team in league['teams']:
        canonical=team['id'];conf=team['conference_id'];record=rules['conferences'][conf]
        teams.append(dict(id=identities['teams'][canonical]['engine_index'],key=identities['conferences'][conf]['engine_key'],division=divisions.get(canonical,0),canonical=canonical,eligible=canonical not in record['tournament']['eligibility']['conference_ineligible_team_ids']))
    # Existing authored named campus venues. This binds simulated host identity;
    # it does not claim that the borrowed 3D park has been replaced.
    campus={'Charles Schwab Field Omaha':'ncaa:169','Brooks Field':'ncaa:460','Bearcats Baseball Campus':'ncaa:62','Johnny Reagan Field at Taylor Family Park':'ncaa:454','Cicerone Field at Anteater Ballpark':'ncaa:109'}
    for key,record in rules['conferences'].items():
        event=record['tournament']['event_plan'];venue=event['venue'];policy=event['host_policy'];host=0;mode=0
        if policy=='fixed_site':
            canonical=venue.get('team_id') or campus.get(venue.get('name'))
            if canonical:host=identities['teams'][canonical]['engine_index']
        elif policy in ('higher_seed_each_game','seeds_1_and_2_host_semifinals_higher_remaining_seed_hosts_final'):mode=1
        elif policy=='highest_regular_season_seed':mode=2
        elif policy=='regular_seeds_1_and_2_brackets_then_higher_remaining_final':mode=3
        else:raise ValueError('Unimplemented authored host policy '+policy)
        identity=identities['conferences'][key];venues.append(dict(key=identity['engine_key'],slot=identity['tournament_slot'],mode=mode,host=host,origin=policy))
    return dict(teams=sorted(teams,key=lambda t:t['id']),selections=selections,venues=venues)

def cpp_config(config):
    def array(rows,keys):
        return '{'+','.join('{'+','.join(json.dumps(r[k]) if isinstance(r[k],str) else str(int(r[k])) for k in keys)+'}' for r in rows)+'}'
    return '#pragma once\nnamespace mvp::division_qualification {\n'+\
        'struct TeamConfig {uint32_t id,key,division;const char* canonical;bool eligible;}; inline constexpr TeamConfig kTeams[]='+array(config['teams'],('id','key','division','canonical','eligible'))+';\n'+\
        'struct Selection {uint32_t key,regular;int32_t cutoff;uint32_t season;const char* canonical;}; inline constexpr Selection kSelections[]='+array(config['selections'],('key','regular','cutoff','season','canonical'))+';\n'+\
        'struct Venue {uint32_t key,slot,mode,host;}; inline constexpr Venue kVenues[]='+array(config['venues'],('key','slot','mode','host'))+';\n}\n'

if __name__=="__main__":
 import sys
 from pathlib import Path
 root,fixtures,output=map(Path,sys.argv[1:])
 output.write_text(cpp_config(configuration(root,fixtures)))
