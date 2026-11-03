#requires -Version 7.0
[CmdletBinding()]
param(
    [string]$Base,
    [string]$Dev,
    [string]$Allowlist = (Join-Path $PSScriptRoot 'sync-allowlist.json'),
    [string]$Report
)
$ErrorActionPreference = 'Stop'
$localTree = Split-Path $PSScriptRoot
$parent = Split-Path $localTree
$workspace = if ((Split-Path $parent -Leaf) -eq 'MVP06-Base') { Split-Path $parent } else { $parent }
if (-not $Base) { $Base = Join-Path $workspace 'MVP06-Base/MVP06-XBERecomp' }
if (-not $Dev) { $Dev = Join-Path $workspace 'MVP06-XBERecomp' }
# Relative paths, applied identically in both directions. No general *.cpp or sdk exclusions.
$excludedRoots = @('.git','assets','generated','out','build','logs','hdd','CrashDumps','.vs','.vscode','.idea','.local','backups')
$excludedPaths = @('launcher/bin','launcher/obj','docs/m0',
    'external/MVP-Workshop/src/MVPWorkshop.Core/bin',
    'external/MVP-Workshop/src/MVPWorkshop.Core/obj',
    'tools/MvpProject/bin','tools/MvpProject/obj',
    'tools/tests/Phase3/bin','tools/tests/Phase3/obj',
    'tools/tests/Phase4/bin','tools/tests/Phase4/obj')
$excludedSuffixes = @('.zip','.zip.sha256','.pdb','.exe','.dll','.obj','.o')
$textExtensions = @('.cpp','.c','.h','.hpp','.inl','.inc','.cmake','.txt','.md','.json','.toml','.yaml','.yml','.xml','.cs','.csproj','.props','.targets','.sln','.ps1','.bat','.cmd','.py','.sh','.rc','.csv','.tsv','.gitignore','.gitattributes','.gitmodules')
function Get-Tree($root) {
    $root = (Resolve-Path -LiteralPath $root).Path
    $map = @{}; $queue = [Collections.Generic.Queue[string]]::new(); $queue.Enqueue($root)
    while ($queue.Count) {
        foreach ($item in Get-ChildItem -LiteralPath $queue.Dequeue() -Force) {
            $rel = [IO.Path]::GetRelativePath($root,$item.FullName).Replace('\','/')
            $parts = $rel.Split('/')
            if ($parts[0] -in $excludedRoots -or $item.Name -eq '.git') { continue }
            # Unpacked release output, identified by its versioned name and package layout.
            if ($parts.Count -eq 1 -and $item.PSIsContainer -and
                $item.Name -match '^MVP-27-College-Baseball-v\d[^/]*$' -and
                (Test-Path -LiteralPath (Join-Path $item.FullName 'Setup.exe') -PathType Leaf) -and
                (Test-Path -LiteralPath (Join-Path $item.FullName 'Port Files/port-manifest.json') -PathType Leaf)) { continue }
            if (@($excludedPaths | Where-Object { $rel -eq $_ -or $rel.StartsWith($_ + '/') }).Count) { continue }
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Unexpected link (add a reviewed exclusion): $root/$rel" }
            if ($item.PSIsContainer) { $queue.Enqueue($item.FullName); continue }
            if ($rel -eq 'settings.toml' -or @($excludedSuffixes | Where-Object { $rel.EndsWith($_) }).Count) { continue }
            $map[$rel] = $item.FullName
        }
    }
    return $map
}
function Get-ComparisonHash($path) {
    $bytes = [IO.File]::ReadAllBytes($path)
    $ext = [IO.Path]::GetExtension($path).ToLowerInvariant()
    if ($ext -in $textExtensions -or [IO.Path]::GetFileName($path) -in @('VERSION','SDK_VERSION','LICENSE','CMakeLists.txt')) {
        try {
            # Strict UTF-8, no lossy decoding; NUL-bearing/binary data stays byte-exact.
            $utf8 = [Text.UTF8Encoding]::new($false,$true)
            $s = $utf8.GetString($bytes)
            if (-not $s.Contains([char]0)) { $bytes = $utf8.GetBytes($s.Replace("`r`n","`n").Replace("`r","`n")) }
        } catch [Text.DecoderFallbackException] { }
    }
    return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes))
}
$a = Get-Tree $Base; $b = Get-Tree $Dev
$allowed = @()
if (Test-Path -LiteralPath $Allowlist) { $allowed = @(Get-Content -LiteralPath $Allowlist -Raw | ConvertFrom-Json) }
foreach ($entry in $allowed) {
    if (-not $entry.path -or $entry.path -match '[*?]' -or -not $entry.reason -or $entry.status -notin @('AddedInDev','MissingInDev','Changed') -or (-not $entry.baseHash -and -not $entry.devHash)) { throw 'Allowlist requires exact path, status, reason and expected comparison hashes.' }
}
$rows = @(
    foreach ($rel in @(@($a.Keys) + @($b.Keys) | Sort-Object -Unique)) {
        $ah = if ($a.ContainsKey($rel)) { Get-ComparisonHash $a[$rel] } else { '' }
        $bh = if ($b.ContainsKey($rel)) { Get-ComparisonHash $b[$rel] } else { '' }
        if ($ah -eq $bh) { continue }
        $status = if (-not $ah) { 'AddedInDev' } elseif (-not $bh) { 'MissingInDev' } else { 'Changed' }
        $match = @($allowed | Where-Object { $_.path -ceq $rel -and $_.status -eq $status -and $_.baseHash -eq $ah -and $_.devHash -eq $bh })
        [pscustomobject]@{Status=$status;Path=$rel;Allowed=($match.Count -eq 1);BaseHash=$ah;DevHash=$bh;Reason=($match.reason -join '; ')}
    }
)
if ($Report) { ConvertTo-Json -InputObject $rows -Depth 4 | Set-Content -LiteralPath $Report }
$rows | Format-Table Status,Path,Allowed -AutoSize | Out-Host
$unexplained = @($rows | Where-Object { -not $_.Allowed }).Count
Write-Host "$($rows.Count) differences; $unexplained unexplained. Added/missing are relative to DEV."
if ($unexplained) { exit 1 }
exit 0
