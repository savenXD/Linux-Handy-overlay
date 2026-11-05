<#
.SYNOPSIS
  Developer installation script for Cronq (Windows).
  Mirrors Linux install.sh. Installs to %LOCALAPPDATA%\Programs\Cronq.
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir 'process.ps1')
$ProjectDir = Split-Path -Parent $ScriptDir
$BinDir = @('dist', 'build\Release', 'build', 'build-mingw') |
    ForEach-Object { Join-Path $ProjectDir $_ } |
    Where-Object { Test-Path (Join-Path $_ 'cronq.exe') } | Select-Object -First 1
if (-not $BinDir) { throw "Build output not found. Run make-dist.ps1 first." }

if (-not (Test-Path (Join-Path $BinDir "cronq.exe"))) {
    Write-Error "Build output not found. Please build Cronq or run make-dist.ps1 first."
    exit 1
}

foreach ($name in @('cronq.exe', 'cronqw.exe', 'WebView2Loader.dll', 'ui\index.html', 'ui\cronq.css', 'ui\cronq.js')) {
    if (-not (Test-Path (Join-Path $BinDir $name) -PathType Leaf)) { throw "Missing build payload: $name" }
}

$InstallDir = Join-Path $env:LOCALAPPDATA "Programs\Cronq"
Write-Host "Installing Cronq to: $InstallDir"

# Stop existing instance if running
$OldExe = Join-Path $InstallDir "cronq.exe"
if (Test-Path $OldExe) {
    Write-Host "Stopping running instance..."
    try {
        & $OldExe kill | Out-Null
    } catch {
        # Ignore errors during stop
    }
}

Wait-CronqExit -InstallDir $InstallDir

# Create installation directory
if (-not (Test-Path $InstallDir)) {
    New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
}

# Copy executables and libraries
Copy-Item (Join-Path $BinDir "cronq.exe") -Destination $InstallDir -Force
Copy-Item (Join-Path $BinDir "cronqw.exe") -Destination $InstallDir -Force

$LoaderDll = Join-Path $BinDir "WebView2Loader.dll"
if (Test-Path $LoaderDll) {
    Copy-Item $LoaderDll -Destination $InstallDir -Force
}


# Copy UI folder
$UiSrc = Join-Path $BinDir "ui"
if (-not (Test-Path (Join-Path $UiSrc "index.html"))) { throw "Built UI assets missing" }
if (Test-Path $UiSrc) {
    $UiDest = Join-Path $InstallDir "ui"
    if (Test-Path $UiDest) { Remove-Item -Recurse -Force $UiDest }
    Copy-Item -Path $UiSrc -Destination $UiDest -Recurse -Force
}

# Create Start Menu shortcut
$StartMenuDir = [Environment]::GetFolderPath('Programs')
$ShortcutPath = Join-Path $StartMenuDir "Cronq.lnk"
$WshShell = New-Object -ComObject WScript.Shell
$Shortcut = $WshShell.CreateShortcut($ShortcutPath)
$Shortcut.TargetPath = Join-Path $InstallDir "cronqw.exe"
$Shortcut.WorkingDirectory = $InstallDir
$Shortcut.Description = "Cronq - Always-on-top overlay"
$Shortcut.Save()
Write-Host "Created Start Menu shortcut: $ShortcutPath"

# Configure Autostart Run key
$RunKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
if (-not (Test-Path $RunKey)) { New-Item -Path $RunKey | Out-Null }
$OverlayCmd = "`"$InstallDir\cronqw.exe`" --supervise"
Set-ItemProperty -Path $RunKey -Name "Cronq" -Value $OverlayCmd
Write-Host "Configured autostart in HKCU Run key."

# Add to user PATH if not present
$UserEnvKey = "HKCU:\Environment"
$CurrentPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if (-not $CurrentPath) { $CurrentPath = "" }
if ($InstallDir -notin @($CurrentPath.Split(';') | ForEach-Object { $_.Trim().TrimEnd('\') })) {
    $NewPath = if ($CurrentPath -eq "") { $InstallDir } else { "$CurrentPath;$InstallDir" }
    [Environment]::SetEnvironmentVariable('Path', $NewPath, 'User')
    Write-Host "Added $InstallDir to user PATH."
}

# Launch the installed app
$TargetExe = Join-Path $InstallDir "cronqw.exe"
Write-Host "Launching Cronq..."
Start-Process -FilePath $TargetExe

Write-Host "Cronq installed successfully!"
