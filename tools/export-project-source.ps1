#requires -Version 7.0
param([Parameter(Mandatory)][string]$Project,[Parameter(Mandatory)][string]$Output)
$ErrorActionPreference='Stop'
$Project=(Resolve-Path -LiteralPath $Project).Path
$Output=[IO.Path]::GetFullPath($Output)
if(!(Test-Path (Join-Path $Project 'project.json'))){throw 'Not an MVP project'}
if(Test-Path -LiteralPath $Output){throw 'Source archive already exists'}
if($Output.StartsWith($Project+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Write the archive outside the project'}
$repo=Split-Path $PSScriptRoot
foreach($relative in @('tools/MvpProject','external/MVP-Workshop/src/MVPWorkshop.Core')){
    $source=Join-Path $repo $relative
    foreach($file in Get-ChildItem -LiteralPath $source -File -Recurse -Force){
        $part=[IO.Path]::GetRelativePath($source,$file.FullName).Replace('\','/')
        if($part -match '(^|/)(bin|obj)/'){continue}
        $target=Join-Path $Project ($relative+'/'+$part)
        New-Item -ItemType Directory -Force (Split-Path $target)|Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $target
    }
}
New-Item -ItemType Directory -Force (Join-Path $Project 'docs')|Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'docs/MVPPROJECT.md') -Destination (Join-Path $Project 'docs/MVPPROJECT.md')
@'
$ErrorActionPreference='Stop'
& dotnet run --project (Join-Path $PSScriptRoot 'tools/MvpProject') -c Release -- $PSScriptRoot
if($LASTEXITCODE){throw 'Project build failed'}
'@|Set-Content (Join-Path $Project 'build.ps1')
New-Item -ItemType Directory -Force (Split-Path $Output)|Out-Null
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive=[IO.Compression.ZipFile]::Open($Output,[IO.Compression.ZipArchiveMode]::Create)
try{
    $count=0
    foreach($file in Get-ChildItem -LiteralPath $Project -File -Recurse -Force|Sort-Object FullName){
        $relative=[IO.Path]::GetRelativePath($Project,$file.FullName).Replace('\','/')
        if($relative.StartsWith('build/') -or $relative -match '/(bin|obj)/'){continue}
        if($file.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Linked source: $relative"}
        $entry=$archive.CreateEntry((Split-Path $Project -Leaf)+'/'+$relative,[IO.Compression.CompressionLevel]::NoCompression)
        $entry.LastWriteTime=[DateTimeOffset]::new(2000,1,1,0,0,0,[TimeSpan]::Zero)
        $input=$file.OpenRead();$stream=$entry.Open()
        try{$input.CopyTo($stream)}finally{$stream.Dispose();$input.Dispose()}
        $count++
    }
}finally{$archive.Dispose()}
$hash=(Get-FileHash -LiteralPath $Output).Hash.ToLowerInvariant()
"$hash *$([IO.Path]::GetFileName($Output))"|Set-Content ($Output+'.sha256')
"Exported $count source files: $Output"
