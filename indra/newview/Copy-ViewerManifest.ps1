#!/usr/bin/env pwsh
<#
.SYNOPSIS
    KV Viewer S24 Build Manifest - Essential file copying for build/debug
.DESCRIPTION
    PowerShell-only viewer manifest (Python removed!)
    Copies essential files for build/debug workflow
    Full packaging is handled by winpack.ps1
.PARAMETER BuildDir
    Build directory (e.g., build-vc145)
.PARAMETER Configuration
    Build configuration (Release, Debug, RelWithDebInfo)
.PARAMETER SourceDir
    Source directory (newview)
.PARAMETER Touch
    Touch file to indicate completion
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$BuildDir,

    [Parameter(Mandatory)]
    [string]$Configuration,

    [Parameter(Mandatory)]
    [string]$SourceDir,

    [Parameter()]
    [string]$Touch
)

$ErrorActionPreference = 'Stop'

Write-Host "=== KV S24 Viewer Manifest (PowerShell-Only) ===" -ForegroundColor Magenta
Write-Host "Build Dir: $BuildDir" -ForegroundColor Cyan
Write-Host "Config: $Configuration" -ForegroundColor Cyan
Write-Host "Source: $SourceDir`n" -ForegroundColor Cyan

$destDir = Join-Path $BuildDir $Configuration

# Ensure destination exists
if (-not (Test-Path $destDir)) {
    New-Item -ItemType Directory -Path $destDir -Force | Out-Null
}

#region Essential Runtime Files

Write-Host "Copying essential runtime files..." -ForegroundColor Yellow

# Message template and configuration
$scriptsDir = Join-Path (Split-Path $SourceDir -Parent) ".." "scripts"
$etcDir = Join-Path (Split-Path $SourceDir -Parent) ".." "etc"

$appSettingsDir = Join-Path $destDir "app_settings"
if (-not (Test-Path $appSettingsDir)) {
    New-Item -ItemType Directory -Path $appSettingsDir -Force | Out-Null
}

# Copy message template
$msgTemplate = Join-Path $scriptsDir "messages" "message_template.msg"
if (Test-Path $msgTemplate) {
    Copy-Item $msgTemplate (Join-Path $appSettingsDir "message_template.msg") -Force
    Write-Host "  ✓ message_template.msg" -ForegroundColor Green
}

# Copy message.xml
$msgXml = Join-Path $etcDir "message.xml"
if (Test-Path $msgXml) {
    Copy-Item $msgXml (Join-Path $appSettingsDir "message.xml") -Force
    Write-Host "  ✓ message.xml" -ForegroundColor Green
}

# Copy essential app_settings files for debugging
$appSettingsSource = Join-Path $SourceDir "app_settings"
if (Test-Path $appSettingsSource) {
    # Copy critical XML/INI files
    $criticalFiles = @(
        "settings.xml",
        "settings_files.xml", 
        "settings_per_account.xml",
        "cmd_line.xml",
        "logcontrol.xml",
        "keywords_lsl_default.xml",
        "colors.xml"
    )

    foreach ($file in $criticalFiles) {
        $srcFile = Join-Path $appSettingsSource $file
        if (Test-Path $srcFile) {
            Copy-Item $srcFile (Join-Path $appSettingsDir $file) -Force
            Write-Host "  ✓ $file" -ForegroundColor Green
        }
    }
}

#endregion

# Touch completion file
if ($Touch) {
    $touchDir = Split-Path $Touch -Parent
    if (-not (Test-Path $touchDir)) {
        New-Item -ItemType Directory -Path $touchDir -Force | Out-Null
    }

    # Create completion marker
    "@echo Viewer manifest copy completed at $(Get-Date)" | Out-File $Touch -Encoding ascii -Force
    Write-Host "`n✓ Essential files copied" -ForegroundColor Green
    Write-Host "Touch file: $Touch" -ForegroundColor DarkGray
}

Write-Host "`nNote: Full packaging is handled by winpack.ps1" -ForegroundColor Cyan
Write-Host "Python dependency removed - PowerShell only! 🚀`n" -ForegroundColor Green

