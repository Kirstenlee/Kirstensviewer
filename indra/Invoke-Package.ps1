#!/usr/bin/env pwsh
<#
.SYNOPSIS
    KV Viewer S24 Distribution Packaging Script
    
.DESCRIPTION
    Packages the compiled viewer into a distributable format by copying
    all required binaries, resources, assets, and dependencies.
    
.PARAMETER NonInteractive
    Run without user prompts (for CI/CD automation)
    
.PARAMETER Configuration
    Build configuration to package (Release, Debug, RelWithDebInfo)
    
.EXAMPLE
    .\Invoke-Package.ps1
    Package the viewer with interactive prompts
    
.EXAMPLE
    .\Invoke-Package.ps1 -NonInteractive
    Package the viewer without prompts (automation mode)
    
.EXAMPLE
    .\Invoke-Package.ps1 -Configuration Debug
    Package Debug configuration
    
.NOTES
    Copyright (c) 2025 Kirstens Viewer S24
    Part of KV Autobuild System
#>


[CmdletBinding()]
param(
    [Parameter()]
    [switch]$NonInteractive,
    
    [Parameter()]
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'

function Write-PackageHeader {
    Clear-Host
    Write-Host ('═' * 70) -ForegroundColor Cyan
    Write-Host "   KV Viewer S24 - Distribution Packaging" -ForegroundColor Magenta
    Write-Host "   Copyright (c) 2025 Kirstenlee Cinquetti" -ForegroundColor Yellow
    Write-Host ('═' * 70) -ForegroundColor Cyan
    Write-Host ""
}

function Write-Stage {
    param([string]$Message)
    Write-Host "`n$Message" -ForegroundColor Yellow
    Write-Host ('-' * 70) -ForegroundColor DarkGray
}

Write-PackageHeader

if (-not $NonInteractive) {
    Write-Host "Configuration: $Configuration" -ForegroundColor Cyan
    Write-Host "Output: S24-Packaged\" -ForegroundColor Cyan
    Write-Host ""
    Read-Host -Prompt 'Press Enter to begin packaging'
}

$packageStartTime = Get-Date

#region Stage 1 - Create Directory Structure

Write-Stage "Stage 1: Creating directory structure"

$dirs = @(
    'S24-Packaged',
    'S24-Packaged\app_settings\dictionaries',
    'S24-Packaged\character',
    'S24-Packaged\fonts',
    'S24-Packaged\llplugin',
    'S24-Packaged\skins'
)

for ($i = 0; $i -lt $dirs.Count; $i++) {
    $d = $dirs[$i]
    Write-Progress -Activity "Creating directories" -Status $d -PercentComplete (($i / $dirs.Count) * 100)
    
    if (-not (Test-Path $d)) {
        New-Item -ItemType Directory -Path $d -Force | Out-Null
        Write-Host "  Created: $d" -ForegroundColor Green
    }
    else {
        Write-Host "  Exists:  $d" -ForegroundColor DarkGray
    }
}

Write-Progress -Activity "Stage 1 Complete" -Completed
Write-Host "✓ Directory structure ready" -ForegroundColor Green

#endregion

#region Stage 2 - Copy Viewer Assets

Write-Stage "Stage 2: Copying viewer assets"

$assetCopies = @(
    @{ Src = 'newview\app_settings'; Dest = 'S24-Packaged\app_settings'; Name = 'App settings' },
    @{ Src = 'newview\character';    Dest = 'S24-Packaged\character';    Name = 'Characters' },
    @{ Src = 'newview\fonts';        Dest = 'S24-Packaged\fonts';        Name = 'Fonts' },
    @{ Src = 'newview\skins';        Dest = 'S24-Packaged\skins';        Name = 'Skins' }
)

for ($i = 0; $i -lt $assetCopies.Count; $i++) {
    $copy = $assetCopies[$i]
    Write-Progress -Activity "Copying viewer assets" -Status "$($copy.Name)" -PercentComplete (($i / $assetCopies.Count) * 100)
    
    Write-Host "  $($copy.Name): $($copy.Src) → $($copy.Dest)" -ForegroundColor Cyan
    
    $result = Start-Process -FilePath 'robocopy.exe' `
        -ArgumentList "`"$($copy.Src)`" `"$($copy.Dest)`" /E /NFL /NDL /NJH /NJS" `
        -NoNewWindow -Wait -PassThru
    
    if ($result.ExitCode -gt 7) {
        throw "Failed to copy $($copy.Name): Exit code $($result.ExitCode)"
    }
}

Write-Progress -Activity "Stage 2 Complete" -Completed
Write-Host "✓ Viewer assets copied" -ForegroundColor Green

#endregion

#region Stage 3 - Copy Dependencies

Write-Stage "Stage 3: Copying dependencies and resources"

$currentLocation = Get-Location
Set-Location ..

$dependencyCopies = @(
    @{ Src = 'bin\release';      Dest = 'indra\S24-Packaged\llplugin'; Args = '/E /XF SLVoice.exe /NFL /NDL /NJH /NJS'; Name = 'Runtime binaries' },
    @{ Src = 'resources';        Dest = 'indra\S24-Packaged\llplugin'; Args = '/E /NFL /NDL /NJH /NJS'; Name = 'Resources' },
    @{ Src = 'dictionaries';     Dest = 'indra\S24-Packaged\app_settings\dictionaries'; Args = '/E /NFL /NDL /NJH /NJS'; Name = 'Dictionaries' }
)

for ($i = 0; $i -lt $dependencyCopies.Count; $i++) {
    $copy = $dependencyCopies[$i]
    Write-Progress -Activity "Copying dependencies" -Status "$($copy.Name)" -PercentComplete (($i / $dependencyCopies.Count) * 100)
    
    if (Test-Path $copy.Src) {
        Write-Host "  $($copy.Name): $($copy.Src) → $($copy.Dest)" -ForegroundColor Cyan
        
        $result = Start-Process -FilePath 'robocopy.exe' `
            -ArgumentList "`"$($copy.Src)`" `"$($copy.Dest)`" $($copy.Args)" `
            -NoNewWindow -Wait -PassThru
        
        if ($result.ExitCode -gt 7) {
            throw "Failed to copy $($copy.Name): Exit code $($result.ExitCode)"
        }
    }
    else {
        Write-Host "  Warning: $($copy.Src) not found, skipping..." -ForegroundColor Yellow
    }
}

