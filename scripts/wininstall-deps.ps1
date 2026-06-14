#Requires -Version 7.0
<#
.SYNOPSIS
    Kirstens Viewer S24 - Dependency Auto-Installer
.DESCRIPTION
    Detects and installs missing build dependencies for the S24 viewer.
    Called by AutoBuild-S24.ps1 when missing components are detected,
    or can be run standalone for a full environment check.
.PARAMETER MissingComponents
    Optional array of component names passed from AutoBuild-S24.ps1.
    When omitted the script scans for all known dependencies.
.NOTES
    (c) 2025 Kirstenlee Cinquetti
    Requires PowerShell 7+ and Windows 10/11.
#>

param(
    [string[]]$MissingComponents = @()
)

# ── Strict mode ──────────────────────────────────────────────────────────────
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ── Constants ────────────────────────────────────────────────────────────────
$VCPKG_CLONE_URL  = "https://github.com/microsoft/vcpkg.git"
$VCPKG_INSTALL_DIR = "C:\vcpkg"

# VS 2026 workload that includes MSBuild + C++ desktop tools
$VS_WORKLOAD = "Microsoft.VisualStudio.Workload.NativeDesktop"

# ── Banner ───────────────────────────────────────────────────────────────────
function Show-Banner {
    Write-Host ""
    Write-Host "==============================================================" -ForegroundColor Cyan
    Write-Host "    Kirstens Viewer S24 - Dependency Installer" -ForegroundColor Green
    Write-Host "    (c) 2025 Kirstenlee Cinquetti" -ForegroundColor Magenta
    Write-Host "==============================================================" -ForegroundColor Cyan
    Write-Host ""
}

# ── Helpers ──────────────────────────────────────────────────────────────────
function Write-Step  ([string]$msg) { Write-Host "  [*] $msg" -ForegroundColor Cyan }
function Write-Ok    ([string]$msg) { Write-Host "  [+] $msg" -ForegroundColor Green }
function Write-Warn  ([string]$msg) { Write-Host "  [!] $msg" -ForegroundColor Yellow }
function Write-Fail  ([string]$msg) { Write-Host "  [-] $msg" -ForegroundColor Red }
function Write-Detail([string]$msg) { Write-Host "      $msg" -ForegroundColor Gray }

function Test-IsAdmin {
    $identity  = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]$identity
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Request-Elevation {
    <#
    .SYNOPSIS Re-launch the current script as Administrator via pwsh.
    #>
    if (Test-IsAdmin) { return }

    Write-Warn "This script requires Administrator privileges for PATH updates and installations."
    Write-Step "Requesting elevation..."

    $scriptPath = $PSCommandPath
    $argString  = "-NoProfile -ExecutionPolicy Bypass -File `"$scriptPath`""
    if ($MissingComponents.Count -gt 0) {
        $joined = ($MissingComponents | ForEach-Object { "`"$_`"" }) -join ","
        $argString += " -MissingComponents $joined"
    }

    try {
        Start-Process pwsh -Verb RunAs -ArgumentList $argString -Wait
        Write-Ok "Elevated process completed."
        exit 0
    }
    catch {
        Write-Fail "Elevation was declined or failed: $($_.Exception.Message)"
        exit 1
    }
}

function Refresh-EnvironmentPath {
    <#
    .SYNOPSIS Reload Machine + User PATH into the current session.
    #>
    $machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
    $userPath    = [Environment]::GetEnvironmentVariable("Path", "User")
    $env:Path    = "$machinePath;$userPath"
    Write-Detail "Session PATH refreshed from registry."
}

function Add-ToMachinePath([string]$directory) {
    $currentPath = [Environment]::GetEnvironmentVariable("Path", "Machine")
    if ($currentPath -split ";" | Where-Object { $_ -ieq $directory }) {
        Write-Detail "'$directory' is already in the Machine PATH."
        return
    }
    Write-Step "Adding '$directory' to Machine PATH..."
    [Environment]::SetEnvironmentVariable("Path", "$currentPath;$directory", "Machine")
    Write-Ok "Machine PATH updated."
    Refresh-EnvironmentPath
}

function Set-MachineEnvironmentVariable([string]$name, [string]$value) {
    Write-Step "Setting Machine environment variable: $name = $value"
    [Environment]::SetEnvironmentVariable($name, $value, "Machine")
    # Also apply in current session
    Set-Item -Path "Env:\$name" -Value $value
    Write-Ok "$name environment variable set."
}

function Test-CommandExists([string]$cmd) {
    return [bool](Get-Command $cmd -ErrorAction SilentlyContinue)
}

