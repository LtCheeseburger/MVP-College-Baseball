param([Parameter(Mandatory)][string]$RuntimeMod,[Parameter(Mandatory)][string]$Project)
$ErrorActionPreference='Stop'
$RuntimeMod=(Resolve-Path -LiteralPath $RuntimeMod).Path
$Project=[IO.Path]::GetFullPath($Project)
if(Test-Path (Join-Path $Project 'project.json')){throw 'Project already exists; edit its sources and recipes instead of reimporting'}
New-Item -ItemType Directory -Force $Project|Out-Null
$recipes=[Collections.Generic.List[object]]::new();$inventory=[Collections.Generic.List[object]]::new()
$root=Join-Path $RuntimeMod 'files'
foreach($file in Get-ChildItem -LiteralPath $root -File -Recurse){
    $rel=[IO.Path]::GetRelativePath($root,$file.FullName).Replace('\','/')
    $source='sources/runtime/'+$rel
    if($file.Extension -ne '.png'){$source='overrides/'+$rel}
    $to=Join-Path $Project $source
    New-Item -ItemType Directory -Force (Split-Path $to)|Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $to
    $inventory.Add(@{source=$source;sha256=(Get-FileHash -LiteralPath $to).Hash.ToLowerInvariant();original=$rel;bytes=$file.Length})
    # GPU-swap variants and mappings have no role in the host UI mod.
    if($rel -match '/base-logos/.*-wide\.png$' -or $file.Name -eq 'replacements.tsv'){continue}
    $recipe=[ordered]@{op='copy';source=$source;target='files/'+$rel}
    if($file.Extension -eq '.png'){
        $header=[byte[]]::new(24);$stream=[IO.File]::OpenRead($to);try{$stream.ReadExactly($header)}finally{$stream.Dispose()}
        $w=[uint32]$header[16]*16777216+[uint32]$header[17]*65536+[uint32]$header[18]*256+$header[19]
        $h=[uint32]$header[20]*16777216+[uint32]$header[21]*65536+[uint32]$header[22]*256+$header[23]
        $scale=1.0
        if($rel -match '/base-logos/'){$scale=[math]::Min(1.0,512.0/[math]::Max($w,$h))}
        elseif($file.Name -match 'background|menu_bg|prbg|settings-bg|preview-bg'){$scale=[math]::Min(1.0,[math]::Min(2560.0/$w,1440.0/$h))}
        if($scale -lt 1){$recipe.op='image';$recipe.width=[int][math]::Max(1,[math]::Round($w*$scale));$recipe.height=[int][math]::Max(1,[math]::Round($h*$scale));$recipe.format='png';$recipe.mipLevels=1;$recipe.padding=0}
    }
    $recipes.Add($recipe)
}
foreach($notice in Get-ChildItem (Join-Path $PSScriptRoot 'MvpProject/licenses') -File){
    $source='overrides/licenses/'+$notice.Name
    $destination=Join-Path $Project $source
    New-Item -ItemType Directory -Force (Split-Path $destination)|Out-Null
    Copy-Item -LiteralPath $notice.FullName -Destination $destination
    $recipes.Add(@{op='copy';source=$source;target='files/data/overhaul/licenses/'+$notice.Name})
}
$marker=Join-Path $Project 'overrides/data/overhaul/overhaul.json'
@{format=1;overhaul='mvp27-ui';dataVersion='1.0.0'}|ConvertTo-Json|Set-Content $marker
$logo=Join-Path $Project 'sources/runtime/data/overhaul/startup/lg27.png'
Copy-Item -LiteralPath $logo -Destination (Join-Path $Project 'sources/icon.png')
$recipes.Add(@{op='image';source='sources/icon.png';target='icon.png';width=256;height=256;fit='contain';format='png';mipLevels=1;padding=0})
@'
# MVP 27 UI

Enable MVP 27 UI in the launcher, then restart the game. --no-mods disables it.
This optional UI mod does not change dynasty data or save layouts. It contains host UI artwork, fonts, strings, video and a native loading-bar texture. Its 512px team logos are host-screen previews; native game-logo upgrades are a separate Phase 4 mod.

Sources and recipes: mvp27-ui.mvpproject. Build with tools/MvpProject; see docs/MVPPROJECT.md. Artwork sources are retained at their imported master size. No executable or translated game code is included.
'@ | Set-Content (Join-Path $Project 'overrides/readme.md')
$recipes.Add(@{op='copy';source='overrides/readme.md';target='readme.md'})
@{format=1;mod=@{format=1;id='mvp27-ui';name='MVP 27 UI';version='1.0.0';author='MVP 27';description='Optional MVP 27 interface, menus and presentation.';game=@{xbeSha1='2f57040bab2c8b2eb33ebc7aa93209ad30dd103f'};layout=1;affectsSaves=$false};gameRelease='MVP 06 NCAA Baseball (Xbox)';build=@{archive='stored-zip64';folder='build/mvp27-ui';defaultEnabled=$true}}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $Project 'project.json')
@{format=1;recipes=@($recipes.ToArray())}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $Project 'recipes.json')
@{format=1;files=@($inventory.ToArray());notes='Lossless imported masters; generated wide logo variants retained only as sources. Build recipes replace legacy installation scripts with explicit copy/resize operations; previews/colors are editable frozen sources rather than reads from a mutable game install.'}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $Project 'source-inventory.json')
"/build/`n"|Set-Content (Join-Path $Project '.gitignore')
"Imported $($inventory.Count) files; $($recipes.Count) recipes -> $Project"