# Copy additional files
if (Test-Path 'etc\*.xml') {
    Write-Host "  Copying XML configuration files..." -ForegroundColor Cyan
    Copy-Item -Path 'etc\*.xml' -Destination 'indra\S24-Packaged\app_settings' -Force -ErrorAction SilentlyContinue
}

if (Test-Path 'lib\release\*.dll') {
    Write-Host "  Copying runtime libraries..." -ForegroundColor Cyan
    Copy-Item -Path 'lib\release\*.dll' -Destination 'indra\S24-Packaged' -Force -ErrorAction SilentlyContinue
}

if (Test-Path 'bin\release\SLVoice.exe') {
    Write-Host "  Copying SLVoice.exe..." -ForegroundColor Cyan
    Copy-Item -Path 'bin\release\SLVoice.exe' -Destination 'indra\S24-Packaged' -Force
}

if (Test-Path 'bin\release\openjp2.dll') {
    Write-Host "  Copying OpenJPEG library..." -ForegroundColor Cyan
    Copy-Item -Path 'bin\release\openjp2.dll' -Destination 'indra\S24-Packaged' -Force
}

Set-Location $currentLocation

Write-Progress -Activity "Stage 3 Complete" -Completed
Write-Host "✓ Dependencies copied" -ForegroundColor Green

#endregion

#region Stage 4 - Copy Compiled Binaries

Write-Stage "Stage 4: Copying compiled binaries"

$configPath = $Configuration.ToLower()

$binaryCopies = @(
    @{ Src = "build-vc145\newview\$configPath\Kirstens-S24.exe"; Dest = 'S24-Packaged'; Name = 'Viewer executable' },
    @{ Src = "build-vc145\llplugin\slplugin\$configPath\SLPlugin.exe"; Dest = 'S24-Packaged'; Name = 'Plugin host' },
    @{ Src = "build-vc145\llwebrtc\$configPath\llwebrtc.dll"; Dest = 'S24-Packaged'; Name = 'WebRTC library' }
)

for ($i = 0; $i -lt $binaryCopies.Count; $i++) {
    $binary = $binaryCopies[$i]
    Write-Progress -Activity "Copying compiled binaries" -Status "$($binary.Name)" -PercentComplete (($i / $binaryCopies.Count) * 100)
    
    if (Test-Path $binary.Src) {
        Write-Host "  $($binary.Name): $($binary.Src)" -ForegroundColor Cyan
        Copy-Item -Path $binary.Src -Destination $binary.Dest -Force
    }
    else {
        Write-Host "  Warning: $($binary.Src) not found!" -ForegroundColor Yellow
    }
}

# Copy viewer DLLs (excluding FMOD variants)
Write-Host "  Copying viewer DLLs..." -ForegroundColor Cyan
$robocopyResult = Start-Process -FilePath 'robocopy.exe' `
    -ArgumentList "newview S24-Packaged *.dll /XF fmodL.dll fmodstudio.dll fmodstudioL.dll /NFL /NDL /NJH /NJS" `
    -NoNewWindow -Wait -PassThru

