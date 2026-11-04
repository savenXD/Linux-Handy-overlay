# Exercises installer/killswitch.ps1. The dry run is safe anywhere; the real run only happens on CI
# (it stops processes named cronq*/cronqw and edits the real registry, so it never runs on a dev PC).
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot '../installer/killswitch.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) ("cronq-ks-test-" + [guid]::NewGuid().ToString('N'))
$local = Join-Path $root 'Local'; $roaming = Join-Path $root 'Roaming'; $temp = Join-Path $root 'Temp'
$install = Join-Path $local 'Programs\Cronq'
foreach ($d in $install, (Join-Path $local 'cronq\webview'), (Join-Path $roaming 'cronq'), $temp) {
    New-Item -ItemType Directory -Path $d -Force | Out-Null
}
Set-Content (Join-Path $install 'cronqw.exe') 'x'
Set-Content (Join-Path $local 'cronq\webview\login.db') 'secret'
Set-Content (Join-Path $roaming 'cronq\config.ini') 'x'

$saved = @{ L = $env:LOCALAPPDATA; R = $env:APPDATA; T = $env:TEMP }
try {
    $env:LOCALAPPDATA = $local; $env:APPDATA = $roaming; $env:TEMP = $temp

    $out = & pwsh -NoProfile -File $script -InstallDir $install -DryRun | Out-String
    if ($out -notmatch [regex]::Escape((Join-Path $local 'cronq'))) { throw "dry run did not list the data dir:`n$out" }
    if (-not (Test-Path (Join-Path $local 'cronq\webview\login.db'))) { throw 'dry run deleted data' }
    if (-not (Test-Path $install)) { throw 'dry run deleted the install dir' }
    Write-Output 'PASS: dry run reports removals and deletes nothing'

    if ($env:CI -eq 'true') {
        $copy = Join-Path $temp 'cronq-kill-test.ps1'
        Copy-Item $script $copy
        & pwsh -NoProfile -File $copy -InstallDir $install | Out-Null
        foreach ($p in $install, (Join-Path $local 'cronq'), (Join-Path $roaming 'cronq')) {
            if (Test-Path $p) { throw "not removed: $p" }
        }
        Write-Output 'PASS: install dir and all data directories are removed'
    }
} finally {
    $env:LOCALAPPDATA = $saved.L; $env:APPDATA = $saved.R; $env:TEMP = $saved.T
    Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}