function Assert-WingetAvailable {
    if (Test-CommandExists "winget") {
        Write-Ok "winget is available."
        return $true
    }
    Write-Fail "winget is not available. Please install App Installer from the Microsoft Store."
    Write-Detail "https://aka.ms/getwinget"
    return $false
}

# ── Component Installers ────────────────────────────────────────────────────

function Install-Vcpkg {
    Write-Host ""
    Write-Host "  ── vcpkg ────────────────────────────────────────" -ForegroundColor White

    # Already present?
    $existing = $null
    if ($env:VCPKG_ROOT -and (Test-Path (Join-Path $env:VCPKG_ROOT "vcpkg.exe"))) {
        $existing = Join-Path $env:VCPKG_ROOT "vcpkg.exe"
    }
    if (-not $existing) {
        $existing = (Get-Command vcpkg -ErrorAction SilentlyContinue).Source
    }
    if ($existing) {
        Write-Ok "vcpkg already installed at: $existing"
        return $true
    }

    # Need git
    if (-not (Test-CommandExists "git")) {
        Write-Fail "git is required to clone vcpkg but was not found."
        Write-Detail "Install git first, or add it to PATH."
        return $false
    }

    Write-Step "Cloning vcpkg from $VCPKG_CLONE_URL ..."
    Write-Detail "Target directory: $VCPKG_INSTALL_DIR"

    if (Test-Path $VCPKG_INSTALL_DIR) {
        Write-Warn "Directory $VCPKG_INSTALL_DIR already exists. Attempting to use it."
    }
    else {
        & git clone $VCPKG_CLONE_URL $VCPKG_INSTALL_DIR 2>&1 | ForEach-Object { Write-Detail $_ }
        if ($LASTEXITCODE -ne 0) {
            Write-Fail "git clone failed (exit code $LASTEXITCODE)."
            return $false
        }
    }

    Write-Step "Bootstrapping vcpkg..."
    $bootstrapScript = Join-Path $VCPKG_INSTALL_DIR "bootstrap-vcpkg.bat"
    if (-not (Test-Path $bootstrapScript)) {
        Write-Fail "bootstrap-vcpkg.bat not found in $VCPKG_INSTALL_DIR."
        return $false
    }

    Push-Location $VCPKG_INSTALL_DIR
    try {
        & cmd.exe /c $bootstrapScript -disableMetrics 2>&1 | ForEach-Object { Write-Detail $_ }
        if ($LASTEXITCODE -ne 0) {
            Write-Fail "vcpkg bootstrap failed (exit code $LASTEXITCODE)."
            return $false
        }
    }
    finally {
        Pop-Location
    }

    $vcpkgExe = Join-Path $VCPKG_INSTALL_DIR "vcpkg.exe"
    if (-not (Test-Path $vcpkgExe)) {
        Write-Fail "vcpkg.exe was not created. Bootstrap may have failed silently."
        return $false
    }

    # Environment
    Set-MachineEnvironmentVariable "VCPKG_ROOT" $VCPKG_INSTALL_DIR
    Add-ToMachinePath $VCPKG_INSTALL_DIR

    Write-Step "Running vcpkg integrate install..."
    & $vcpkgExe integrate install 2>&1 | ForEach-Object { Write-Detail $_ }

    Write-Ok "vcpkg installed and integrated successfully."
    return $true
}

function Install-DotNetSdk {
    Write-Host ""
    Write-Host "  ── .NET SDK ─────────────────────────────────────" -ForegroundColor White

    if (Test-CommandExists "dotnet") {
        $ver = & dotnet --version 2>$null
        if ($ver) {
            Write-Ok ".NET SDK already installed: $ver"
            return $true
        }
    }

    if (-not (Assert-WingetAvailable)) { return $false }

    Write-Step "Installing latest .NET SDK via winget..."
    & winget install Microsoft.DotNet.SDK.9 --accept-source-agreements --accept-package-agreements --silent 2>&1 |
        ForEach-Object { Write-Detail $_ }

    if ($LASTEXITCODE -ne 0) {
        Write-Warn "winget exit code $LASTEXITCODE (may still be OK if already installed)."
    }

    Refresh-EnvironmentPath

    if (Test-CommandExists "dotnet") {
        $ver = & dotnet --version 2>$null
        Write-Ok ".NET SDK installed: $ver"
        return $true
    }
    else {
        Write-Fail ".NET SDK installation could not be verified."
        Write-Detail "You may need to restart your terminal or reboot."
        return $false
    }
}

