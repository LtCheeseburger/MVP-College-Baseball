#requires -Version 7.0
param([Parameter(Mandatory)][string]$League,[Parameter(Mandatory)][string]$Collection,[Parameter(Mandatory)][string]$Project,[Parameter(Mandatory)][string]$Mapping,[Parameter(Mandatory)][string]$Backup)
$ErrorActionPreference='Stop'
$League=(Resolve-Path $League).Path;$Collection=(Resolve-Path $Collection).Path;$Project=(Resolve-Path $Project).Path
$identity=Get-Content (Join-Path $League 'identities.json') -Raw|ConvertFrom-Json
$art=Get-Content $Mapping -Raw|ConvertFrom-Json
if(@($art).Count -ne 309 -or @($art.id|Sort-Object -Unique).Count -ne 309){throw 'Need one reviewed art mapping per active school'}
New-Item -ItemType Directory -Force $Backup|Out-Null
foreach($name in @('project.json','recipes.json','source-inventory.json')){if(!(Test-Path (Join-Path $Backup $name))){Copy-Item (Join-Path $Project $name) (Join-Path $Backup $name)}}
$recipes=[Collections.Generic.List[object]]::new();$inventory=[Collections.Generic.List[object]]::new()
foreach($entry in $art){
    $school=$identity.teams.PSObject.Properties[$entry.id].Value
    if(!$school -or $school.retired){throw "Unknown active identity $($entry.id)"}
    $asset=([int]$school.asset_id).ToString('000');$source='sources/logos/'+$asset+'.png';$file=Join-Path $Project $source
    if(!(Test-Path $file)){
        if($entry.art.Contains('..') -or [IO.Path]::IsPathRooted($entry.art)){throw 'Unsafe mapped art path'}
        Copy-Item (Join-Path $Collection $entry.art) $file
    }
    # Preserve existing edited masters. The mapping identifies new-school art;
    # existing project masters remain the authority for already-authored schools.
    $template='overrides/templates/l'+$asset+'.xsh'
    if(!(Test-Path (Join-Path $Project ($template+'/xsh.meta')))){$template='overrides/templates/l001.xsh'}
    $recipes.Add(@{op='xsh-logo';source=$source;template=$template;target='files/data/frontend/logos_big/l'+$asset+'.xsh';assetId=[int]$asset;allSlots=$true;mainSize=256;wideWidth=128;wideHeight=64;mainFormat='DXT3';wideFormat='DXT3';mipLevels='full';indexedFormat='PAL8';paletteColors=256})
    $inventory.Add(@{id=$entry.id;assetId=[int]$asset;school=$entry.school;source=$source;sha256=(Get-FileHash $file).Hash.ToLowerInvariant();template=$template})
}
$recipes.Add(@{op='image';source='sources/icon.png';target='icon.png';width=256;height=256;fit='contain';format='png';mipLevels=1;padding=0})
$recipes.Add(@{op='copy';source='overrides/readme.md';target='readme.md'})
$metadata=Get-Content (Join-Path $Project 'project.json') -Raw|ConvertFrom-Json -AsHashtable
$metadata.mod.version='2.0.0';$metadata.mod.description='Native embroidered logos for all 309 active league schools.'
$metadata.mod.requires=@(@{id='mvp27-league';version='>=0.1.0'});$metadata.gameRelease='MVP 27 D1 league (309 active schools)'
$metadata|ConvertTo-Json -Depth 10|Set-Content (Join-Path $Project 'project.json')
@{format=1;recipes=$recipes.ToArray()}|ConvertTo-Json -Depth 10|Set-Content (Join-Path $Project 'recipes.json')
@{format=1;teams=$inventory.ToArray();retired=@(@{id='legacy:102';assetId=103;reason='Retired identity remains reserved; no active school is renumbered'})}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $Project 'source-inventory.json')
@'
# MVP 27 Native Logos — 309 schools

Requires mvp27-league 0.1.0 or newer. Optional embroidered native art for all 309 active schools. Original-layout texture folders use school-specific internal tags, main 256x256 DXT3 mip chains, 128x64 wide logos and native PAL8 palettes. Every small logo slot is redrawn too. Retired Northern Iowa's reserved asset is unused by the active league. Existing edited masters were preserved during the upgrade.
'@|Set-Content (Join-Path $Project 'overrides/readme.md')
'Prepared 309 native school-logo recipes; build with the current shared MvpProject tool.'
