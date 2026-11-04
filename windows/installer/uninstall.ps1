<#
.SYNOPSIS
  Developer uninstallation script for Cronq (Windows).
  Mirrors Linux uninstall.sh.
.PARAMETER Purge
  Deletes configuration, logins, and webview data in AppData.
#>
param(
    [switch]$Purge
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $ScriptDir 'process.ps1')

$InstallDir = Join-Path $env:LOCALAPPDATA "Programs\Cronq"
$Exe = Join-Path $InstallDir "cronq.exe"

# Stop running instance
if (Test-Path $Exe) {
    Write-Host "Stopping running Cronq instance..."
    try {
        & $Exe kill | Out-Null
    } catch {
        # Ignore errors
    }
}

Wait-CronqExit -InstallDir $InstallDir

# Remove Start Menu shortcut
$StartMenuDir = [Environment]::GetFolderPath('Programs')
$ShortcutPath = Join-Path $StartMenuDir "Cronq.lnk"
if (Test-Path $ShortcutPath) {
    Remove-Item -Force $ShortcutPath
    Write-Host "Removed Start Menu shortcut."
}

# Remove Autostart Run key
$RunKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
if (Get-ItemProperty -Path $RunKey -Name "Cronq" -ErrorAction SilentlyContinue) {
    Remove-ItemProperty -Path $RunKey -Name "Cronq"
    Write-Host "Removed autostart Run entry."
}

# Remove from user PATH
$UserEnvKey = "HKCU:\Environment"
$CurrentPath = [Environment]::GetEnvironmentVariable('Path', 'User')
if ($CurrentPath -and $CurrentPath -like "*$InstallDir*") {
    $Parts = $CurrentPath.Split(';') | Where-Object { $_ -ne "" -and $_ -ne $InstallDir }
    $NewPath = [string]::Join(';', $Parts)
    [Environment]::SetEnvironmentVariable('Path', $NewPath, 'User')
    Write-Host "Removed $InstallDir from user PATH."
}

# Remove installation directory
if (Test-Path $InstallDir) {
    Remove-Item -Recurse -Force $InstallDir
    Write-Host "Removed installation directory: $InstallDir"
}

# Purge data if requested
if ($Purge) {
    Write-Host "Purging settings, webview data and logs..."
    $AppData = Join-Path $env:APPDATA "cronq"
    $LocalAppData = Join-Path $env:LOCALAPPDATA "cronq"

    if (Test-Path $AppData) {
        Remove-Item -Recurse -Force $AppData
        Write-Host "Deleted $AppData"
    }
    if (Test-Path $LocalAppData) {
        Remove-Item -Recurse -Force $LocalAppData
        Write-Host "Deleted $LocalAppData"
    }
    Write-Host "Note: Screenshots in Pictures\cronq are preserved."
} else {
    Write-Host "Settings and login state retained in AppData. Pass -Purge to remove them."
}

Write-Host "Cronq uninstalled successfully."
