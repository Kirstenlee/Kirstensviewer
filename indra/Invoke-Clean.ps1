#!/usr/bin/env pwsh
<#
.SYNOPSIS
    KV Viewer S24 - Build Cleanup Utility

.DESCRIPTION
    Removes all build artifacts, temporary files, and generated directories
    to ensure a clean build environment.

.PARAMETER NonInteractive
    Run without user prompts

.PARAMETER CleanCMakeCache
    Clean CMake cache files from the build folder

.PARAMETER PurgeTarballCache
    Purge the tarball download cache

.PARAMETER PurgeVcpkgCache
    Purge vcpkg cache folders

.PARAMETER PurgeAllCaches
    Purge all cache folders (tarball and vcpkg)

.EXAMPLE
    .\Invoke-Clean.ps1
    Clean build with confirmation prompt

.EXAMPLE
    .\Invoke-Clean.ps1 -NonInteractive
    Clean build without prompts (for automation)

.EXAMPLE
    .\Invoke-Clean.ps1 -CleanCMakeCache
    Clean build and remove CMake cache

.EXAMPLE
    .\Invoke-Clean.ps1 -PurgeAllCaches
    Clean build and purge all caches

.NOTES
    Copyright (c) 2025 Kirstenlee Cinquetti
    Part of KV Autobuild System

#>

[CmdletBinding()]
param(
    [Parameter()]
    [switch]$NonInteractive,

    [Parameter()]
    [switch]$CleanCMakeCache,

    [Parameter()]
    [switch]$PurgeTarballCache,

    [Parameter()]
    [switch]$PurgeVcpkgCache,

    [Parameter()]
    [switch]$PurgeAllCaches
)

Clear-Host

Write-Host "═══════════════════════════════════════════════════════════════" -ForegroundColor Cyan
Write-Host "   KV Viewer S24 - Build Cleanup Utility" -ForegroundColor Magenta
Write-Host "   Copyright (c) 2025 Kirstenlee Cinquetti" -ForegroundColor Yellow
Write-Host "═══════════════════════════════════════════════════════════════" -ForegroundColor Cyan
Write-Host ""

# ============================================================================
# STEP 1: Determine what to clean (ask user or use parameters)
# ============================================================================

$shouldCleanBuild = $false
$shouldCleanCMake = $CleanCMakeCache
$shouldPurgeTarball = $PurgeTarballCache
$shouldPurgeVcpkg = $PurgeVcpkgCache

if (-not $NonInteractive) {
    Write-Host "What would you like to clean?" -ForegroundColor Yellow
    Write-Host ""
    
    # Ask about build folders
    $response = Read-Host "Clean build folders? (Y/n)"
    if ($response -ne 'n' -and $response -ne 'N') {
        $shouldCleanBuild = $true
    }
    
    # Ask about CMake cache (only if not cleaning all builds)
    if (-not $shouldCleanBuild -and -not $CleanCMakeCache) {
        $response = Read-Host "Clean CMake cache only (preserves build folder)? (y/N)"
        if ($response -eq 'y' -or $response -eq 'Y') {
            $shouldCleanCMake = $true
        }
    } elseif ($shouldCleanBuild -and -not $CleanCMakeCache) {
        $response = Read-Host "Also clean CMake cache? (y/N)"
        if ($response -eq 'y' -or $response -eq 'Y') {
            $shouldCleanCMake = $true
        }
    }
    
    # Ask about tarball cache
    if (-not $PurgeTarballCache -and -not $PurgeAllCaches) {
        $response = Read-Host "Purge tarball cache (.temp-downloads)? (y/N)"
        if ($response -eq 'y' -or $response -eq 'Y') {
            $shouldPurgeTarball = $true
        }
    }
    
    # Ask about vcpkg cache with WARNING
    if (-not $PurgeVcpkgCache -and -not $PurgeAllCaches) {
        Write-Host ""
        Write-Host "WARNING: Purging vcpkg cache will require a full rebuild (20-30 minutes)!" -ForegroundColor Red
        $response = Read-Host "Purge vcpkg cache? (y/N)"
        if ($response -eq 'y' -or $response -eq 'Y') {
            $shouldPurgeVcpkg = $true
        }
    }
    
    # Handle -PurgeAllCaches parameter
    if ($PurgeAllCaches) {
        $shouldPurgeTarball = $true
        $shouldPurgeVcpkg = $true
    }
    
    Write-Host ""
    Write-Host "───────────────────────────" -ForegroundColor Cyan
    Write-Host "Cleanup Summary:" -ForegroundColor Yellow
    Write-Host "  Build Folders: " -NoNewline; Write-Host $(if ($shouldCleanBuild) { "YES" } else { "NO" }) -ForegroundColor $(if ($shouldCleanBuild) { "Green" } else { "Gray" })
    Write-Host "  CMake Cache:   " -NoNewline; Write-Host $(if ($shouldCleanCMake) { "YES" } else { "NO" }) -ForegroundColor $(if ($shouldCleanCMake) { "Green" } else { "Gray" })
    Write-Host "  Tarball Cache: " -NoNewline; Write-Host $(if ($shouldPurgeTarball) { "YES" } else { "NO" }) -ForegroundColor $(if ($shouldPurgeTarball) { "Green" } else { "Gray" })
    Write-Host "  Vcpkg Cache:   " -NoNewline; Write-Host $(if ($shouldPurgeVcpkg) { "YES" } else { "NO" }) -ForegroundColor $(if ($shouldPurgeVcpkg) { "Red" } else { "Gray" })
    Write-Host "───────────────────────────" -ForegroundColor Cyan
    Write-Host ""
    
    if (-not $shouldCleanBuild -and -not $shouldCleanCMake -and -not $shouldPurgeTarball -and -not $shouldPurgeVcpkg) {
        Write-Host "No cleanup actions selected. Exiting." -ForegroundColor Yellow
        Pause
        Exit
    }
    
    $response = Read-Host "Proceed with cleanup? (y/N)"
    if ($response -ne 'y' -and $response -ne 'Y') {
        Write-Host "Cleanup cancelled." -ForegroundColor Yellow
        Exit
    }
    
    Write-Host ""
    Write-Host "Starting cleanup..." -ForegroundColor Green
    Write-Host ""
} else {
    # Non-interactive mode: default to cleaning build if no specific options provided
    if (-not $CleanCMakeCache -and -not $PurgeTarballCache -and -not $PurgeVcpkgCache -and -not $PurgeAllCaches) {
        $shouldCleanBuild = $true
    }
    if ($PurgeAllCaches) {
        $shouldPurgeTarball = $true
        $shouldPurgeVcpkg = $true
    }
}

