#!/usr/bin/env pwsh
<#
.SYNOPSIS
    KV Viewer S24 - Build Environment Diagnostic Tool

.DESCRIPTION
    Comprehensive build environment checker for the KV autobuild system.
    Validates all required tools, libraries, and configurations before building.

.NOTES
    Copyright (c) 2025 Kirstenlee Cinquetti
    Part of KV Autobuild System

    Requirements: Visual Studio 2026, CMake, PowerShell 7+, vcpkg
#>

Clear-Host

Write-Host "═══════════════════════════════════════════════════════════════" -ForegroundColor Cyan
Write-Host "   KV Viewer S24 - Build Environment Diagnostic" -ForegroundColor Magenta
Write-Host "   Copyright (c) 2025 Kirstenlee Cinquetti" -ForegroundColor Yellow
Write-Host "═══════════════════════════════════════════════════════════════" -ForegroundColor Cyan

Write-Host "`n=== Build Environment Overview ===" -ForegroundColor Cyan

# Track missing components for summary
$missingComponents = @()

# Memory summary
$mem = Get-CimInstance Win32_OperatingSystem
$memFreeGB = [math]::Round($mem.FreePhysicalMemory / 1MB, 2)
$memTotalGB = [math]::Round($mem.TotalVisibleMemorySize / 1MB, 2)
Write-Host "Memory: $memFreeGB GB Free / $memTotalGB GB Total" -ForegroundColor Gray

# Process count
$procCount = (Get-Process).Count
Write-Host "Active Processes: $procCount" -ForegroundColor Gray

# PowerShell version
$psVer = $PSVersionTable.PSVersion.ToString()
Write-Host "PowerShell Version: $psVer" -ForegroundColor Gray

# .NET SDK info
$dotnetInfo = & dotnet --info 2>$null
$dotnetVersion = ($dotnetInfo | Select-String "Version:\s+([\d\.]+)").Matches.Groups[1].Value
if ($dotnetVersion) {
    Write-Host ".NET SDK Version: $dotnetVersion" -ForegroundColor Gray
} else {
    Write-Host ".NET SDK: Not found" -ForegroundColor DarkYellow
    $missingComponents += ".NET SDK"
}

# Try vswhere to locate MSBuild
$msbuildExe = $null
$vswherePath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vswherePath) {
    $msbuildExe = & $vswherePath -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe 2>$null
    if ($msbuildExe) {
        Write-Host "MSBuild Path: $msbuildExe" -ForegroundColor Gray
    } else {
        Write-Host "MSBuild not found via vswhere" -ForegroundColor DarkYellow
        $missingComponents += "MSBuild"
    }
} else {
    Write-Host "vswhere.exe not found fallback to manual path check" -ForegroundColor DarkYellow
    $missingComponents += "vswhere / Visual Studio Installer"
}

if ($msbuildExe) {
    $msbuildVersion = & "$msbuildExe" -version | Select-String "Version" | ForEach-Object { $_.Line }
    Write-Host "MSBuild Version: $msbuildVersion" -ForegroundColor Gray
}

# vcpkg detection
$vcpkgExe = $null
$vcpkgVersion = $null

# 1. Check VCPKG_ROOT environment variable
if ($env:VCPKG_ROOT -and (Test-Path (Join-Path $env:VCPKG_ROOT "vcpkg.exe"))) {
    $vcpkgExe = Join-Path $env:VCPKG_ROOT "vcpkg.exe"
}

# 2. Fallback: check PATH
if (-not $vcpkgExe) {
    $vcpkgExe = (Get-Command vcpkg -ErrorAction SilentlyContinue).Source
}

if ($vcpkgExe) {
    try {
        $vcpkgVersion = & $vcpkgExe version 2>$null | Select-String "vcpkg package management" | ForEach-Object { $_.Line.Trim() }
    } catch {
        $vcpkgVersion = "installed (version query failed)"
    }
    Write-Host "vcpkg Path: $vcpkgExe" -ForegroundColor Gray
    Write-Host "vcpkg Version: $vcpkgVersion" -ForegroundColor Gray

    if ($env:VCPKG_ROOT) {
        Write-Host "VCPKG_ROOT: $env:VCPKG_ROOT" -ForegroundColor Gray
    } else {
        Write-Host "VCPKG_ROOT: Not set (using PATH)" -ForegroundColor DarkYellow
    }
} else {
    Write-Host "vcpkg: Not found" -ForegroundColor DarkYellow
    Write-Host "  -> vcpkg is not in PATH and VCPKG_ROOT is not set or invalid." -ForegroundColor DarkYellow
    Write-Host "  -> Install: https://learn.microsoft.com/en-us/vcpkg/get_started/overview" -ForegroundColor DarkGray
    $missingComponents += "vcpkg"
}

