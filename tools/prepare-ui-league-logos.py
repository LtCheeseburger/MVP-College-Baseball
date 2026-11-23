"""Prepare UI logo recipes from the canonical built league and native-logo masters."""
import argparse,csv,hashlib,json,shutil
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('ui',type=Path);p.add_argument('league',type=Path);p.add_argument('logos',type=Path);p.add_argument('backup',type=Path);a=p.parse_args()
ui=a.ui;table=ui/'overrides/data/overhaul/startup/base-logos/teams.tsv'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def save(p,d):p.write_text(json.dumps(d,indent=2)+'\n',encoding='utf-8')
def backup(p):
 dst=a.backup/p.relative_to(ui)
 if p.exists() and not dst.exists():dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,dst)
rows=list(csv.DictReader((a.league/'build/mvp27-league/files/data/league/school-logos.csv').open(encoding='latin1',newline='')))
if len(rows)!=309 or len({r['Logo ID'] for r in rows})!=309:raise ValueError('Expected 309 canonical school assets')
identity=read(a.league/'sources/league/identities.json')['teams'];active={int(t['asset_id']):t for t in identity.values() if not t.get('retired')}
recipes=read(ui/'recipes.json');inventory=read(ui/'source-inventory.json');mapping=dict(line.split('\t',1) for line in table.read_text().splitlines() if '\t' in line)
report=[]
for row in rows:
 asset=int(row['Logo ID']);tag=f'{asset:03}';target=f'files/data/overhaul/startup/base-logos/{tag}.png';source=f'sources/runtime/data/overhaul/startup/base-logos/{tag}.png';dest=ui/source
 # Preserve authored stock-school masters. New-school IDs replace obsolete custom-school images.
 if active[asset]['engine_index']>128 or not dest.exists():
  master=a.logos/f'sources/logos/{tag}.png'
  if not master.exists():raise ValueError(f'Missing school master: {asset}')
  backup(dest);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(master,dest)
 recipes['recipes']=[r for r in recipes['recipes'] if r['target']!=target]
 recipes['recipes'].append(dict(op='image',source=source,target=target,width=512,height=512,fit='contain',format='png',mipLevels=1,padding=0))
 for name in {row['School'].upper(),row['School'][:23].upper()}:
  if name in mapping and mapping[name]!=tag:raise ValueError(f'Ambiguous UI alias: {name}')
  mapping[name]=tag
 sha=hashlib.sha256(dest.read_bytes()).hexdigest()
 inventory['files']=[f for f in inventory['files'] if f['source']!=source]
 inventory['files'].append(dict(source=source,sha256=sha,bytes=dest.stat().st_size,original=f'canonical school asset {tag}'))
 report.append(dict(school=row['School'],assetId=asset,source=source,sha256=sha))
for name in ['project.json','recipes.json','source-inventory.json']:backup(ui/name)
backup(table);table.write_text(''.join(f'{k}\t{v}\n' for k,v in sorted(mapping.items())),encoding='utf-8')
for f in inventory['files']:
 if f['source']==table.relative_to(ui).as_posix():f.update(sha256=hashlib.sha256(table.read_bytes()).hexdigest(),bytes=table.stat().st_size)
metadata=read(ui/'project.json');metadata['mod']['version']='1.0.1';metadata['mod']['description']='Optional MVP 27 interface with canonical logo coverage for all 309 league schools.'
save(ui/'project.json',metadata);save(ui/'recipes.json',recipes);save(ui/'source-inventory.json',inventory)
save(ui/'league-logo-coverage.json',dict(format=1,schools=report,aliases=len(mapping)))
print(f'Prepared 309 canonical UI logos / {len(mapping)} school aliases; stock masters preserved')
