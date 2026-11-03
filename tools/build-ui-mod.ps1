#requires -Version 7.0
param([string]$Project,[string]$Stage)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot
$parent=Split-Path $repo
$workspace=if((Split-Path $parent -Leaf) -eq 'MVP06-Base'){Split-Path $parent}else{$parent}
if(!$Project){$Project=Join-Path $workspace 'projects/mvp27-ui.mvpproject'}
if(!$Stage){$Stage=Join-Path $repo 'out/build/release/bundled-mods'}
if(Test-Path (Join-Path $Project 'project.json')){
    & dotnet build (Join-Path $PSScriptRoot 'MvpProject') -c Release --nologo -v q
    if($LASTEXITCODE){throw 'Project builder failed'}
    & dotnet (Join-Path $PSScriptRoot 'MvpProject/bin/Release/net9.0-windows/MvpProject.dll') $Project
    if($LASTEXITCODE){throw 'UI project build failed'}
    New-Item -ItemType Directory -Force $Stage|Out-Null
    foreach($name in @('mvp27-ui.mvpmod','mvp27-ui.mvpmod.sha256')){
        Copy-Item -LiteralPath (Join-Path $Project ('build/'+$name)) -Destination (Join-Path $Stage $name)
    }
    $metadata=Get-Content (Join-Path $Project 'project.json') -Raw|ConvertFrom-Json
    @{format=1;'mvp27-ui'=[bool]$metadata.build.defaultEnabled}|ConvertTo-Json|Set-Content (Join-Path $Stage 'defaults.json')
}
$archive=Join-Path $Stage 'mvp27-ui.mvpmod'
if(!(Test-Path $archive) -or !(Test-Path ($archive+'.sha256')) -or !(Test-Path (Join-Path $Stage 'defaults.json'))){throw 'UI project or previously built bundle is required; see docs/MVPPROJECT.md'}
$expected=(Get-Content ($archive+'.sha256')).Split(' ')[0]
if((Get-FileHash -LiteralPath $archive).Hash -ne $expected){throw 'Staged UI bundle checksum mismatch'}
"UI bundle staged: $archive"
