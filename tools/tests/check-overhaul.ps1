param([string]$Output)
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$parent=Split-Path $repo
$workspace=if((Split-Path $parent -Leaf) -eq 'MVP06-Base'){Split-Path $parent}else{$parent}
if(!$Output){$Output=Join-Path $workspace 'artifacts/phase2'}
$ErrorActionPreference='Stop'
$Output=[IO.Path]::GetFullPath($Output)
New-Item -ItemType Directory -Force $Output|Out-Null
$env:TEMP=Join-Path $Output 'tmp';$env:TMP=$env:TEMP
New-Item -ItemType Directory -Force $env:TEMP|Out-Null
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
Push-Location $repo
try {
    & ./env.cmd clang++ -std=c++20 -DNDEBUG -D_CRT_SECURE_NO_WARNINGS -D_SILENCE_CXX20_U8PATH_DEPRECATION_WARNING -Isdk/runtime -Isrc tools/tests/overhaul_check.cpp src/common/overhaul.cpp sdk/runtime/kernel/mods.cpp sdk/runtime/kernel/inflate.cpp -o (Join-Path $Output 'overhaul_check.exe') > (Join-Path $Output 'check-build.log') 2>&1
    if($LASTEXITCODE){throw 'Headless check build failed'}
} finally { Pop-Location }
$fixtures=Join-Path $Output 'fixtures'
New-Item -ItemType Directory -Force $fixtures|Out-Null
$game=Join-Path $fixtures 'game'
$startup=Join-Path $game 'data/overhaul/startup'
New-Item -ItemType Directory -Force $startup|Out-Null
Set-Content (Join-Path $startup 'probe.txt') 'loose content' -NoNewline
Set-Content (Join-Path $game 'native.txt') 'native' -NoNewline
Set-Content (Join-Path $game 'data/overhaul/overhaul.json') '{}' -NoNewline
function Mod($folder,$id,$marker=$true) {
    $path=Join-Path $fixtures $folder
    New-Item -ItemType Directory -Force (Join-Path $path 'files/data/overhaul/startup')|Out-Null
    @{format=1;id=$id;name=$id;version='1.0.0';game=@{xbeSha1='2f57040bab2c8b2eb33ebc7aa93209ad30dd103f'};layout=1;affectsSaves=$false}|ConvertTo-Json|Set-Content (Join-Path $path 'mod.json')
    Set-Content (Join-Path $path 'files/data/overhaul/startup/probe.txt') 'mod content' -NoNewline
    if($marker){Set-Content (Join-Path $path 'files/data/overhaul/overhaul.json') '{}' -NoNewline}
    return $path
}
$ui=Mod 'ui' 'mvp27-ui'
$missing=Mod 'missing-marker' 'mvp27-ui' $false
$other=Mod 'other' 'other-mod'
$off=Join-Path $fixtures 'off'
New-Item -ItemType Directory -Force $off|Out-Null
$mods=Join-Path $fixtures 'mods'
New-Item -ItemType Directory -Force $mods|Out-Null
$enabled=Join-Path $mods 'mvp27-ui'
if(!(Test-Path $enabled)){Copy-Item -LiteralPath $ui -Destination $enabled -Recurse}
@{format=1;order=@(@{id='mvp27-ui';enabled=$true})}|ConvertTo-Json -Depth 4|Set-Content (Join-Path $mods 'mods.json')
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=Join-Path $fixtures 'ui.mvpmod'
if(Test-Path $archive){Remove-Item -LiteralPath $archive}
[IO.Compression.ZipFile]::CreateFromDirectory($ui,$archive,[IO.Compression.CompressionLevel]::NoCompression,$false)
$env:TEMP=Join-Path $Output 'tmp';$env:TMP=$env:TEMP
New-Item -ItemType Directory -Force $env:TEMP|Out-Null
$exe=Join-Path $Output 'overhaul_check.exe'
$cases=@(
    @{Name='loose-marker-only';Mods=$off;Mode='normal';Expected='off';Extra=@()},
    @{Name='saved-enabled';Mods=$mods;Mode='normal';Expected='on';Extra=@()},
    @{Name='saved-safe';Mods=$mods;Mode='safe';Expected='off';Extra=@()},
    @{Name='explicit-folder';Mods=$off;Mode='normal';Expected='on';Extra=@($ui)},
    @{Name='explicit-safe';Mods=$off;Mode='safe';Expected='off';Extra=@($ui)},
    @{Name='wrong-id';Mods=$off;Mode='normal';Expected='off';Extra=@($other)},
    @{Name='missing-marker';Mods=$off;Mode='normal';Expected='off';Extra=@($missing)},
    @{Name='packed-mod';Mods=$off;Mode='normal';Expected='on';Extra=@($archive)},
    @{Name='packed-safe';Mods=$off;Mode='safe';Expected='off';Extra=@($archive)},
    @{Name='on-then-off-1';Mods=$off;Mode='normal';Expected='on';Extra=@($ui)},
    @{Name='on-then-off-2';Mods=$off;Mode='normal';Expected='off';Extra=@()},
    @{Name='on-then-off-3';Mods=$off;Mode='normal';Expected='on';Extra=@($ui)}
)
foreach($case in $cases){
    $arguments=@($game,$case.Mods,$case.Mode,$case.Expected)+$case.Extra
    & $exe @arguments > (Join-Path $Output ($case.Name+'.log')) 2>&1
    if($LASTEXITCODE){throw "Failed: $($case.Name)"}
    "PASS $($case.Name)"
}