# Copy viewer assets
if (Test-Path 'newview\ca-bundle.crt') {
    Copy-Item -Path 'newview\ca-bundle.crt' -Destination 'S24-Packaged' -Force
}

if (Test-Path 'newview\featuretable.txt') {
    Copy-Item -Path 'newview\featuretable.txt' -Destination 'S24-Packaged' -Force
}

# Copy build outputs (message templates only - settings.xml comes from source)
if (Test-Path "build-vc145\newview\$configPath\app_settings\*.msg") {
    Write-Host "  Copying message templates..." -ForegroundColor Cyan
    Copy-Item -Path "build-vc145\newview\$configPath\app_settings\*.msg" -Destination 'S24-Packaged\app_settings' -Force
}

# Copy generated XML files EXCEPT settings.xml and settings_files.xml (always use source versions from Stage 2)
if (Test-Path "build-vc145\newview\$configPath\app_settings\*.xml") {
    Write-Host "  Copying generated XML files (excluding settings.xml and settings_files.xml - using source versions)..." -ForegroundColor Cyan
    Get-ChildItem "build-vc145\newview\$configPath\app_settings\*.xml" -Exclude 'settings.xml','settings_files.xml' | 
        ForEach-Object { Copy-Item $_.FullName -Destination 'S24-Packaged\app_settings' -Force }
}

Write-Progress -Activity "Stage 4 Complete" -Completed
Write-Host "✓ Compiled binaries copied" -ForegroundColor Green

#endregion

#region Stage 5 - Copy Media Plugins

Write-Stage "Stage 5: Copying media plugins"

$mediaPluginPath = "build-vc145\media_plugins"

# LibVLC plugin
if (Test-Path "$mediaPluginPath\libvlc\$Configuration\*.dll") {
    Write-Host "  LibVLC media plugin..." -ForegroundColor Cyan
    Copy-Item -Path "$mediaPluginPath\libvlc\$Configuration\*.dll" -Destination 'S24-Packaged\llplugin' -Force
}

# CEF (Chromium Embedded Framework) plugin
if (Test-Path "$mediaPluginPath\cef\$Configuration\*.dll") {
    Write-Host "  CEF (Chromium) plugin..." -ForegroundColor Cyan
    Copy-Item -Path "$mediaPluginPath\cef\$Configuration\*.dll" -Destination 'S24-Packaged\llplugin' -Force
}

Write-Progress -Activity "Stage 5 Complete" -Completed
Write-Host "✓ Media plugins copied" -ForegroundColor Green

#endregion

#region Stage 6 - Copy Visual C++ Redistributables

Write-Stage "Stage 6: Copying runtime redistributables"

if (Test-Path 'newview\vc145\*') {
    Write-Host "  Visual C++ 2026 redistributables..." -ForegroundColor Cyan
    Copy-Item -Path 'newview\vc145\*' -Destination 'S24-Packaged' -Force
    Write-Host "✓ Redistributables copied" -ForegroundColor Green
}
else {
    Write-Host "  Warning: VC++ redistributables not found" -ForegroundColor Yellow
}

#endregion

#region Final Summary

$packageDuration = (Get-Date) - $packageStartTime

Write-Host ""
Write-Host ('═' * 70) -ForegroundColor Green
Write-Host "   ✓ PACKAGING COMPLETE" -ForegroundColor Green
Write-Host ('═' * 70) -ForegroundColor Green
Write-Host ""

if (Test-Path 'S24-Packaged') {
    $fileCount = (Get-ChildItem 'S24-Packaged' -Recurse -File).Count
    $totalSize = (Get-ChildItem 'S24-Packaged' -Recurse -File | Measure-Object -Property Length -Sum).Sum / 1MB
    
    Write-Host "Package Statistics:" -ForegroundColor Cyan
    Write-Host "  Files: $fileCount" -ForegroundColor White
    Write-Host "  Size: $([math]::Round($totalSize, 2)) MB" -ForegroundColor White
    Write-Host "  Duration: $([int]$packageDuration.TotalMinutes)m $($packageDuration.Seconds)s" -ForegroundColor White
    Write-Host "  Location: S24-Packaged\" -ForegroundColor White
    Write-Host ""
    Write-Host "Status: Ready for testing and deployment" -ForegroundColor Green
}

if (-not $NonInteractive) {
    Write-Host ""
    Read-Host -Prompt 'Packaging complete! Press Enter to exit'
}

#endregion
