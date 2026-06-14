#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Analyzes viewer_manifest.py operations to help plan migration
.DESCRIPTION
    Compares files copied by viewer_manifest vs winpack to identify overlaps and gaps
#>

param(
    [string]$BuildDir = "..\build-vc145\newview\Release",
    [string]$PackageDir = "S24-Packaged"
)

Write-Host "=== KV S24 Viewer Manifest Analysis ===" -ForegroundColor Magenta
Write-Host ""

#region Check Build Directory

if (Test-Path $BuildDir) {
    Write-Host "Build Directory: $BuildDir" -ForegroundColor Cyan
    
    $buildFiles = Get-ChildItem $BuildDir -Recurse -File
    $buildFileCount = $buildFiles.Count
    $buildSize = ($buildFiles | Measure-Object -Property Length -Sum).Sum / 1MB
    
    Write-Host "  Files: $buildFileCount" -ForegroundColor White
    Write-Host "  Total Size: $([math]::Round($buildSize, 2)) MB" -ForegroundColor White
    
    # Categorize files
    $categories = @{
        'Executables' = @($buildFiles | Where-Object Extension -eq '.exe')
        'DLLs' = @($buildFiles | Where-Object Extension -eq '.dll')
        'XML Files' = @($buildFiles | Where-Object Extension -eq '.xml')
        'Textures' = @($buildFiles | Where-Object Extension -in @('.png', '.jpg', '.tga', '.j2c'))
        'Fonts' = @($buildFiles | Where-Object Extension -in @('.ttf', '.otf'))
        'Shaders' = @($buildFiles | Where-Object Extension -in @('.glsl', '.vert', '.frag'))
    }
    
    Write-Host "`n  File Breakdown:" -ForegroundColor Yellow
    foreach ($cat in $categories.Keys) {
        $count = $categories[$cat].Count
        if ($count -gt 0) {
            Write-Host "    $cat : $count" -ForegroundColor Gray
        }
    }
}
else {
    Write-Host "Build Directory Not Found: $BuildDir" -ForegroundColor Red
    Write-Host "Run a build first!" -ForegroundColor Yellow
}

Write-Host ""

#endregion

#region Check Package Directory

if (Test-Path $PackageDir) {
    Write-Host "Package Directory: $PackageDir" -ForegroundColor Cyan
    
    $pkgFiles = Get-ChildItem $PackageDir -Recurse -File
    $pkgFileCount = $pkgFiles.Count
    $pkgSize = ($pkgFiles | Measure-Object -Property Length -Sum).Sum / 1MB
    
    Write-Host "  Files: $pkgFileCount" -ForegroundColor White
    Write-Host "  Total Size: $([math]::Round($pkgSize, 2)) MB" -ForegroundColor White
    
    # Top-level structure
    $topDirs = Get-ChildItem $PackageDir -Directory
    Write-Host "`n  Structure:" -ForegroundColor Yellow
    foreach ($dir in $topDirs) {
        $dirFileCount = (Get-ChildItem $dir.FullName -Recurse -File).Count
        Write-Host "    $($dir.Name)\ : $dirFileCount files" -ForegroundColor Gray
    }
}
else {
    Write-Host "Package Directory Not Found: $PackageDir" -ForegroundColor Red
    Write-Host "Run winpack.ps1 first!" -ForegroundColor Yellow
}

Write-Host ""

#endregion

#region Compare Directories

if ((Test-Path $BuildDir) -and (Test-Path $PackageDir)) {
    Write-Host "=== Comparison Analysis ===" -ForegroundColor Magenta
    
    # Essential files that should be in build dir for debugging
    $essentialFiles = @(
        'app_settings\message_template.msg',
        'app_settings\message.xml',
        'app_settings\settings.xml',
        'app_settings\settings_files.xml',
        'app_settings\settings_per_account.xml'
    )
    
    Write-Host "`nEssential Runtime Files Check:" -ForegroundColor Yellow
    foreach ($file in $essentialFiles) {
        $buildPath = Join-Path $BuildDir $file
        $pkgPath = Join-Path $PackageDir $file
        
        $inBuild = Test-Path $buildPath
        $inPkg = Test-Path $pkgPath
        
        $buildMark = if ($inBuild) { "✓" } else { "✗" }
        $pkgMark = if ($inPkg) { "✓" } else { "✗" }
        
        $color = if ($inBuild -and $inPkg) { "Green" } 
                 elseif ($inPkg) { "Yellow" }
                 else { "Red" }
        
        Write-Host "  [$buildMark Build] [$pkgMark Pkg] $file" -ForegroundColor $color
    }
    
    # Check for files only in build (possible bloat)
    Write-Host "`nLarge Files in Build Directory:" -ForegroundColor Yellow
    $largeBuildFiles = $buildFiles | 
        Where-Object { $_.Length -gt 1MB } |
        Sort-Object Length -Descending |
        Select-Object -First 10
    
    foreach ($file in $largeBuildFiles) {
        $relPath = $file.FullName.Replace($BuildDir, '').TrimStart('\')
        $sizeMB = [math]::Round($file.Length / 1MB, 2)
        Write-Host "  $relPath : $sizeMB MB" -ForegroundColor Gray
    }
}

#endregion

#region Recommendations

Write-Host "`n=== Recommendations ===" -ForegroundColor Magenta

if ((Test-Path $BuildDir) -and (Test-Path $PackageDir)) {
    $ratio = $buildFileCount / $pkgFileCount
    
    if ($ratio -gt 0.8) {
        Write-Host "⚠️  Build directory has $buildFileCount files vs $pkgFileCount in package" -ForegroundColor Yellow
        Write-Host "   Consider Option D: Minimal CMake copying" -ForegroundColor Cyan
        Write-Host "   This would reduce build directory bloat significantly`n" -ForegroundColor Cyan
    }
    elseif ($ratio -lt 0.2) {
        Write-Host "✓  Build directory is minimal ($buildFileCount files)" -ForegroundColor Green
        Write-Host "   Current setup is already lean!`n" -ForegroundColor Green
    }
    else {
        Write-Host "ℹ️  Build directory has moderate file count" -ForegroundColor White
        Write-Host "   Review which files are needed for debugging`n" -ForegroundColor White
    }
}

Write-Host "Next Steps:" -ForegroundColor Yellow
Write-Host "1. Review: MANIFEST_MIGRATION_OPTIONS.md" -ForegroundColor White
Write-Host "2. Choose: Option A, B, C, or D based on your workflow" -ForegroundColor White
Write-Host "3. Test: Build after changes and verify functionality" -ForegroundColor White
Write-Host "4. Cleanup: Remove old Python scripts once confirmed`n" -ForegroundColor White

#endregion

#region Python Dependencies Check

Write-Host "=== Python Dependencies Check ===" -ForegroundColor Magenta

$pythonScripts = Get-ChildItem "..\..\scripts" -Filter "*.py" -File -ErrorAction SilentlyContinue

if ($pythonScripts) {
    Write-Host "Python scripts found in scripts/:" -ForegroundColor Yellow
    foreach ($script in $pythonScripts) {
        $size = [math]::Round($script.Length / 1KB, 1)
        Write-Host "  $($script.Name) ($size KB)" -ForegroundColor Gray
    }
    
    Write-Host "`n✓ template_verifier.py can be replaced (already done)" -ForegroundColor Green
    Write-Host "? Other .py scripts may need review for modernization" -ForegroundColor Yellow
}
else {
    Write-Host "No Python scripts found" -ForegroundColor Green
}

#endregion

Write-Host ""
