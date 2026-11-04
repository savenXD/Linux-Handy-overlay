<#
.SYNOPSIS
  Builds Cronq in Release configuration and stages files into windows/dist/ for packaging.
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$WindowsDir = Split-Path -Parent $ScriptDir
$BuildDir = Join-Path $WindowsDir "build"
$DistDir = Join-Path $WindowsDir "dist"

Write-Host "=== Configuring Release build ==="
cmake -S $WindowsDir -B $BuildDir -A x64 -DCRONQ_WEBVIEW2=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed ($LASTEXITCODE)" }

Write-Host "=== Building Release target ==="
cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) { throw "CMake build failed ($LASTEXITCODE)" }

$ReleaseBin = Join-Path $BuildDir "Release"
if (-not (Test-Path $ReleaseBin)) {
    # MinGW or single-config build fallback
    $ReleaseBin = $BuildDir
}

$UiSrc = Join-Path $ReleaseBin "ui"
foreach ($name in @('cronq.exe', 'cronqw.exe', 'WebView2Loader.dll', 'ui\index.html', 'ui\cronq.css', 'ui\cronq.js')) {
    if (-not (Test-Path (Join-Path $ReleaseBin $name) -PathType Leaf)) { throw "Missing build payload: $name" }
}

Write-Host "=== Staging distribution directory: $DistDir ==="
$StagingDir = Join-Path $BuildDir 'staging-dist'
if (Test-Path $StagingDir) { Remove-Item -Recurse -Force $StagingDir }
New-Item -ItemType Directory -Path $StagingDir | Out-Null
foreach ($name in @('cronq.exe', 'cronqw.exe', 'WebView2Loader.dll')) {
    Copy-Item (Join-Path $ReleaseBin $name) -Destination $StagingDir -Force
}
Copy-Item -Path $UiSrc -Destination (Join-Path $StagingDir 'ui') -Recurse -Force
if (Test-Path $DistDir) { Remove-Item -Recurse -Force $DistDir }
Move-Item -Path $StagingDir -Destination $DistDir
Write-Host "Distribution staged successfully at: $DistDir"
Get-ChildItem -Path $DistDir
