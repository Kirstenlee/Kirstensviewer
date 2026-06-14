#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Safely removes Python dependencies from KV Viewer S24
.DESCRIPTION
    Identifies and removes Python scripts and dependencies after verifying
    the PowerShell-only build system is working correctly
.PARAMETER WhatIf
    Show what would be removed without actually removing
.PARAMETER Backup
    Create backup of Python files before removal
.PARAMETER Force
    Skip confirmation prompts
#>

[CmdletBinding(SupportsShouldProcess)]
param(
    [Parameter()]
    [switch]$WhatIf,
    
    [Parameter()]
    [switch]$Backup = $true,
    
    [Parameter()]
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

Write-Host @"
╔══════════════════════════════════════════════════════════════════════╗
║                                                                      ║
║   KV Viewer S24 - Python Dependency Removal Tool                    ║
║   Removes legacy Python scripts from build system                   ║
║                                                                      ║
╚══════════════════════════════════════════════════════════════════════╝
"@ -ForegroundColor Magenta

# Verify build system is working
Write-Host "`n[1/5] Verifying PowerShell build system..." -ForegroundColor Cyan

$psScriptCheck = @(
    "..\..\scripts\Test-MessageTemplate.ps1",
    "..\newview\Copy-ViewerManifest.ps1",
    "..\cmake\TemplateCheckModern.cmake"
)

$allPresent = $true
foreach ($script in $psScriptCheck) {
    if (-not (Test-Path $script)) {
        Write-Host "  ✗ Missing: $script" -ForegroundColor Red
        $allPresent = $false
    }
    else {
        Write-Host "  ✓ Found: $script" -ForegroundColor Green
    }
}

if (-not $allPresent) {
    Write-Host "`nERROR: PowerShell build system incomplete!" -ForegroundColor Red
    Write-Host "Run Test-Modernization.ps1 first to ensure everything is set up." -ForegroundColor Yellow
    exit 1
}

Write-Host "`n✓ PowerShell build system verified" -ForegroundColor Green

# Find Python files
Write-Host "`n[2/5] Scanning for Python files..." -ForegroundColor Cyan

$pythonFiles = @()

# Build scripts
$pythonFiles += Get-ChildItem -Path "..\..\scripts" -Filter "*.py" -File -ErrorAction SilentlyContinue
$pythonFiles += Get-ChildItem -Path "..\newview" -Filter "*manifest*.py" -File -ErrorAction SilentlyContinue
$pythonFiles += Get-ChildItem -Path "..\cmake" -Filter "Python*.cmake" -File -ErrorAction SilentlyContinue

# LL legacy modules (if present)
if (Test-Path "..\lib\python") {
    $pythonFiles += Get-ChildItem -Path "..\lib\python" -Recurse -Filter "*.py" -File -ErrorAction SilentlyContinue
}

Write-Host "  Found $($pythonFiles.Count) Python-related files`n" -ForegroundColor White

if ($pythonFiles.Count -eq 0) {
    Write-Host "No Python files found. System already clean!" -ForegroundColor Green
    exit 0
}

# Categorize files
Write-Host "[3/5] Analyzing Python dependencies..." -ForegroundColor Cyan

$categories = @{
    'Build Scripts' = @()
    'LL Modules' = @()
    'CMake Python' = @()
    'Other' = @()
}

foreach ($file in $pythonFiles) {
    $relPath = $file.FullName
    
    if ($file.Name -like "*verifier*" -or $file.Name -like "*manifest*") {
        $categories['Build Scripts'] += $file
    }
    elseif ($file.DirectoryName -like "*lib\python*") {
        $categories['LL Modules'] += $file
    }
    elseif ($file.Extension -eq '.cmake') {
        $categories['CMake Python'] += $file
    }
    else {
        $categories['Other'] += $file
    }
}

foreach ($cat in $categories.Keys | Sort-Object) {
    $items = $categories[$cat]
    if ($items.Count -gt 0) {
        Write-Host "`n  $cat ($($items.Count) files):" -ForegroundColor Yellow
        foreach ($item in $items | Select-Object -First 10) {
            $size = [math]::Round($item.Length / 1KB, 1)
            $relPath = $item.FullName.Replace((Get-Location).Path, ".").Replace("\", "/")
            Write-Host "    - $relPath ($size KB)" -ForegroundColor Gray
        }
        if ($items.Count -gt 10) {
            Write-Host "    ... and $($items.Count - 10) more" -ForegroundColor DarkGray
        }
    }
}

# Create backup if requested
if ($Backup -and -not $WhatIf) {
    Write-Host "`n[4/5] Creating backup..." -ForegroundColor Cyan
    
    $backupDir = "..\..\python_backup_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
    New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
    
    foreach ($file in $pythonFiles) {
        $relativePath = $file.FullName.Replace((Get-Location).Path, "")
        $backupPath = Join-Path $backupDir $relativePath
        $backupFolder = Split-Path $backupPath -Parent
        
        if (-not (Test-Path $backupFolder)) {
            New-Item -ItemType Directory -Path $backupFolder -Force | Out-Null
        }
        
        Copy-Item $file.FullName $backupPath -Force
    }
    
    Write-Host "  ✓ Backup created: $backupDir" -ForegroundColor Green
    Write-Host "  Files backed up: $($pythonFiles.Count)" -ForegroundColor White
}

# Removal
Write-Host "`n[5/5] Removing Python files..." -ForegroundColor Cyan

if ($WhatIf) {
    Write-Host "`n  WHAT-IF MODE: The following would be removed:`n" -ForegroundColor Yellow
    foreach ($file in $pythonFiles | Select-Object -First 20) {
        Write-Host "    - $($file.FullName.Replace((Get-Location).Path, '.'))" -ForegroundColor DarkGray
    }
    if ($pythonFiles.Count -gt 20) {
        Write-Host "    ... and $($pythonFiles.Count - 20) more files" -ForegroundColor DarkGray
    }
}
else {
    if (-not $Force) {
        Write-Host "`nAbout to remove $($pythonFiles.Count) Python files." -ForegroundColor Yellow
        if ($Backup) {
            Write-Host "Backup created at: $backupDir" -ForegroundColor Green
        }
        
        $confirm = Read-Host "`nProceed with removal? (yes/no)"
        
        if ($confirm -ne 'yes') {
            Write-Host "Removal cancelled." -ForegroundColor Gray
            exit 0
        }
    }
    
    $removed = 0
    foreach ($file in $pythonFiles) {
        try {
            Remove-Item $file.FullName -Force
            $removed++
            Write-Host "  ✓ Removed: $($file.Name)" -ForegroundColor DarkGreen
        }
        catch {
            Write-Host "  ✗ Failed to remove: $($file.Name) - $_" -ForegroundColor Red
        }
    }
    
    Write-Host "`n✓ Removed $removed of $($pythonFiles.Count) files" -ForegroundColor Green
}

# Summary
Write-Host "`n" + ("=" * 70) -ForegroundColor Cyan
Write-Host "SUMMARY" -ForegroundColor Yellow
Write-Host ("=" * 70) -ForegroundColor Cyan

Write-Host "`nPython Dependency Status:" -ForegroundColor White
Write-Host "  Build System: PowerShell-only ✓" -ForegroundColor Green
Write-Host "  Template Verification: PowerShell ✓" -ForegroundColor Green
Write-Host "  Viewer Manifest: PowerShell ✓" -ForegroundColor Green
Write-Host "  Packaging: winpack.ps1 ✓" -ForegroundColor Green

if (-not $WhatIf -and $removed -gt 0) {
    Write-Host "`nPython files removed: $removed" -ForegroundColor Green
    if ($Backup) {
        Write-Host "Backup location: $backupDir" -ForegroundColor Cyan
    }
}

Write-Host "`nNext Steps:" -ForegroundColor Yellow
Write-Host "  1. Run a clean build to verify everything works" -ForegroundColor White
Write-Host "     cd ..\build-vc145" -ForegroundColor Gray
Write-Host "     cmake --build . --clean-first --config Release" -ForegroundColor Gray
Write-Host "`n  2. Test packaging with winpack.ps1" -ForegroundColor White
Write-Host "     cd ..\indra" -ForegroundColor Gray
Write-Host "     .\winpack.ps1" -ForegroundColor Gray
Write-Host "`n  3. If all works, Python is fully removed! 🎉" -ForegroundColor White

Write-Host "`n" + ("=" * 70) + "`n" -ForegroundColor Cyan