function Install-MSBuild {
    Write-Host ""
    Write-Host "  ── MSBuild / Visual Studio C++ Workload ────────" -ForegroundColor White

    # Check via vswhere
    $vswherePath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswherePath) {
        $msbExe = & $vswherePath -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe 2>$null
        if ($msbExe) {
            Write-Ok "MSBuild already available: $msbExe"
            return $true
        }
    }

    if (-not (Assert-WingetAvailable)) { return $false }

    Write-Step "Installing Visual Studio 2026 Build Tools with C++ workload via winget..."
    Write-Detail "Workload: $VS_WORKLOAD"
    Write-Detail "This may take a while — Visual Studio components are large."

    # Use the BuildTools SKU so we don't force a full VS install
    & winget install Microsoft.VisualStudio.2026.BuildTools `
        --override "--quiet --wait --add $VS_WORKLOAD --includeRecommended" `
        --accept-source-agreements --accept-package-agreements 2>&1 |
        ForEach-Object { Write-Detail $_ }

    if ($LASTEXITCODE -ne 0) {
        Write-Warn "winget exit code $LASTEXITCODE — checking if MSBuild is now available..."
    }

    Refresh-EnvironmentPath

    # Re-check
    if (Test-Path $vswherePath) {
        $msbExe = & $vswherePath -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe 2>$null
        if ($msbExe) {
            Write-Ok "MSBuild now available: $msbExe"
            return $true
        }
    }

    Write-Fail "MSBuild could not be verified after install."
    Write-Detail "If Visual Studio is already installed, add the C++ Desktop workload via the Visual Studio Installer."
    return $false
}

function Install-CMake {
    Write-Host ""
    Write-Host "  ── CMake ────────────────────────────────────────" -ForegroundColor White

    if (Test-CommandExists "cmake") {
        $ver = & cmake --version 2>$null | Select-Object -First 1
        Write-Ok "CMake already installed: $ver"
        return $true
    }

    if (-not (Assert-WingetAvailable)) { return $false }

    Write-Step "Installing CMake via winget..."
    & winget install Kitware.CMake --accept-source-agreements --accept-package-agreements --silent 2>&1 |
        ForEach-Object { Write-Detail $_ }

    Refresh-EnvironmentPath

    if (Test-CommandExists "cmake") {
        $ver = & cmake --version 2>$null | Select-Object -First 1
        Write-Ok "CMake installed: $ver"
        return $true
    }
    else {
        Write-Fail "CMake installation could not be verified."
        return $false
    }
}

function Install-Python {
    Write-Host ""
    Write-Host "  ── Python ───────────────────────────────────────" -ForegroundColor White

    if (Test-CommandExists "python") {
        $ver = & python --version 2>$null
        Write-Ok "Python already installed: $ver"
        return $true
    }

    if (-not (Assert-WingetAvailable)) { return $false }

    Write-Step "Installing Python 3.x via winget..."
    & winget install Python.Python.3.12 --accept-source-agreements --accept-package-agreements --silent 2>&1 |
        ForEach-Object { Write-Detail $_ }

    Refresh-EnvironmentPath

    if (Test-CommandExists "python") {
        $ver = & python --version 2>$null
        Write-Ok "Python installed: $ver"
        return $true
    }
    else {
        Write-Fail "Python installation could not be verified."
        return $false
    }
}

function Install-Git {
    Write-Host ""
    Write-Host "  ── Git ──────────────────────────────────────────" -ForegroundColor White

    if (Test-CommandExists "git") {
        $ver = & git --version 2>$null
        Write-Ok "Git already installed: $ver"
        return $true
    }

    if (-not (Assert-WingetAvailable)) { return $false }

    Write-Step "Installing Git via winget..."
    & winget install Git.Git --accept-source-agreements --accept-package-agreements --silent 2>&1 |
        ForEach-Object { Write-Detail $_ }

    Refresh-EnvironmentPath

    if (Test-CommandExists "git") {
        $ver = & git --version 2>$null
        Write-Ok "Git installed: $ver"
        return $true
    }
    else {
        Write-Fail "Git installation could not be verified."
        return $false
    }
}

# ── Component Registry ──────────────────────────────────────────────────────
# Maps the component name strings (from AutoBuild-S24 $missingComponents) to installer functions.
$ComponentMap = [ordered]@{
    "Git"                            = { Install-Git }
    "vcpkg"                          = { Install-Vcpkg }
    ".NET SDK"                       = { Install-DotNetSdk }
    "MSBuild"                        = { Install-MSBuild }
    "vswhere / Visual Studio Installer" = { Install-MSBuild }  # vswhere ships with VS
    "CMake"                          = { Install-CMake }
    "Python"                         = { Install-Python }
}

