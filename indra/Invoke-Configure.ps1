<#
    develop.ps1  CMake solution setup for Kirstens Viewer

    Example usage:
        .\develop.ps1 -Option "-DFOO=BAR" "-DENABLE_LOGGING=ON"

    This script configures and generates the Visual Studio solution using CMake.
    It assumes a 64-bit build and a fixed generator (Visual Studio 2022).

    MIT License

    Copyright (c) 2025 Kirstens Viewer

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.
#>

param (
    [string[]]$Option,  # CMake options (e.g. -DFOO=BAR)
    [string]$GenKey = "vc145"  # Generator key (can be overridden at runtime)
)

# -------------------------------------------------------------
# [INFO] Fixed Configuration
# -------------------------------------------------------------
$ProjectName = "Kirstens-S24"
$BuildType = "Release"
$SystemLibs = "OFF"
$AddressSize = 64
$VSGenerators = @{
    "vc143" = @{ Gen = "Visual Studio 17 2022"; Ver = "17.0" }
    "vc145" = @{ Gen = "Visual Studio 18 2026"; Ver = "18.4" }
}

# -------------------------------------------------------------
# [INFO] Locate Visual Studio via Registry
# -------------------------------------------------------------
function Find-VisualStudio {
    param ([string]$Key)

    $Ver = $VSGenerators[$Key].Ver
    $RegPaths = @(
        "HKLM:\SOFTWARE\Microsoft\VisualStudio\$Ver\Setup\VS",
        "HKLM:\SOFTWARE\Wow6432Node\Microsoft\VisualStudio\$Ver\Setup\VS"
    )

    foreach ($Path in $RegPaths) {
        try {
            $EnvDir = Get-ItemProperty -Path $Path -Name EnvironmentDirectory -ErrorAction Stop
            Write-Host "[OK] Found Visual Studio $Key at $($EnvDir.EnvironmentDirectory)" -ForegroundColor Green
            return $EnvDir.EnvironmentDirectory
        } catch {
            continue
        }
    }

    # Fallback: try vswhere (more robust). Prefer installer path, then PATH.
    $VswherePath = "$env:ProgramFiles(x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $VswherePath)) { $VswherePath = "vswhere.exe" }
    try {
        $vsPath = & $VswherePath -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
        if ($vsPath) {
            Write-Host "[OK] Found Visual Studio via vswhere at $vsPath" -ForegroundColor Green
            return $vsPath
        }
    } catch {
        # ignore vswhere errors
    }

    Write-Host "[FAIL] Visual Studio $Key not found via registry or vswhere" -ForegroundColor Red
    return ""
}

# -------------------------------------------------------------
# [INFO] Build Directory Naming
# -------------------------------------------------------------
function Get-BuildDir {
    param ([string]$GenKey)
    return "build-$GenKey"
}

# -------------------------------------------------------------
# [INFO] Run CMake to Generate Solution
# -------------------------------------------------------------
function Run-CMake {
    param (
        [string]$GenKey,
        [string[]]$Opts
    )

    $SrcDir = Get-Location
    $BuildDir = Get-BuildDir -GenKey $GenKey
    $Generator = $VSGenerators[$GenKey].Gen

    Write-Host "`n[INFO] Generator: $GenKey" -ForegroundColor Cyan
    Write-Host "[INFO] Build Type: $BuildType" -ForegroundColor Cyan
    Write-Host "[INFO] Project: $ProjectName" -ForegroundColor Cyan
    Write-Host "[INFO] Address Size: $AddressSize-bit`n" -ForegroundColor Cyan

    if (-not (Test-Path $BuildDir)) {
        Write-Host "[INFO] Creating build directory: $BuildDir" -ForegroundColor Yellow
        New-Item -ItemType Directory -Path $BuildDir | Out-Null
    }

    Push-Location $BuildDir

    # Try to get VS instance path for CMAKE_GENERATOR_INSTANCE
    $VswherePath = "$env:ProgramFiles(x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $VswherePath)) { $VswherePath = "vswhere.exe" }
    $VSInstance = ""
    try {
        $VSInstance = & $VswherePath -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2>$null
    } catch {}

    $QuotedOpts = $Opts -join " "
    $InstanceOpt = if ($VSInstance) { "-DCMAKE_GENERATOR_INSTANCE=$VSInstance" } else { "" }
    $Cmd = "cmake -G `"$Generator`" -DUSESYSTEMLIBS=$SystemLibs -DADDRESS_SIZE=$AddressSize -DROOT_PROJECT_NAME=$ProjectName $InstanceOpt $QuotedOpts `"$SrcDir`""
    Write-Host "[INFO] Running CMake command:" -ForegroundColor Yellow
    Write-Host "       $Cmd" -ForegroundColor Gray

    try {
        Invoke-Expression $Cmd
        Write-Host "[OK] CMake configuration completed" -ForegroundColor Green
    } catch {
        Write-Host "[FAIL] CMake failed: $($_.Exception.Message)" -ForegroundColor Red
    }

    Pop-Location
}

# -------------------------------------------------------------
# [INFO] Main Execution Flow
# -------------------------------------------------------------
try {
    Run-CMake -GenKey $GenKey -Opts $Option
} catch {
    Write-Host "[FAIL] Script error: $($_.Exception.Message)" -ForegroundColor Red
}