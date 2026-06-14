#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Complete Build-to-Package workflow for KV Viewer S24
    
.DESCRIPTION
    PowerShell-only workflow: Build → Package → Verify
    Professional autobuild orchestrator for the KV autobuild system
    
.PARAMETER Configuration
    Build configuration (Release, Debug, RelWithDebInfo)
    
.PARAMETER CleanBuild
    Perform clean build
    
.PARAMETER SkipBuild
    Skip build step (package only)
    
.PARAMETER SkipPackage
    Skip packaging step (build only)
    
.PARAMETER SkipVerify
    Skip verification step
    
.PARAMETER NonInteractive
    Run without user prompts (for CI/CD)
    
.EXAMPLE
    .\Invoke-ViewerManifest.ps1
    Run complete build-to-package workflow
    
.EXAMPLE
    .\Invoke-ViewerManifest.ps1 -Configuration Release -CleanBuild
    Clean build and package in Release mode
    
.EXAMPLE
    .\Invoke-ViewerManifest.ps1 -SkipBuild
    Package only (skip build step)
    
.NOTES
    Copyright (c) 2025 Kirstens Viewer S24
    Part of KV Autobuild System
#>

[CmdletBinding()]
param(
    [Parameter()]
    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
    [string]$Configuration = 'Release',
    
    [Parameter()]
    [switch]$CleanBuild,
    
    [Parameter()]
    [switch]$SkipBuild,
    
    [Parameter()]
    [switch]$SkipPackage,
    
    [Parameter()]
    [switch]$SkipVerify,
    
    [Parameter()]
    [switch]$NonInteractive
)

$ErrorActionPreference = 'Stop'
$startTime = Get-Date

function Write-Step {
    param([string]$Message)
    Write-Host "`n$('═' * 75)" -ForegroundColor Cyan
    Write-Host "  $Message" -ForegroundColor Yellow
    Write-Host "$('═' * 75)`n" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "✓ $Message" -ForegroundColor Green
}

function Write-Info {
    param([string]$Message)
    Write-Host "  $Message" -ForegroundColor White
}

function Write-Warning {
    param([string]$Message)
    Write-Host "⚠ $Message" -ForegroundColor Yellow
}

function Write-Error {
    param([string]$Message)
    Write-Host "✗ $Message" -ForegroundColor Red
}

Clear-Host

Write-Host @"
╔═══════════════════════════════════════════════════════════════════════╗
║                                                                       ║
║   KV Viewer S24 - Build Manifest & Package Workflow                  ║
║   Professional PowerShell Autobuild System                            ║
║                                                                       ║
╚═══════════════════════════════════════════════════════════════════════╝
"@ -ForegroundColor Magenta

Write-Host "`nConfiguration: $Configuration" -ForegroundColor Cyan
Write-Host "Started: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')`n" -ForegroundColor Gray

#region Pre-Flight Checks

Write-Step "Pre-Flight Checks"

# Check PowerShell version
if ($PSVersionTable.PSVersion.Major -lt 7) {
    Write-Error "PowerShell 7+ required (you have $($PSVersionTable.PSVersion))"
    Write-Info "Install: winget install Microsoft.PowerShell"
    exit 1
}
Write-Success "PowerShell 7+ detected ($($PSVersionTable.PSVersion))"

# Check for required directories
$requiredDirs = @(
    @{ Path = "build-vc145"; Name = "Build directory" },
    @{ Path = "newview"; Name = "Source directory" }
)

foreach ($dir in $requiredDirs) {
    if (Test-Path $dir.Path) {
        Write-Success "$($dir.Name) found"
    }
    else {
        Write-Error "$($dir.Name) not found: $($dir.Path)"
        exit 1
    }
}

# Check for packaging script
if (Test-Path "Invoke-Package.ps1") {
    Write-Success "Package script found"
}
else {
    Write-Error "Package script not found: Invoke-Package.ps1"
    exit 1
}

Write-Info "`nAll pre-flight checks passed!`n"

#endregion

#region Build Step

if (-not $SkipBuild) {
    Write-Step "Step 1: Building KV Viewer S24"
    
    Push-Location "build-vc145"
    
    try {
        Write-Info "Build directory: $(Get-Location)"
        Write-Info "Configuration: $Configuration"
        
        if ($CleanBuild) {
            Write-Info "Performing clean build...`n"
            
            $buildArgs = @(
                '--build', '.',
                '--clean-first',
                '--config', $Configuration,
                '--', '/maxcpucount', '/verbosity:minimal'
            )
        }
        else {
            Write-Info "Performing incremental build...`n"
            
            $buildArgs = @(
                '--build', '.',
                '--config', $Configuration,
                '--', '/maxcpucount', '/verbosity:minimal'
            )
        }
        
        $buildStart = Get-Date
        
        # Run CMake build
        & cmake @buildArgs
        
        if ($LASTEXITCODE -ne 0) {
            Write-Error "Build failed with exit code $LASTEXITCODE"
            Pop-Location
            exit 1
        }
        
        $buildDuration = (Get-Date) - $buildStart
        
        Write-Success "Build completed in $([int]$buildDuration.TotalMinutes)m $($buildDuration.Seconds)s"
        
        # Verify output
        $viewerExe = "newview\$Configuration\Kirstens-S24.exe"
        if (Test-Path $viewerExe) {
            $exeSize = (Get-Item $viewerExe).Length / 1MB
            Write-Success "Viewer executable created: $([math]::Round($exeSize, 2)) MB"
        }
        else {
            Write-Error "Viewer executable not found: $viewerExe"
            Pop-Location
            exit 1
        }
        
    }
    finally {
        Pop-Location
    }
}
else {
    Write-Step "Step 1: Build (SKIPPED)"
}