# ============================================================================
# STEP 2: Execute cleanup actions based on user choices
# ============================================================================

# Find build folder for CMake cache cleaning (if needed)
$buildFolderToPreserve = $null
if ($shouldCleanCMake -and -not $shouldCleanBuild) {
    # Look for build-vc145* or build-vc143* folder
    $buildFolderCandidates = Get-ChildItem -Path "." -Directory -ErrorAction SilentlyContinue |
                             Where-Object { $_.Name -like "build-vc145*" -or $_.Name -like "build-vc143*" } |
                             Select-Object -First 1
    if ($buildFolderCandidates) {
        $buildFolderToPreserve = $buildFolderCandidates.FullName
        Write-Host "Found build folder to preserve: $buildFolderToPreserve" -ForegroundColor Cyan
    } else {
        Write-Host "No build folder found for CMake cache cleaning." -ForegroundColor Yellow
        $shouldCleanCMake = $false
    }
}

# Clean build folders if requested
if ($shouldCleanBuild) {
    # Remove main build directory
    $mainBuildDir = "..\slviewer-all-platforms-libs-viewer-S24"
    if (Test-Path $mainBuildDir) {
        Write-Host "Removing main build directory: $mainBuildDir" -ForegroundColor Cyan
        Remove-Item $mainBuildDir -Recurse -Force
    }

    Write-Host ""
    Write-Host "============================" -ForegroundColor Cyan
    Write-Host "Removing build folders..." -ForegroundColor Cyan
    Write-Host "============================" -ForegroundColor Cyan

    # List of folders to remove (base list)
    $foldersToRemove = @(
        "..\lib", "..\xui", "..\fonts", "..\js", "..\include", "..\bin",
        "..\resources", "..\dictionaries", "..\LICENSES", "..\docs",
        "S24-Packaged", "..\meta"
    )

    # Also search recursively for any folders matching build-vc143* or build-vc145*
    $searchPatterns = @("build-vc143*", "build-vc145*")
    foreach ($root in @(".", "..")) {
        foreach ($pattern in $searchPatterns) {
            $matchedDirs = Get-ChildItem -Path $root -Directory -Recurse -ErrorAction SilentlyContinue |
                           Where-Object { $_.Name -like $pattern }
            foreach ($m in $matchedDirs) {
                if (-not ($foldersToRemove -contains $m.FullName)) {
                    $foldersToRemove += $m.FullName
                }
            }
        }
    }

    # Ensure unique entries
    $foldersToRemove = $foldersToRemove | Select-Object -Unique

    $total = $foldersToRemove.Count
    $barLength = 30

    function Show-ProgressBar($current, $total) {
        $percent = [math]::Round(($current / $total) * 100)
        $filled = [math]::Round(($current / $total) * $barLength)
        $empty = $barLength - $filled
        $bar = ('#' * $filled) + ('-' * $empty)
        Write-Host ("[$bar] $percent%") -ForegroundColor White
    }

    $counter = 0
    foreach ($folder in $foldersToRemove) {
        $counter++
        Show-ProgressBar $counter $total
        Start-Sleep -Milliseconds 150
        try {
            if (Test-Path $folder) {
                Remove-Item -LiteralPath $folder -Recurse -Force -ErrorAction Stop
                Write-Host "✓ Removed: $folder" -ForegroundColor Green
            } else {
                Write-Host "○ Not found: $folder" -ForegroundColor DarkGray
            }
        } catch {
            Write-Host "✗ Failed to remove: $folder - $($_.Exception.Message)" -ForegroundColor Yellow
        }
    }

    Write-Host ""
    Write-Host "============================" -ForegroundColor Cyan
    Write-Host "Removed build libs and includes folders." -ForegroundColor Cyan
    Write-Host "============================" -ForegroundColor Cyan

    # Stray file cleanup
    Write-Host ""
    Write-Host "============================" -ForegroundColor Cyan
    Write-Host "Removing stray build files..." -ForegroundColor Cyan
    Write-Host "============================" -ForegroundColor Cyan

    $filesToRemove = @(
        "S:\Dev\S24\installed-packages.json"
    )

    foreach ($file in $filesToRemove) {
        try {
            if (Test-Path -LiteralPath $file) {
                Remove-Item -LiteralPath $file -Force -ErrorAction Stop
                Write-Host "✓ Removed: $file" -ForegroundColor Green
            } else {
                Write-Host "○ Not found: $file" -ForegroundColor DarkGray
            }
        } catch {
            Write-Host "✗ Failed to remove: $file - $($_.Exception.Message)" -ForegroundColor Yellow
        }
    }
}