# === Missing Components Summary ===
if ($missingComponents.Count -gt 0) {
    Write-Host ""
    Write-Host "==============================================================" -ForegroundColor Red
    Write-Host "  WARNING: Missing Build Components Detected!" -ForegroundColor Yellow
    Write-Host "==============================================================" -ForegroundColor Red
    foreach ($comp in $missingComponents) {
        Write-Host "  ? $comp" -ForegroundColor Yellow
    }
    Write-Host ""

    # Resolve path to the scripts directory (one level up from indra)
    $installScript = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\scripts\wininstall-deps.ps1"))
    if (Test-Path $installScript) {
        Write-Host "  Dependency installer found: $installScript" -ForegroundColor DarkGray
        Write-Host "[R] Run dependency installer to fix missing components" -ForegroundColor Green
        Write-Host "[C] Continue anyway (may cause build failures)" -ForegroundColor DarkYellow
        Write-Host "[X] Exit" -ForegroundColor Red
        $depChoice = Read-Host "Enter option (R, C, or X)"

        if ($depChoice -eq "R") {
            try {
                & $installScript -MissingComponents $missingComponents
                Write-Host "Dependency installer completed." -ForegroundColor Green
            } catch {
                Write-Host "Dependency installer failed: $($_.Exception.Message)" -ForegroundColor Red
                exit 1
            }
        } elseif ($depChoice -eq "X") {
            Write-Host "Exiting." -ForegroundColor Red
            exit 0
        } else {
            Write-Host "Continuing with missing components..." -ForegroundColor DarkYellow
        }
    } else {
        Write-Host "  Dependency installer not found: $installScript" -ForegroundColor DarkGray
        Write-Host "  Create this script to enable automated installs." -ForegroundColor DarkGray
        Write-Host ""
        Write-Host "[C] Continue anyway (may cause build failures)" -ForegroundColor DarkYellow
        Write-Host "[X] Exit" -ForegroundColor Red
        $depChoice = Read-Host "Enter option (C or X)"

        if ($depChoice -eq "X") {
            Write-Host "Exiting." -ForegroundColor Red
            exit 0
        } else {
            Write-Host "Continuing with missing components..." -ForegroundColor DarkYellow
        }
    }
}

Write-Host "===================================" -ForegroundColor Cyan
Pause

Write-Host "==============================================================" -ForegroundColor Cyan
Write-Host "  Welcome to the build! Let's get started :) KL" -ForegroundColor Green
Write-Host "==============================================================" -ForegroundColor Cyan

Pause  # Wait before proceeding

# originally Python develop script with CMake
powershell.exe -ExecutionPolicy Bypass -File Invoke-Configure.ps1
# Runs PS which invokes the CMake files with a generator for VS2026 and 64bit build

Write-Host "Pre-configuring done! Press a key to exit." -ForegroundColor Yellow

Pause  # Hold press a key to go on!

# === User-Defined Base Directory Adjust To Fit YOUR directory structure! ===
$BASE_PATH = "S:\Dev"
Write-Host "Base path is set to: $BASE_PATH" -ForegroundColor Magenta

Write-Host "===================================" -ForegroundColor Cyan
Write-Host "[1] Open Kirstens-S24 in Visual Studio (Manual Build)" -ForegroundColor Green
Write-Host "[2] Build Kirstens-S24 using MsBuild (Automated Build)" -ForegroundColor Yellow
Write-Host "===================================" -ForegroundColor Cyan

# User input choice
$choice = Read-Host "Enter option (1 or 2)"

