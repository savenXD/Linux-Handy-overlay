# Exercises the native kill-switch purge (cronq.exe --purge / --purge-dry-run).
# The dry run is safe anywhere. The real run only happens on CI: it stops processes named cronq*/cronqw
# and edits the real HKCU registry, so it never runs on a developer PC.
param([string]$BinDir = (Join-Path $PSScriptRoot '../build/Release'))
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$exe = Join-Path $BinDir 'cronq.exe'
if (-not (Test-Path $exe)) { throw "cronq.exe not found in $BinDir" }
$root = Join-Path ([IO.Path]::GetTempPath()) ("cronq-purge-test-" + [guid]::NewGuid().ToString('N'))
$local = Join-Path $root 'Local'; $roaming = Join-Path $root 'Roaming'; $temp = Join-Path $root 'Temp'
$install = Join-Path $local 'Programs\Cronq'
$portable = Join-Path $root 'Portable'
foreach ($d in $install, (Join-Path $local 'cronq\webview'), (Join-Path $roaming 'cronq'), $temp, $portable) {
    New-Item -ItemType Directory -Path $d -Force | Out-Null
}
Set-Content (Join-Path $install 'cronqw.exe') 'x'
Set-Content (Join-Path $local 'cronq\webview\login.db') 'secret'
Set-Content (Join-Path $roaming 'cronq\config.ini') 'x'
Set-Content (Join-Path $portable 'cronq.exe') 'x'
Set-Content (Join-Path $portable 'notes.txt') 'keep me'

$saved = @{ L = $env:LOCALAPPDATA; R = $env:APPDATA; T = $env:TEMP }
try {
    $env:LOCALAPPDATA = $local; $env:APPDATA = $roaming; $env:TEMP = $temp

    $out = & $exe --purge-dry-run $install | Out-String
    if ($out -notmatch [regex]::Escape((Join-Path $local 'cronq'))) { throw "dry run did not list the data dir:`n$out" }
    if ($out -notmatch [regex]::Escape($install)) { throw "dry run did not list the install dir:`n$out" }
    if (-not (Test-Path (Join-Path $local 'cronq\webview\login.db'))) { throw 'dry run deleted data' }
    if (-not (Test-Path $install)) { throw 'dry run deleted the install dir' }
    Write-Output 'PASS: dry run reports removals and deletes nothing'

    $out = & $exe --purge-dry-run $portable | Out-String
    if ($out -notmatch [regex]::Escape((Join-Path $portable 'cronq.exe'))) { throw "portable: own file not listed:`n$out" }
    if ($out -match 'notes\.txt') { throw "portable: would delete an unrelated file:`n$out" }
    if ($out -match ('would delete ' + [regex]::Escape($portable) + '\r?\n')) { throw "portable: would delete the folder:`n$out" }
    Write-Output 'PASS: portable folder keeps unrelated files'

    if ($env:CI -eq 'true') {
        $copy = Join-Path $temp 'cronq-purge-test.exe'   # the purge deletes the copy it runs from
        Copy-Item $exe $copy
        & $copy --purge $install 0 | Out-Null
        foreach ($p in $install, (Join-Path $local 'cronq'), (Join-Path $roaming 'cronq')) {
            if (Test-Path $p) { throw "not removed: $p" }
        }
        Write-Output 'PASS: install dir and all data directories are removed'
    }
} finally {
    $env:LOCALAPPDATA = $saved.L; $env:APPDATA = $saved.R; $env:TEMP = $saved.T
    Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}
