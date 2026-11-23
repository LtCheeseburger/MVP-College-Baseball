"""Author the kit's exact-budget annual fixtures against the current league mod.

Pure authoring only: never changes the original game or imports the donor save layout.
Run as python -m tools.phase9.generate <league sources> <league.mvpmod> <output>.
"""
import csv
import hashlib
import io
import json
import sys
import zipfile
from collections import Counter
from datetime import date
from pathlib import Path
from .mvp_schedule_graphs import conference_series, Unsatisfied
from .mvp_schedule_calendar import generate_fixtures, REGION
from .mvp_schedule_validation import validate_fixtures
from .mvp_annual_config import cpp_config

def generate(league_dir, archive, output):
    def read(name): return json.loads((league_dir/name).read_text(encoding="utf-8-sig"))
    league, identities, rules, policy, bigwest = [read(n) for n in ["league.json", "identities.json", "competition-rules.json", "schedule-policy.json", "bigwest-2027-series-matrix.json"]]
    with zipfile.ZipFile(archive) as z:
        rows = list(csv.reader(io.StringIO(z.read("files/data/database/database_big/conf.dat").decode("cp1252"))))
    native = {int(r[0],16): {c.partition(" ")[0]:c.partition(" ")[2] for c in r[1:] if c!=";"} for r in rows[1:]}
    confs = []
    for conference in league["conferences"]:
        cid = conference["id"]; identity = identities["conferences"][cid]; key=identity["engine_key"]
        confs.append(dict(conference_id=cid,engine_key=key,members=sorted(identities["teams"][t["id"]]["engine_index"] for t in league["teams"] if t["conference_id"]==cid),
            row={"conf_abbr":native[key]["1"],"conf_schedulename":native[key]["3"]}))
    confs.sort(key=lambda c:c["engine_key"])
    graphs={c["conference_id"]:conference_series(c,rules["conferences"][c["conference_id"]],identities,2027,bigwest) for c in confs}
    rejected=[]
    for attempt in range(16):
        try:
            schedule=generate_fixtures(2027,confs,rules["conferences"],graphs,league,policy,bigwest,attempt)
            validation=validate_fixtures(schedule,confs,graphs,rules["conferences"],policy)
            break
        except Unsatisfied as error:rejected.append(str(error))
    else: raise ValueError("Annual schedule failed: "+"; ".join(rejected))
    if len(schedule["games"])!=8588:raise ValueError("Wrong annual fixture count")
    output.mkdir(parents=True,exist_ok=True)
    models={"single_round_robin_series":"Single","double_round_robin_series":"Double","protected_partial_round_robin_series":"Protected","mixed_length_round_robin":"MixedLength","round_robin_plus_repeat_series":"Repeat","divisional_double_round_robin_plus_cross_series":"DividedDouble","asymmetric_divisional_series":"Asymmetric"}
    stamp=lambda value:29221+(date.fromisoformat(value)-date(2028,1,1)).days
    weekdays=["Monday","Tuesday","Wednesday","Thursday","Friday","Saturday","Sunday"]
    config=dict(teams=[],conferences=[],flow_order=[identities["conferences"][cid]["engine_key"] for cid in rules["conferences"]],
        first_dates={y:stamp(v) for y,v in policy["first_permissible_date"].items() if y.isdigit()},max_pair_games=policy["nonconference"]["maximum_games_against_same_opponent"])
    for t in league["teams"]:
        cid=t["conference_id"]
        config["teams"].append(dict(id=t["id"],engine=identities["teams"][t["id"]]["engine_index"],state=t["state"],region=REGION[t["state"]],key=identities["conferences"][cid]["engine_key"],budget=policy["conference_budget_overrides"].get(cid,{}).get("games",56)))
    for conf in confs:
        cid=conf["conference_id"]; p=rules["conferences"][cid]; regular=p["regular_season"];event=p["tournament"]["event_plan"];factor=0
        if regular["model"]=="partial_round_robin_series":
            rotation=regular["rotation_policy"];model={"rotating_circle_method_perfect_matching_factors":"PartialCircle","rotating_circulant_distance_factors":"PartialDistance"}[rotation["selection"]];factor=rotation["selection_factor_count"]
        else:model=models[regular["model"]]
        groups=p["division"]["groups"]
        if model=="Asymmetric":groups=sorted(groups,key=lambda g:g["id"]!="north")
        published=event.get("published_target_dates")
        if cid==bigwest["conference_id"]:published=bigwest["published_tournament_dates"]
        history=Counter(tuple(sorted((s["home"],s["away"])))for s in graphs[cid]["series"])
        identity=identities["conferences"][cid]
        config["conferences"].append(dict(key=conf["engine_key"],model=model,factor=factor,teams=graphs[cid]["teams"],
            divisions=[sorted(g["team_ids"],key=lambda t:identities["teams"][t]["engine_index"])for g in groups],
            protected=sorted({tuple(sorted((t,u)))for t,others in regular.get("protected_opponents",{}).items()for u in others}),
            history=[dict(pair=list(pair),series=count)for pair,count in sorted(history.items())],regular=identity["regular_slot"],tournament=identity["tournament_slot"],
            abbreviation=conf["row"]["conf_abbr"],stem=conf["row"]["conf_schedulename"],
            event=[conf["engine_key"],event["maximum_span_days"],weekdays.index(event["start_weekday"]),date.fromisoformat(published["start"]).year if published else 0,stamp(published["start"])if published else 0,stamp(published["end"])if published else 0,int(cid=="ncaa-conf:865")],
            saturday_only=regular["calendar_policy"]["series_window"]=="Saturday-Sunday"))
    def write(name,value):(output/name).write_text(json.dumps(value,indent=2)+"\n",encoding="utf-8")
    write("fixtures-2027.json",schedule);write("conference-graphs.json",graphs);write("annual-config.json",config);write("validation.json",dict(result="passed",games=8588,validation=validation,rejectedAttempts=rejected))
    (output/"annual-league-config.h").write_text(cpp_config(config),encoding="utf-8")
    print("PASS 2027 formal annual fixture policy: 8,588 games, exact budgets, conference graphs, venue balance and calendar validation")

if __name__=="__main__":generate(*map(Path,sys.argv[1:]))
