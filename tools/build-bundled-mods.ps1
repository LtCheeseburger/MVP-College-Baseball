#requires -Version 7.0
param([string]$Projects,[string]$Stage)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot
$parent=Split-Path $repo
$workspace=if((Split-Path $parent -Leaf) -eq 'MVP06-Base'){Split-Path $parent}else{$parent}
if(!$Projects){$Projects=Join-Path $workspace 'projects'}
if(!$Stage){$Stage=Join-Path $repo 'out/build/release/bundled-mods'}
& (Join-Path $PSScriptRoot 'build-ui-mod.ps1') -Project (Join-Path $Projects 'mvp27-ui.mvpproject') -Stage $Stage
foreach($id in @('mvp27-league','mvp27-logos')){
    $archive=Join-Path $Projects "$id.mvpproject/build/$id.mvpmod"
    if(!(Test-Path $archive) -or !(Test-Path "$archive.sha256")){throw "Build the $id project before packaging."}
    $expected=(Get-Content "$archive.sha256").Split(' ')[0]
    if((Get-FileHash -LiteralPath $archive).Hash -ne $expected){throw "$id checksum mismatch"}
    Copy-Item -LiteralPath $archive -Destination $Stage
}
@{format=1;'mvp27-league'=$true;'mvp27-logos'=$false;'mvp27-ui'=$true}|ConvertTo-Json|Set-Content (Join-Path $Stage 'defaults.json')
