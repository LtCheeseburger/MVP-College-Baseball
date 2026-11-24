#requires -Version 7.0
# Stages the one default mod the release installs: the combined MVP27 Overhaul.
param([string]$Projects,[string]$Stage)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot
$parent=Split-Path $repo
$workspace=if((Split-Path $parent -Leaf) -eq 'MVP06-Base'){Split-Path $parent}else{$parent}
if(!$Projects){$Projects=Join-Path $workspace 'projects'}
if(!$Stage){$Stage=Join-Path $repo 'out/build/release/bundled-mods'}
if(Test-Path $Stage){Remove-Item (Join-Path $Stage '*.mvpmod*') -Force}else{New-Item -ItemType Directory $Stage|Out-Null}
$archive=Join-Path $Projects 'MVP27-Overhaul.mvpproject/build/MVP27-Overhaul.mvpmod'
if(!(Test-Path $archive) -or !(Test-Path "$archive.sha256")){throw 'Build the MVP27-Overhaul project before packaging.'}
$expected=(Get-Content "$archive.sha256").Split(' ')[0]
if((Get-FileHash -LiteralPath $archive).Hash -ne $expected){throw 'MVP27-Overhaul checksum mismatch'}
Copy-Item -LiteralPath $archive -Destination (Join-Path $Stage 'mvp27-overhaul.mvpmod')
@{format=1;'mvp27-overhaul'=$true}|ConvertTo-Json|Set-Content (Join-Path $Stage 'defaults.json')
