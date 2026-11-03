#requires -Version 7.0
param([Parameter(Mandatory)][string]$Collection,[Parameter(Mandatory)][string]$Game,[Parameter(Mandatory)][string]$Project)
$ErrorActionPreference='Stop'
$Collection=(Resolve-Path -LiteralPath $Collection).Path;$Game=(Resolve-Path -LiteralPath $Game).Path;$Project=[IO.Path]::GetFullPath($Project)
if(Test-Path (Join-Path $Project 'project.json')){throw 'Existing project; edit masters/recipes instead of overwriting'}
New-Item -ItemType Directory -Force $Project|Out-Null
$mapping=Get-Content (Join-Path $Collection 'game-install.json') -Raw|ConvertFrom-Json
$recipes=[Collections.Generic.List[object]]::new();$inventory=[Collections.Generic.List[object]]::new()
foreach($team in $mapping.installed){
    $id=$team.id;$source='sources/logos/'+$id+'.png';$input=Join-Path $Collection $team.source
    $target=Join-Path $Project $source;New-Item -ItemType Directory -Force (Split-Path $target)|Out-Null;Copy-Item -LiteralPath $input -Destination $target
    $template='overrides/templates/l'+$id+'.xsh'
    $native=Join-Path $Game ('data/frontend/logos_big/l'+$id+'.xsh')
    if(!(Test-Path (Join-Path $native 'xsh.meta'))){throw "Loose logo template missing: $native"}
    New-Item -ItemType Directory -Force (Split-Path (Join-Path $Project $template))|Out-Null
    Copy-Item -LiteralPath $native -Destination (Join-Path $Project $template) -Recurse -Force
    $recipes.Add(@{op='xsh-logo';source=$source;template=$template;target='files/data/frontend/logos_big/l'+$id+'.xsh';mainSize=256;wideWidth=128;wideHeight=64;mainFormat='DXT3';wideFormat='DXT3';mipLevels='full';indexedFormat='PAL8';paletteColors=256})
    $inventory.Add(@{team=$team.team;id=$id;school=$team.school;source=$source;sha256=(Get-FileHash -LiteralPath $input).Hash.ToLowerInvariant();template=$template})
}
Copy-Item -LiteralPath (Join-Path $Project 'sources/logos/001.png') -Destination (Join-Path $Project 'sources/icon.png')
$recipes.Add(@{op='image';source='sources/icon.png';target='icon.png';width=256;height=256;fit='contain';format='png';mipLevels=1;padding=0})
@'
# MVP 27 Native Logos

Replaces native game logo files for 127 stock schools. Northern Iowa (103) retains its original logo because no replacement artwork was supplied. The stock team database and saves are unchanged.

Main i logos: 256x256 DXT3 with full mip chains. Rectangular j logos: 128x64 DXT3 with full mip chains and aspect-preserving padding. Indexed a logos retain their original dimensions/storage with a rebuilt 256-color palette. Other slots are preserved.

Requires port 0.2.3-beta or newer for the HD logo cache and heap budget. Enable MVP 27 Native Logos in the launcher, or use --mod <archive or folder>. --no-mods disables it. The MVP 27 UI mod uses its own smaller preview PNGs and is optional. There is no GPU texture-hash swap. No league dependency is declared until a future 309-team data/art release actually exists.
'@|Set-Content (Join-Path $Project 'overrides/readme.md')
$recipes.Add(@{op='copy';source='overrides/readme.md';target='readme.md'})
@{format=1;mod=@{format=1;id='mvp27-logos';name='MVP 27 Native Logos';version='1.0.0';author='MVP 27';description='Native embroidered logos for 127 stock schools; Northern Iowa retained.';game=@{xbeSha1='2f57040bab2c8b2eb33ebc7aa93209ad30dd103f'};layout=1;affectsSaves=$false};gameRelease='MVP 06 NCAA Baseball (Xbox), stock 128 schools';build=@{archive='stored-zip64';folder='build/mvp27-logos';defaultEnabled=$false}}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Project 'project.json')
@{format=1;recipes=$recipes.ToArray()}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Project 'recipes.json')
@{format=1;teams=$inventory.ToArray();retained=$mapping.retained;budget='256px main/128x64 wide with full mips; PAL8 unchanged. Port 0.2.3-beta grows the twenty native logo-cache slots to the largest XSH and uses the 46 MB heap for HD assets.'}|ConvertTo-Json -Depth 7|Set-Content (Join-Path $Project 'source-inventory.json')
"/build/`n/tools/MvpProject/bin/`n/tools/MvpProject/obj/`n/external/MVP-Workshop/src/MVPWorkshop.Core/bin/`n/external/MVP-Workshop/src/MVPWorkshop.Core/obj/"|Set-Content (Join-Path $Project '.gitignore')
"Imported $($inventory.Count) native team recipes"