# ── Full Scan Mode ──────────────────────────────────────────────────────────
function Get-AllMissingComponents {
    <#
    .SYNOPSIS When no -MissingComponents are passed, scan for everything.
    #>
    $missing = @()
    if (-not (Test-CommandExists "git"))    { $missing += "Git" }
    if (-not (Test-CommandExists "cmake"))  { $missing += "CMake" }
    if (-not (Test-CommandExists "python")) { $missing += "Python" }
    if (-not (Test-CommandExists "dotnet")) { $missing += ".NET SDK" }

    # vcpkg
    $vcFound = $false
    if ($env:VCPKG_ROOT -and (Test-Path (Join-Path $env:VCPKG_ROOT "vcpkg.exe"))) { $vcFound = $true }
    if (-not $vcFound -and (Test-CommandExists "vcpkg")) { $vcFound = $true }
    if (-not $vcFound) { $missing += "vcpkg" }

    # MSBuild
    $vswherePath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswherePath) {
        $msb = & $vswherePath -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe 2>$null
        if (-not $msb) { $missing += "MSBuild" }
    }
    else {
        $missing += "vswhere / Visual Studio Installer"
    }

    return $missing
}

# ── Main ─────────────────────────────────────────────────────────────────────
Show-Banner

# Elevation gate
if (-not (Test-IsAdmin)) {
    Request-Elevation
    # If we reach here, elevation spawned a child process that already ran.
    exit 0
}

Write-Ok "Running as Administrator."
Write-Host ""

# Determine what to install
if ($MissingComponents.Count -eq 0) {
    Write-Step "No component list provided — performing full environment scan..."
    $MissingComponents = Get-AllMissingComponents
}

if ($MissingComponents.Count -eq 0) {
    Write-Host ""
    Write-Ok "All build dependencies are present. Nothing to install!"
    Write-Host ""
    Pause
    exit 0
}

Write-Host ""
Write-Host "  Components to install:" -ForegroundColor White
foreach ($comp in $MissingComponents) {
    Write-Warn $comp
}
Write-Host ""

# Confirm
Write-Host "  Proceed with installation? (Y/N)" -ForegroundColor Yellow -NoNewline
$confirm = Read-Host " "
if ($confirm -notin @("Y", "y", "Yes", "yes")) {
    Write-Warn "Installation cancelled by user."
    exit 0
}

# Verify winget up front — most installers depend on it
$hasWinget = Test-CommandExists "winget"
if (-not $hasWinget) {
    Write-Warn "winget is not available. Only vcpkg (via git clone) can be installed without it."
    Write-Detail "Install 'App Installer' from the Microsoft Store: https://aka.ms/getwinget"
}

# Run each installer
$results = [ordered]@{}

foreach ($comp in $MissingComponents) {
    $installer = $null

    # Find matching installer (case-insensitive partial match)
    foreach ($key in $ComponentMap.Keys) {
        if ($comp -ieq $key) {
            $installer = $ComponentMap[$key]
            break
        }
    }

    if ($null -eq $installer) {
        Write-Warn "No installer defined for component: '$comp' — skipping."
        $results[$comp] = "Skipped"
        continue
    }

    try {
        $success = & $installer
        $results[$comp] = if ($success) { "OK" } else { "FAILED" }
    }
    catch {
        Write-Fail "Exception installing '$comp': $($_.Exception.Message)"
        $results[$comp] = "ERROR"
    }
}

# ── Summary ──────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "==============================================================" -ForegroundColor Cyan
Write-Host "    Installation Summary" -ForegroundColor Green
Write-Host "==============================================================" -ForegroundColor Cyan

foreach ($entry in $results.GetEnumerator()) {
    $color = switch ($entry.Value) {
        "OK"      { "Green" }
        "Skipped" { "DarkYellow" }
        default   { "Red" }
    }
    $icon = switch ($entry.Value) {
        "OK"      { "[+]" }
        "Skipped" { "[~]" }
        default   { "[-]" }
    }
    Write-Host "  $icon $($entry.Key): $($entry.Value)" -ForegroundColor $color
}

$failCount = ($results.Values | Where-Object { $_ -notin @("OK", "Skipped") }).Count

Write-Host ""
if ($failCount -eq 0) {
    Write-Ok "All requested components installed successfully!"
    Write-Detail "You may need to restart your terminal for PATH changes to take full effect."
}
else {
    Write-Fail "$failCount component(s) failed. Review the output above for details."
}

Write-Host ""
Pause
exit $failCount