# Clean CMake cache if requested
if ($shouldCleanCMake) {
    Write-Host ""
    Write-Host "============================" -ForegroundColor Cyan
    Write-Host "Cleaning CMake cache..." -ForegroundColor Cyan
    Write-Host "============================" -ForegroundColor Cyan

    if ($buildFolderToPreserve) {
        $cmakeCacheFiles = @(
            "$buildFolderToPreserve\CMakeCache.txt",
            "$buildFolderToPreserve\cmake_install.cmake"
        )

        $cmakeFolders = @(
            "$buildFolderToPreserve\CMakeFiles"
        )

        foreach ($file in $cmakeCacheFiles) {
            try {
                if (Test-Path -LiteralPath $file) {
                    Remove-Item -LiteralPath $file -Force -ErrorAction Stop
                    Write-Host "✓ Removed: $file" -ForegroundColor Green
                }
            } catch {
                Write-Host "✗ Failed to remove: $file - $($_.Exception.Message)" -ForegroundColor Yellow
            }
        }

        foreach ($folder in $cmakeFolders) {
            try {
                if (Test-Path -LiteralPath $folder) {
                    Remove-Item -LiteralPath $folder -Recurse -Force -ErrorAction Stop
                    Write-Host "✓ Removed: $folder" -ForegroundColor Green
                }
            } catch {
                Write-Host "✗ Failed to remove: $folder - $($_.Exception.Message)" -ForegroundColor Yellow
            }
        }
    } else {
        Write-Host "○ No build folder specified, skipping CMake cache cleanup." -ForegroundColor Yellow
    }
}

# Purge tarball cache if requested
if ($shouldPurgeTarball) {
    Write-Host ""
    Write-Host "============================" -ForegroundColor Cyan
    Write-Host "Purging tarball cache..." -ForegroundColor Cyan
    Write-Host "============================" -ForegroundColor Cyan

    $tarballCache = "S:\Dev\S24\.temp-downloads"
    try {
        if (Test-Path -LiteralPath $tarballCache) {
            Remove-Item -LiteralPath $tarballCache -Recurse -Force -ErrorAction Stop
            Write-Host "✓ Purged: $tarballCache" -ForegroundColor Green
        } else {
            Write-Host "○ Not found: $tarballCache" -ForegroundColor DarkGray
        }
    } catch {
        Write-Host "✗ Failed to purge: $tarballCache - $($_.Exception.Message)" -ForegroundColor Yellow
    }
}

# Purge vcpkg cache if requested (CAREFUL - expensive rebuild!)
if ($shouldPurgeVcpkg) {
    Write-Host ""
    Write-Host "============================" -ForegroundColor Red
    Write-Host "Purging vcpkg cache folders..." -ForegroundColor Red
    Write-Host "============================" -ForegroundColor Red
    Write-Host "WARNING: This will require a full rebuild (20-30 minutes)!" -ForegroundColor Yellow
    Write-Host ""

    $vcpkgCacheFolders = @(
        "S:\Dev\S24\.package-cache",
        "S:\Dev\S24\.vcpkg-packages",
        "S:\Dev\S24\.vcpkg-repo",
        "S:\Dev\S24\.vcpkg-temp"
    )

    foreach ($cacheFolder in $vcpkgCacheFolders) {
        try {
            if (Test-Path -LiteralPath $cacheFolder) {
                Remove-Item -LiteralPath $cacheFolder -Recurse -Force -ErrorAction Stop
                Write-Host "✓ Purged: $cacheFolder" -ForegroundColor Green
            } else {
                Write-Host "○ Not found: $cacheFolder" -ForegroundColor DarkGray
            }
        } catch {
            Write-Host "✗ Failed to purge: $cacheFolder - $($_.Exception.Message)" -ForegroundColor Yellow
        }
    }
}

Write-Host ""
Write-Host "============================" -ForegroundColor Cyan
Write-Host "Cleanup completed! S24 is now cleared to a prebuild state." -ForegroundColor Green
Write-Host "============================" -ForegroundColor Cyan

Pause
Exit