if ($choice -eq "1") {
    Write-Host "Opening Kirstens-S24 in Visual Studio..." -ForegroundColor Green
    Start-Process "$BASE_PATH\Kirstensviewer\indra\build-vc145\Kirstens-S24.slnx"
}
elseif ($choice -eq "2") {
    Write-Host "Starting Automated Build via MsBuild..." -ForegroundColor Yellow
    $msbuild = if ($msbuildExe) { $msbuildExe } else { "C:\Program Files\Microsoft Visual Studio\2026\Professional\MSBuild\Current\Bin\MSBuild.exe" }


    $msbuildProcess = Start-Process $msbuild `
        -ArgumentList "`"$BASE_PATH\Kirstensviewer\indra\build-vc145\Kirstens-S24.slnx`" /p:PlatformToolset=v145 /p:Configuration=Release /p:Platform=x64" `
        -NoNewWindow -PassThru

    $msbuildProcess.WaitForExit()

    if ($msbuildProcess.ExitCode -ne 0) {
        Write-Host "INFO: exit code = $($msbuildProcess.ExitCode)." -ForegroundColor Cyan
    }
    else {
        Write-Host "Build completed successfully!" -ForegroundColor Green
    }
}
else {
    Write-Host "Invalid option. No action taken." -ForegroundColor Red
}

Write-Host "===================================" -ForegroundColor Cyan
Write-Host "  Package Build Option!" -ForegroundColor Green
Write-Host "===================================" -ForegroundColor Cyan

Write-Host "[1] Proceed to packaging" -ForegroundColor Yellow
Write-Host "[2] Exit without packaging" -ForegroundColor Magenta

$packageChoice = Read-Host "Enter option (1 or 2)"

if ($packageChoice -eq "1") {
    Write-Host "Continuing to packaging step..." -ForegroundColor Green

    # Call external packaging script
    $packScript = Join-Path $PSScriptRoot "Invoke-Package.ps1"
    if (Test-Path $packScript) {
        try {
            & $packScript
            Write-Host "Packaging completed successfully." -ForegroundColor Green
        } catch {
            Write-Host "Packaging failed: $($_.Exception.Message)" -ForegroundColor Red
            Pause
            exit 1
        }
    } else {
        Write-Host "Packaging script not found: $packScript" -ForegroundColor Red
        Pause
        exit 1
    }
} else {
    Write-Host "Packaging skipped." -ForegroundColor Yellow
}

# End of package routine

Write-Host "===================================" -ForegroundColor Cyan
Write-Host "Packaging complete. What do you want to do next?" -ForegroundColor Yellow
Write-Host "===================================" -ForegroundColor Cyan

Write-Host "[1] Launch the Viewer" -ForegroundColor Green #runs the viewer in its packaged directory
Write-Host "[2] Exit" -ForegroundColor Red
Write-Host "[3] Purge build back to pre-compile state" -ForegroundColor Magenta #cleans the build back to zero!

$choice = Read-Host "Enter 1 to launch, 2 to exit, or 3 to purge build"

if ($choice -eq "1") {
    Write-Host "Launching Kirstens-S24 Viewer..." -ForegroundColor Cyan
    Start-Process "S24-Packaged\Kirstens-S24.exe"
}
elseif ($choice -eq "2") {
    Write-Host "Exiting..." -ForegroundColor Red
    Exit
}
elseif ($choice -eq "3") {
    Write-Host "Purging build back to pre-compile state..." -ForegroundColor Magenta

    Write-Host "==============================================================" -ForegroundColor Red
    Write-Host "  WARNING: This will PURGE ALL files and folders to a pre-build state!" -ForegroundColor Yellow
    Write-Host "==============================================================" -ForegroundColor Red

    Pause  # Last chance to bail

    $cleanScript = Join-Path $PSScriptRoot "Invoke-Clean.ps1"
    if (Test-Path $cleanScript) {
        try {
            & $cleanScript
            Write-Host "Build successfully purged. Ready for fresh compile!" -ForegroundColor Green
        } catch {
            Write-Host "Cleanup failed: $($_.Exception.Message)" -ForegroundColor Red
            exit 1
        }
    } else {
        Write-Host "Cleanup script not found: $cleanScript" -ForegroundColor Red
        exit 1
    }
}
else {
    Write-Host "Invalid option. No action taken." -ForegroundColor Red
}