#endregion

#region Package Step

if (-not $SkipPackage) {
    Write-Step "Step 2: Packaging Distribution"
    
    Write-Info "Running packaging script...`n"
    
    $packageStart = Get-Date
    
    # Run Invoke-Package.ps1
    if ($NonInteractive) {
        & .\Invoke-Package.ps1 -NonInteractive
    }
    else {
        & .\Invoke-Package.ps1
    }
    
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Packaging failed with exit code $LASTEXITCODE"
        exit 1
    }
    
    $packageDuration = (Get-Date) - $packageStart
    
    Write-Success "Packaging completed in $([int]$packageDuration.TotalMinutes)m $($packageDuration.Seconds)s"
    
    # Verify package
    if (Test-Path "S24-Packaged") {
        $packageFiles = (Get-ChildItem "S24-Packaged" -Recurse -File).Count
        $packageSize = (Get-ChildItem "S24-Packaged" -Recurse -File | Measure-Object -Property Length -Sum).Sum / 1MB
        
        Write-Success "Package created: $packageFiles files, $([math]::Round($packageSize, 2)) MB total"
    }
    else {
        Write-Error "Package directory not created"
        exit 1
    }
}
else {
    Write-Step "Step 2: Package (SKIPPED)"
}

#endregion

#region Verification Step

if (-not $SkipVerify) {
    Write-Step "Step 3: Verification"
    
    $verifyErrors = @()
    
    # Check essential files in package
    $essentialFiles = @(
        'S24-Packaged\Kirstens-S24.exe',
        'S24-Packaged\SLPlugin.exe',
        'S24-Packaged\app_settings\message_template.msg',
        'S24-Packaged\app_settings\message.xml',
        'S24-Packaged\app_settings\settings.xml'
    )
    
    Write-Info "Checking essential files:"
    foreach ($file in $essentialFiles) {
        if (Test-Path $file) {
            $size = (Get-Item $file).Length / 1KB
            Write-Success "  $($file.Replace('S24-Packaged\', '')) ($([math]::Round($size, 1)) KB)"
        }
        else {
            Write-Error "  $($file.Replace('S24-Packaged\', '')) - MISSING"
            $verifyErrors += "Missing file: $file"
        }
    }
    
    # Check for legacy Python dependencies
    Write-Info "`nChecking for legacy dependencies:"
    $pythonFiles = Get-ChildItem "S24-Packaged" -Recurse -Filter "*.py" -File -ErrorAction SilentlyContinue
    
    if ($pythonFiles) {
        Write-Warning "  Found Python files in package"
        $pythonFiles | ForEach-Object {
            Write-Host "    - $($_.FullName.Replace((Get-Location).Path, '.'))" -ForegroundColor Yellow
        }
        $verifyErrors += "Python files found in package"
    }
    else {
        Write-Success "  No Python files found (clean package!)"
    }
    
    # Summary
    if ($verifyErrors.Count -eq 0) {
        Write-Info "`n"
        Write-Success "All verification checks passed!"
    }
    else {
        Write-Info "`n"
        Write-Error "Verification found $($verifyErrors.Count) issue(s):"
        foreach ($error in $verifyErrors) {
            Write-Host "  • $error" -ForegroundColor Red
        }
        exit 1
    }
}
else {
    Write-Step "Step 3: Verification (SKIPPED)"
}

#endregion

#region Final Summary

$totalDuration = (Get-Date) - $startTime

Write-Host "`n"
Write-Host "╔═══════════════════════════════════════════════════════════════════════╗" -ForegroundColor Green
Write-Host "║                                                                       ║" -ForegroundColor Green
Write-Host "║   ✓ BUILD MANIFEST COMPLETE                                           ║" -ForegroundColor Green
Write-Host "║                                                                       ║" -ForegroundColor Green
Write-Host "╚═══════════════════════════════════════════════════════════════════════╝" -ForegroundColor Green

Write-Host "`nWorkflow Summary:" -ForegroundColor Cyan
Write-Host "  Total Duration: $([int]$totalDuration.TotalMinutes)m $($totalDuration.Seconds)s" -ForegroundColor White
Write-Host "  Configuration: $Configuration" -ForegroundColor White
Write-Host "  Output: S24-Packaged\" -ForegroundColor White
Write-Host "  Status: Ready for deployment" -ForegroundColor Green

if (Test-Path "S24-Packaged\Kirstens-S24.exe") {
    $exePath = Resolve-Path "S24-Packaged\Kirstens-S24.exe"
    Write-Host "`nTo launch viewer:" -ForegroundColor Cyan
    Write-Host "  $exePath" -ForegroundColor Gray
}

Write-Host "`nCompleted: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Gray
Write-Host ""

#endregion
