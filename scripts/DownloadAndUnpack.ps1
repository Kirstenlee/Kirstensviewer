# ================================================================================
#  DownloadAndUnpack.ps1 - Package Download and Installation Script
# ================================================================================
#  Copyright (c) 2025-2026 Kirsten's Viewer Contributors
#  SPDX-License-Identifier: MIT
#
#  Downloads and installs third-party packages declared in installS24.xml.
#
#  SUPPORTED PACKAGE TYPES
#  =======================
#    tarball  Pre-built archives fetched from a URL, validated by MD5,
#             and extracted with Windows tar.exe (gzip / bzip2 / zstd).
#
#    vcpkg   Ports installed on-the-fly via vcpkg.  When <version> is set
#             in the manifest, the script generates a temporary vcpkg.json
#             with a version override (classic mode does not support
#             @version pinning).  Optional <features>, <triplet>, and
#             <args> elements provide full control over the install.
#
#  USAGE
#  =====
#    .\DownloadAndUnpack.ps1 [-TopDir <path>] [-Force]
#
#  PARAMETERS
#  ==========
#    -TopDir   Root of the project tree.  Defaults to the parent of the
#              scripts/ directory (the repository root).
#    -Force    Ignore the ledger and reinstall every package.
#
#  STATE & CACHING
#  ===============
#    installed-packages.json   Ledger that fingerprints each installed
#                              package; unchanged entries are skipped.
#    .package-cache/           Downloaded tarball cache.
#    .vcpkg-packages/          Per-port vcpkg install roots.
#    .vcpkg-temp/              Temporary vcpkg downloads, build trees,
#                              and generated manifest files.
#    indra/build-vc145/prepare/prebuilt
#                              Completion marker; the script exits early
#                              when this file is newer than the manifest
#                              and the script itself.
#
#  DEPENDENCIES
#  ============
#    PowerShell 5.1+   (ships with Windows 10/11)
#    tar.exe            (ships with Windows 10 1803+)
#    robocopy.exe       (ships with Windows)
#    vcpkg.exe          (must be on PATH; Scoop shims auto-resolved)
#    git.exe            (required only for versioned vcpkg packages)
#
#  EXIT CODES
#  ==========
#    0   All packages installed or skipped successfully.
#    1   Fatal error (missing manifest, zero packages, nothing processed).
#    N   Number of failed packages (completion marker not created).
# ================================================================================

param ( 
    [string]$TopDir,
    [switch]$Force
)

# ================================================================================
# FUNCTION: Get-MD5Hash
# ================================================================================
# Computes the MD5 hash of a file for integrity verification.
#
# Parameters:
#   - FilePath: Path to the file to hash
#
# Returns: MD5 hash as lowercase hex string
# ================================================================================
function Get-MD5Hash {
    param ([string]$FilePath)
    
    if (-not (Test-Path $FilePath)) {
        throw "File not found for MD5 hash: $FilePath"
    }
    
    try {
        $md5 = [System.Security.Cryptography.MD5]::Create()
        $stream = [System.IO.File]::OpenRead($FilePath)
        $hashBytes = $md5.ComputeHash($stream)
        $stream.Close()
        $stream.Dispose()
        $md5.Dispose()
        return ([BitConverter]::ToString($hashBytes) -replace "-", "").ToLower()
    }
    catch {
        if ($stream) { $stream.Dispose() }
        if ($md5) { $md5.Dispose() }
        throw "Failed to compute MD5 hash: $($_.Exception.Message)"
    }
}

# ================================================================================
# FUNCTION: Unpack-Archive
# ================================================================================
# Unpacks tar archives (gzip, bzip2, zstd) using Windows tar.exe.
#
# Parameters:
#   - ArchivePath: Path to the archive file
#   - Destination: Directory to extract files to
#
# Returns: 0 on success, non-zero error code on failure
# ================================================================================
function Unpack-Archive {
    param (
        [string]$ArchivePath,
        [string]$Destination
    )

    # Validate archive exists
    if (-not (Test-Path $ArchivePath)) {
        Write-Host "[FAIL] Archive not found: $ArchivePath" -ForegroundColor Red
        return 1
    }

    # Create destination directory if needed
    if (-not (Test-Path $Destination)) {
        try {
            New-Item -ItemType Directory -Path $Destination -Force | Out-Null
        } catch {
            Write-Host " [FAIL] Failed to create destination: $Destination" -ForegroundColor Red
            return 2
        }
    }

    # Detect archive type by magic bytes (first 2 bytes of file)
    try {
        $bytes = Get-Content $ArchivePath -Encoding Byte -TotalCount 4
        $sig   = "{0:X2}{1:X2}" -f $bytes[0], $bytes[1]
    } catch {
        Write-Host "Failed to read archive header: $ArchivePath" -ForegroundColor Red
        return 3
    }

    # Build tar arguments based on archive type
    # Windows tar.exe (BSD tar) overwrites by default - no --overwrite flag needed
    switch ($sig) {
        "1F8B" { $tarArgs = @("-xzf", "`"$ArchivePath`"", "-C", "`"$Destination`"") }  # gzip
        "425A" { $tarArgs = @("-xjf", "`"$ArchivePath`"", "-C", "`"$Destination`"") }  # bzip2
        "28B5" { $tarArgs = @("--zstd", "-xf", "`"$ArchivePath`"", "-C", "`"$Destination`"") }  # zstd
        default {
            Write-Host " [FAIL] Unsupported archive format: 0x${sig}" -ForegroundColor Red
            return 4
        }
    }

    # Execute tar extraction
    try {
        $proc = Start-Process -FilePath "tar.exe" -ArgumentList $tarArgs -NoNewWindow -Wait -PassThru
        if ($proc.ExitCode -ne 0) {
            Write-Host " [FAIL] tar.exe failed with exit code $($proc.ExitCode)" -ForegroundColor Red
            return 5
        }
    } catch {
        Write-Host "Failed to launch tar.exe: $($_.Exception.Message)" -ForegroundColor Red
        return 6
    }

    Write-Host " [OK] Unpacked: $ArchivePath ---> $Destination" -ForegroundColor Green
    return 0
}

# ================================================================================
# FUNCTION: Get-PackageArchive
# ================================================================================
# Downloads a package archive from a URL with caching and validation.
#
# Parameters:
#   - Name: Package name (for logging)
#   - Url: Download URL
#   - CacheDir: Directory to cache downloads
#   - ExpectedMD5: Expected MD5 hash for validation (optional)
#
# Returns: Path to the downloaded archive file
# Throws: On download or validation failure
# ================================================================================
function Get-PackageArchive {
    param (
        [string]$Name,
        [string]$Url,
        [string]$CacheDir,
        [string]$ExpectedMD5
    )

    $archiveName = [IO.Path]::GetFileName($Url)
    $archivePath = Join-Path $CacheDir $archiveName
    
    # Use a temp directory within the project (no admin needed, no spaces)
    $tempDir = Join-Path $script:TopDir ".temp-downloads"
    if (-not (Test-Path $tempDir)) {
        New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
    }
    $tempFile = Join-Path $tempDir "$archiveName.download"
    $lockFile = "$archivePath.lock"

    # Return cached file if it exists
    if (Test-Path $archivePath) {
        Write-Host " [OK] Cached: $archiveName" -ForegroundColor Yellow
        return $archivePath
    }

    # Check for lock file (prevents concurrent downloads)
    if (Test-Path $lockFile) {
        throw " [FAIL] Lock exists for ${Name} - $lockFile"
    }

    # Ensure cache directory exists
    if (-not (Test-Path $CacheDir)) {
        try {
            New-Item -ItemType Directory -Path $CacheDir -Force | Out-Null
        } catch {
            throw "Failed to create cache directory: $CacheDir"
        }
    }

    # Create lock file
    New-Item $lockFile -ItemType File -Force | Out-Null

    try {
        # Download the file
        Write-Host " --> Downloading: $archiveName" -ForegroundColor DarkGray
        Invoke-WebRequest -Uri $Url -OutFile $tempFile -UseBasicParsing -TimeoutSec 60 -ErrorAction Stop

        # Validate download completed
        if (-not (Test-Path $tempFile) -or ((Get-Item $tempFile).Length -lt 1024)) {
            throw " [FAIL] Download failed or incomplete for ${Name}"
        }

        # Verify file is accessible (not locked)
        try {
            $stream = [System.IO.File]::Open($tempFile, 'Open', 'Read', 'None')
            $stream.Close()
            $stream.Dispose()
        } catch {
            throw " [FAIL] File lock detected on $tempFile"
        }

        # Validate magic bytes (file signature)
        $magic = Get-Content $tempFile -Encoding Byte -TotalCount 2
        $sig = '{0:X2}{1:X2}' -f $magic[0], $magic[1]
        if ($sig -notin @("1F8B", "425A", "28B5")) {
            throw " [FAIL] Magic-byte check failed for ${Name}: 0x${sig}"
        }

        # Validate MD5 hash if provided
        if ($ExpectedMD5) {
            $actualMD5 = Get-MD5Hash -FilePath $tempFile
            if ($actualMD5 -ne $ExpectedMD5.ToLower()) {
                throw " [FAIL] MD5 mismatch for ${Name}: expected ${ExpectedMD5}, got ${actualMD5}"
            }
        }

        # Move to cache location
        Move-Item -Path $tempFile -Destination $archivePath -Force
        Write-Host " [OK] Downloaded: $archiveName" -ForegroundColor Green
        return $archivePath
    }
    catch {
        Write-Host " [FAIL] Error: $($_.Exception.Message)" -ForegroundColor Red
        throw $_
    }
    finally {
        # Cleanup: Remove lock and temp files
        if (Test-Path $lockFile) { Remove-Item $lockFile -Force }
        if (Test-Path $tempFile) { Remove-Item $tempFile -Force }
    }
}

# ================================================================================
# LEDGER FUNCTIONS: Track installed packages to avoid redundant work
# ================================================================================
# Each entry is keyed by package name and stores a fingerprint (md5+url or
# portSpec+triplet) so a changed manifest entry triggers reinstallation.
# ================================================================================

function Load-Ledger {
    param ([string]$Path)
    if (Test-Path $Path) {
        try {
            return (Get-Content $Path -Raw | ConvertFrom-Json)
        } catch {
            Write-Host "  --> Ledger corrupt, starting fresh." -ForegroundColor Yellow
        }
    }
    return [pscustomobject]@{}
}

function Save-Ledger {
    param ($Ledger, [string]$Path)
    $dir = Split-Path $Path -Parent
    if (-not (Test-Path $dir)) {
        New-Item -ItemType Directory -Path $dir -Force | Out-Null
    }
    $Ledger | ConvertTo-Json -Depth 4 | Set-Content $Path -Encoding UTF8
}

function Get-LedgerFingerprint {
    param ($Ledger, [string]$Name)
    if ($Ledger.PSObject.Properties.Name -contains $Name) {
        return $Ledger.$Name
    }
    return $null
}

function Set-LedgerFingerprint {
    param ($Ledger, [string]$Name, [string]$Fingerprint)
    if ($Ledger.PSObject.Properties.Name -contains $Name) {
        $Ledger.$Name = $Fingerprint
    } else {
        $Ledger | Add-Member -NotePropertyName $Name -NotePropertyValue $Fingerprint
    }
}

# ================================================================================
# MAIN SCRIPT EXECUTION
# ================================================================================

# Resolve absolute paths    
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
if (-not $TopDir) {
    $TopDir = (Resolve-Path (Join-Path $scriptDir "..")).Path
}
$targetDir = $TopDir
$installXmlPath = Join-Path $TopDir "installS24.xml"

# ================================================================================
# EARLY EXIT: Skip if marker is up-to-date relative to the manifest and script
# ================================================================================
$markerDir  = Join-Path $TopDir "indra/build-vc145/prepare"
$markerPath = Join-Path $markerDir "prebuilt"

if (-not $Force -and (Test-Path $markerPath) -and (Test-Path $installXmlPath)) {
    $markerTime   = (Get-Item $markerPath).LastWriteTimeUtc
    $manifestTime = (Get-Item $installXmlPath).LastWriteTimeUtc
    $scriptTime   = (Get-Item $MyInvocation.MyCommand.Definition).LastWriteTimeUtc

    if ($markerTime -ge $manifestTime -and $markerTime -ge $scriptTime) {
        Write-Host "Packages are up-to-date (marker newer than manifest and script). Skipping." -ForegroundColor Green
        exit 0
    }
}

Write-Host "Top Directory: $TopDir" -ForegroundColor Cyan
Write-Host "Target Directory: $targetDir" -ForegroundColor Cyan

# Validate manifest file exists
if (-not (Test-Path $installXmlPath)) {
    Write-Host "ERROR: Installation manifest not found: $installXmlPath" -ForegroundColor Red
    exit 1
}

# Load and validate XML manifest
try {
    [xml]$xml = Get-Content $installXmlPath
    if (-not $xml.packages.package) {
        throw "No <package> entries found in manifest."
    }
} catch {
    Write-Host "ERROR: Failed to load manifest: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

# Setup cache directory (self-contained within project)
$cacheDir = Join-Path $TopDir ".package-cache"
if (-not (Test-Path $cacheDir)) {
    New-Item -ItemType Directory -Path $cacheDir | Out-Null
}

# Load the installed-packages ledger
$ledgerPath = Join-Path $TopDir "installed-packages.json"
$ledger = Load-Ledger -Path $ledgerPath

# Partition manifest packages by type
$allPackages     = @($xml.packages.package)
$vcpkgPackages   = @($allPackages | Where-Object { [string]$_.type -eq "vcpkg" })
$archivePackages = @($allPackages | Where-Object { [string]$_.type -eq "tarball" })
$unknownPackages = @($allPackages | Where-Object { [string]$_.type -notin @("vcpkg", "tarball") })

Write-Host "  --> Packages: total=$($allPackages.Count), tarball=$($archivePackages.Count), vcpkg=$($vcpkgPackages.Count)" -ForegroundColor DarkGray

if ($unknownPackages.Count -gt 0) {
    foreach ($unk in $unknownPackages) {
        Write-Host "  --> WARNING: Unknown package type '$([string]$unk.type)' for '$([string]$unk.name)' - skipping" -ForegroundColor Yellow
    }
}

if ($allPackages.Count -eq 0) {
    Write-Host "ERROR: Manifest contains zero packages." -ForegroundColor Red
    exit 1
}

# Initialize counters for summary report
$successCount = 0
$skipCount    = 0
$failCount    = 0
$failedPackages = @()

# ================================================================================
# PHASE 1: Archive packages
# ================================================================================
foreach ($pkg in $archivePackages) {
    $name = [string]$pkg.name
    $url = [string]$pkg.url
    $md5 = [string]$pkg.md5

    Write-Host "`n -> Processing: $name" -ForegroundColor Cyan

    try {
        if (-not $url -or [string]::IsNullOrWhiteSpace($url)) {
            throw "Tarball package '$name' is missing required <url> element."
        }

        # Fingerprint = md5 + url so any manifest change triggers reinstall
        $fingerprint = "$($md5.ToLower())|$url"
        $existing = Get-LedgerFingerprint -Ledger $ledger -Name $name

        if (-not $Force -and $existing -eq $fingerprint) {
            Write-Host "  [SKIP] Already installed: $name" -ForegroundColor DarkGray
            $skipCount++
            continue
        }

        $archivePath = Get-PackageArchive -Name $name -Url $url -CacheDir $cacheDir -ExpectedMD5 $md5
        $rc = Unpack-Archive -ArchivePath $archivePath -Destination $targetDir
        if ($rc -ne 0) {
            throw "Unpack failed for '$name' (code $rc)."
        }

        # Record success in ledger
        Set-LedgerFingerprint -Ledger $ledger -Name $name -Fingerprint $fingerprint
        Save-Ledger -Ledger $ledger -Path $ledgerPath

        Write-Host "  [OK] Installed archive package: $name" -ForegroundColor Green
        $successCount++
    }
    catch {
        Write-Host "  [FAIL] archive error: $($_.Exception.Message)" -ForegroundColor Red
        $failCount++
        $failedPackages += $name
    }
}

# ================================================================================
# VCPKG SETUP: Only runs when manifest contains vcpkg packages
# ================================================================================
if ($vcpkgPackages.Count -gt 0) {

    # --------------------------------------------------------------------
    # Locate vcpkg executable
    # --------------------------------------------------------------------
    $vcpkgCmd = Get-Command vcpkg -ErrorAction SilentlyContinue
    if (-not $vcpkgCmd -or -not $vcpkgCmd.Source) {
        throw "vcpkg.exe not found in PATH. Please install vcpkg or add it to PATH."
    }
    $vcpkgExe = $vcpkgCmd.Source

    # Resolve Scoop shim to real executable if needed
    if ($vcpkgExe -match '\\shims\\') {
        Write-Host "  --> Resolving Scoop shim..." -ForegroundColor DarkGray
        $shimContent = Get-Content -LiteralPath $vcpkgExe -Raw -ErrorAction SilentlyContinue
        if ($shimContent -match 'path\s*=\s*"([^"]+)"') {
            $vcpkgExe = $matches[1]
        } else {
            $scoopPath = $env:SCOOP
            if (-not $scoopPath) { $scoopPath = "$env:USERPROFILE\scoop" }
            $candidate = Join-Path $scoopPath "apps\vcpkg\current\vcpkg.exe"
            if (-not (Test-Path -LiteralPath $candidate)) {
                throw "Could not resolve vcpkg.exe from Scoop shim."
            }
            $vcpkgExe = $candidate
        }
    }

    Write-Host "  --> vcpkg exe: $vcpkgExe" -ForegroundColor DarkGray

    # --------------------------------------------------------------------
    # Pre-validate vcpkg git repo for versioned packages
    # --------------------------------------------------------------------
    # vcpkg uses git internally (read-tree) to checkout port files from
    # specific tree hashes when version overrides are active.  Scoop-
    # installed vcpkg often has a broken .git reference (gitdir ->
    # persist/vcpkg/.git which may not exist after updates).  When the
    # local repo is broken we clone vcpkg to a project-local directory
    # and redirect VCPKG_ROOT so all git operations succeed.
    # --------------------------------------------------------------------
    $hasVersionedPkgs = @($vcpkgPackages | Where-Object { [string]$_.version })

    if ($hasVersionedPkgs.Count -gt 0) {

        # --- Resolve git.exe (may not be on PATH in MSBuild context) ---
        $script:gitExe = $null
        $gitCmd = Get-Command git -ErrorAction SilentlyContinue
        if ($gitCmd) {
            $script:gitExe = $gitCmd.Source
        } else {
            $gitCandidates = @(
                (Join-Path $env:ProgramFiles "Git\cmd\git.exe"),
                (Join-Path ${env:ProgramFiles(x86)} "Git\cmd\git.exe")
            )
            $scoopBase = if ($env:SCOOP) { $env:SCOOP } else { "$env:USERPROFILE\scoop" }
            $gitCandidates += Join-Path $scoopBase "apps\git\current\cmd\git.exe"
            $script:gitExe = $gitCandidates |
                Where-Object { Test-Path -LiteralPath $_ } |
                Select-Object -First 1
        }
        if (-not $script:gitExe) {
            throw "git.exe not found. Required for versioned vcpkg packages."
        }
        Write-Host "  --> git exe: $($script:gitExe)" -ForegroundColor DarkGray

        # --- Locate vcpkg root (VCPKG_ROOT env > walk up from exe) ---
        $script:vcpkgRoot = $null
        if ($env:VCPKG_ROOT -and (Test-Path (Join-Path $env:VCPKG_ROOT ".git"))) {
            $script:vcpkgRoot = $env:VCPKG_ROOT
        }
        if (-not $script:vcpkgRoot) {
            $candidate = Split-Path $vcpkgExe -Parent
            for ($i = 0; $i -lt 5 -and $candidate; $i++) {
                if (Test-Path (Join-Path $candidate ".git")) {
                    $script:vcpkgRoot = $candidate
                    break
                }
                $parent = Split-Path $candidate -Parent
                if ($parent -eq $candidate) { break }
                $candidate = $parent
            }
        }

        # --- Verify the git repo actually works ---
        $vcpkgGitOk = $false
        if ($script:vcpkgRoot) {
            try {
                Push-Location -LiteralPath $script:vcpkgRoot
                $null = (& "$($script:gitExe)" rev-parse HEAD 2>&1)
                $vcpkgGitOk = ($LASTEXITCODE -eq 0)
            } finally {
                Pop-Location
            }
        }

        if (-not $vcpkgGitOk) {
            # Scoop's .git is a file pointing to persist/vcpkg/.git which is
            # broken.  Clone vcpkg to a project-local directory so both our
            # baseline resolution AND vcpkg's internal git operations work.
            $localRepo = Join-Path $TopDir ".vcpkg-repo"

            if (Test-Path (Join-Path $localRepo ".git")) {
                # Check if existing repo is a partial (blobless) clone.
                # Blobless clones require network for checkout-index, which
                # fails inside MSBuild CUSTOMBUILD (getaddrinfo thread error).
                $isPartial = $false
                try {
                    Push-Location -LiteralPath $localRepo
                    $filter = (& "$($script:gitExe)" config --get remote.origin.partialclonefilter 2>&1)
                    $isPartial = ($LASTEXITCODE -eq 0 -and $filter)
                } finally {
                    Pop-Location
                }

                if ($isPartial) {
                    Write-Host "  --> Existing repo is a partial (blobless) clone; removing for full re-clone..." -ForegroundColor Yellow
                    Remove-Item -LiteralPath $localRepo -Recurse -Force
                } else {
                    Write-Host "  --> Updating local vcpkg repo..." -ForegroundColor DarkGray
                    try {
                        Push-Location -LiteralPath $localRepo
                        & "$($script:gitExe)" fetch origin --quiet 2>&1 | Out-Null
                    } finally {
                        Pop-Location
                    }
                }
            }

            if (-not (Test-Path (Join-Path $localRepo ".git"))) {
                Write-Host "  --> vcpkg git repo is broken (Scoop persist issue)." -ForegroundColor Yellow
                Write-Host "  --> Cloning vcpkg to .vcpkg-repo/ (one-time, may take a few minutes)..." -ForegroundColor Yellow
                $cloneOutput = & "$($script:gitExe)" clone --single-branch `
                    https://github.com/microsoft/vcpkg.git "$localRepo" 2>&1
                if ($LASTEXITCODE -ne 0) {
                    throw "Failed to clone vcpkg repository: $cloneOutput"
                }
                Write-Host "  --> Clone complete." -ForegroundColor Green
            }

            $script:vcpkgRoot = $localRepo
            $env:VCPKG_ROOT = $localRepo
            Write-Host "  --> VCPKG_ROOT redirected to: $localRepo" -ForegroundColor Green
        }

        # --- Extract baseline from the working repo ---
        try {
            Push-Location -LiteralPath $script:vcpkgRoot
            $script:vcpkgBaseline = (& "$($script:gitExe)" rev-parse HEAD 2>&1)
        } finally {
            Pop-Location
        }
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace("$($script:vcpkgBaseline)")) {
            throw "Failed to get baseline from vcpkg repo at $($script:vcpkgRoot)"
        }
        $script:vcpkgBaseline = "$($script:vcpkgBaseline)".Trim()

        Write-Host "  --> vcpkg root : $($script:vcpkgRoot)" -ForegroundColor DarkGray
        Write-Host "  --> baseline   : $($script:vcpkgBaseline.Substring(0,10))..." -ForegroundColor DarkGray
    }
}

# ================================================================================
# PHASE 2: vcpkg packages
# ================================================================================
foreach ($pkg in $vcpkgPackages) {
    $portName  = [string]$pkg.name
    $triplet   = if ([string]$pkg.triplet)  { [string]$pkg.triplet }  else { "x64-windows" }
    $version   = if ([string]$pkg.version)  { [string]$pkg.version }  else { $null }
    $features  = if ([string]$pkg.features) { [string]$pkg.features } else { $null }
    $extraArgs = if ([string]$pkg.args)     { [string]$pkg.args }     else { $null }

    # Build the vcpkg port spec (e.g. "minizip-ng[zlib,pkcrypt]")
    $portSpec = $portName
    if ($features) {
        $portSpec = "${portName}[$features]"
    }

    Write-Host "`n -> Processing vcpkg: $portSpec" -ForegroundColor Cyan

    try {
        # Fingerprint includes every field that could change in the manifest
        $fingerprint = "$portSpec|$triplet|$version|$extraArgs"
        $existing = Get-LedgerFingerprint -Ledger $ledger -Name $portName

        if (-not $Force -and $existing -eq $fingerprint) {
            Write-Host "  [SKIP] Already installed: $portName" -ForegroundColor DarkGray
            $skipCount++
            continue
        }

        Write-Host "  --> port spec : $portSpec" -ForegroundColor DarkGray
        Write-Host "  --> triplet   : $triplet" -ForegroundColor DarkGray
        if ($version)   { Write-Host "  --> version   : $version" -ForegroundColor DarkGray }
        if ($extraArgs) { Write-Host "  --> extra args: $extraArgs" -ForegroundColor DarkGray }

        $env:VCPKG_BUILD_TYPE = "release"
        $env:VCPKG_DEFAULT_TRIPLET = $triplet

        $vcpkgPackagesRoot = Join-Path $TopDir ".vcpkg-packages"
        $packageDirName    = ($portName -replace '[<>:"/\\|?*\[\],@]', '_')
        $packageInstallDir = Join-Path $vcpkgPackagesRoot $packageDirName

        $vcpkgTempRoot              = Join-Path $TopDir ".vcpkg-temp"
        $env:VCPKG_DOWNLOADS        = Join-Path $vcpkgTempRoot "downloads"
        $env:VCPKG_BUILDTREES_ROOT  = Join-Path $vcpkgTempRoot "buildtrees"

        New-Item -ItemType Directory -Path $packageInstallDir         -Force -ErrorAction SilentlyContinue | Out-Null
        New-Item -ItemType Directory -Path $env:VCPKG_DOWNLOADS       -Force -ErrorAction SilentlyContinue | Out-Null
        New-Item -ItemType Directory -Path $env:VCPKG_BUILDTREES_ROOT  -Force -ErrorAction SilentlyContinue | Out-Null

        # ---------------------------------------------------------------
        # Build vcpkg arguments
        # ---------------------------------------------------------------
        $vcpkgWorkDir = $null

        if ($version) {
            # Manifest mode: classic mode does not support version pinning.
            # Generate a temporary vcpkg.json with a version override.
            $manifestDir = Join-Path $vcpkgTempRoot "manifest-$packageDirName"
            New-Item -ItemType Directory -Path $manifestDir -Force -ErrorAction SilentlyContinue | Out-Null

            # Use the baseline resolved once in the pre-loop setup block
            $baseline = $script:vcpkgBaseline
            if (-not $baseline) {
                throw "No vcpkg baseline available. Pre-loop git validation may have been skipped."
            }

            # Parse port-version from "1.1.1n#1" -> version="1.1.1n", port-version=1
            $portVersion = $null
            $versionClean = $version
            if ($version -match '^(.+)#(\d+)$') {
                $versionClean = $matches[1]
                $portVersion  = [int]$matches[2]
            }

            # Build the dependency entry (include features if present)
            $dep = [ordered]@{ name = $portName }
            if ($features) {
                $dep["features"] = @($features -split ',\s*')
            }

            # Build the version override
            $override = [ordered]@{ name = $portName; version = $versionClean }
            if ($null -ne $portVersion) {
                $override["port-version"] = $portVersion
            }

            $vcpkgJson = [ordered]@{
                "builtin-baseline" = $baseline
                "dependencies"     = @($dep)
                "overrides"        = @($override)
            }
            $vcpkgJson | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $manifestDir "vcpkg.json") -Encoding UTF8

            Write-Host "  --> Generated vcpkg.json (version: $versionClean, port-version: $portVersion, baseline: $($baseline.Substring(0,10))...)" -ForegroundColor DarkGray

            $vcpkgArgs = @("install", "--triplet", $triplet)
            $vcpkgWorkDir = $manifestDir
        } else {
            $vcpkgArgs = @("install", "$portSpec", "--triplet", $triplet)
        }

        $vcpkgArgs += "--x-install-root=$packageInstallDir"
        $vcpkgArgs += "--x-buildtrees-root=$env:VCPKG_BUILDTREES_ROOT"
        $vcpkgArgs += "--downloads-root=$env:VCPKG_DOWNLOADS"

        # Force vcpkg to use our validated repo (overrides exe-location auto-detect)
        if ($script:vcpkgRoot) {
            $vcpkgArgs += "--vcpkg-root=$($script:vcpkgRoot)"
        }

        # Append any extra arguments from the manifest
        if ($extraArgs) {
            $vcpkgArgs += ($extraArgs -split '\s+')
        }

        # Resolve relative --overlay-triplets paths to absolute, based on $TopDir
        for ($i = 0; $i -lt $vcpkgArgs.Count; $i++) {
            if ($vcpkgArgs[$i] -like '--overlay-triplets=*') {
                $parts = $vcpkgArgs[$i] -split '=', 2
                if ($parts.Count -eq 2 -and -not [System.IO.Path]::IsPathRooted($parts[1])) {
                    $absPath = Join-Path $TopDir $parts[1]
                    $vcpkgArgs[$i] = "$($parts[0])=$absPath"
                }
            }
        }

        Write-Host "  --> vcpkg args: $vcpkgArgs" -ForegroundColor DarkGray

        $startParams = @{
            FilePath     = $vcpkgExe
            ArgumentList = $vcpkgArgs
            NoNewWindow  = $true
            Wait         = $true
            PassThru     = $true
        }
        if ($vcpkgWorkDir) {
            $startParams["WorkingDirectory"] = $vcpkgWorkDir
        }

        $vcpkgProc = Start-Process @startParams
        if ($vcpkgProc.ExitCode -ne 0) {
            throw "vcpkg install failed for $portSpec (exit code $($vcpkgProc.ExitCode))"
        }

        $vcpkgInstalled = Join-Path $packageInstallDir $triplet

        if (-not (Test-Path -LiteralPath $vcpkgInstalled)) {
            Write-Host "  --> ERROR: Package installation directory not found: $vcpkgInstalled" -ForegroundColor Red
            throw "vcpkg installed directory not found for $portSpec"
        }

        Write-Host "  --> Package installation: $vcpkgInstalled" -ForegroundColor Green
        Write-Host "  --> Copying to: $targetDir" -ForegroundColor Cyan

        $copyCount = 0

        # --- Copy headers ---
        $installedInclude = Join-Path $vcpkgInstalled "include"
        if (Test-Path -LiteralPath $installedInclude) {
            $includeTarget = Join-Path $targetDir "include"
            if (-not (Test-Path -LiteralPath $includeTarget)) {
                New-Item -ItemType Directory -Path $includeTarget -Force | Out-Null
            }

            $hasSubdirs = (Get-ChildItem -LiteralPath $installedInclude -Directory -ErrorAction SilentlyContinue).Count -gt 0

            if ($hasSubdirs) {
                $null = robocopy $installedInclude $includeTarget /E /NFL /NDL /NJH /NJS
            } else {
                $packageIncludeTarget = Join-Path $includeTarget $portName
                if (-not (Test-Path -LiteralPath $packageIncludeTarget)) {
                    New-Item -ItemType Directory -Path $packageIncludeTarget -Force | Out-Null
                }
                $null = robocopy $installedInclude $packageIncludeTarget /E /NFL /NDL /NJH /NJS
            }
            if ($LASTEXITCODE -ge 8) {
                Write-Host "  --> WARNING: robocopy failed with exit code $LASTEXITCODE" -ForegroundColor Yellow
            }

            # Count header files so header-only packages (e.g. glm) are not
            # falsely reported as "no files copied".
            $headerCount = (Get-ChildItem -LiteralPath $installedInclude -Recurse -File -ErrorAction SilentlyContinue).Count
            $copyCount += $headerCount
            Write-Host "  --> Copied $headerCount header file(s)" -ForegroundColor DarkGray
        }

        # --- Copy release libraries ---
        $installedLib = Join-Path $vcpkgInstalled "lib"
        if (Test-Path -LiteralPath $installedLib) {
            $libTarget = Join-Path (Join-Path $targetDir "lib") "release"
            if (-not (Test-Path -LiteralPath $libTarget)) {
                New-Item -ItemType Directory -Path $libTarget -Force | Out-Null
            }

            Write-Host "  --> Copying release libraries" -ForegroundColor DarkGray

            $libFiles = Get-ChildItem -LiteralPath $installedLib -Filter "*.lib" -ErrorAction SilentlyContinue
            foreach ($libFile in $libFiles) {
                Copy-Item -LiteralPath $libFile.FullName -Destination $libTarget -Force
                Write-Host "      - Copied $($libFile.Name)" -ForegroundColor DarkGray
                $copyCount++
            }
        }

        # --- Copy release runtime DLLs ---
        $installedBin = Join-Path $vcpkgInstalled "bin"
        if (Test-Path -LiteralPath $installedBin) {
            $binTarget = Join-Path (Join-Path $targetDir "bin") "release"
            if (-not (Test-Path -LiteralPath $binTarget)) {
                New-Item -ItemType Directory -Path $binTarget -Force | Out-Null
            }

            Write-Host "  --> Copying release DLLs" -ForegroundColor DarkGray

            $dllFiles = Get-ChildItem -LiteralPath $installedBin -Filter "*.dll" -ErrorAction SilentlyContinue
            foreach ($dllFile in $dllFiles) {
                Copy-Item -LiteralPath $dllFile.FullName -Destination $binTarget -Force
                Write-Host "      - Copied $($dllFile.Name)" -ForegroundColor DarkGray
                $copyCount++
            }
        }

        # --- Copy debug libraries if present ---
        # (only produced when VCPKG_BUILD_TYPE is not set to "release")
        $debugLibDir = Join-Path (Join-Path $vcpkgInstalled "debug") "lib"
        if (Test-Path -LiteralPath $debugLibDir) {
            $debugLibTarget = Join-Path (Join-Path $targetDir "lib") "debug"
            if (-not (Test-Path -LiteralPath $debugLibTarget)) {
                New-Item -ItemType Directory -Path $debugLibTarget -Force | Out-Null
            }

            Write-Host "  --> Copying debug libraries" -ForegroundColor DarkGray

            $debugLibFiles = Get-ChildItem -LiteralPath $debugLibDir -Filter "*.lib" -ErrorAction SilentlyContinue
            foreach ($debugLib in $debugLibFiles) {
                Copy-Item -LiteralPath $debugLib.FullName -Destination $debugLibTarget -Force
                Write-Host "      - Copied $($debugLib.Name)" -ForegroundColor DarkGray
                $copyCount++
            }
        }

        # --- Copy debug runtime DLLs if present ---
        $debugBinDir = Join-Path (Join-Path $vcpkgInstalled "debug") "bin"
        if (Test-Path -LiteralPath $debugBinDir) {
            $debugBinTarget = Join-Path (Join-Path $targetDir "bin") "debug"
            if (-not (Test-Path -LiteralPath $debugBinTarget)) {
                New-Item -ItemType Directory -Path $debugBinTarget -Force | Out-Null
            }

            Write-Host "  --> Copying debug DLLs" -ForegroundColor DarkGray

            $debugDllFiles = Get-ChildItem -LiteralPath $debugBinDir -Filter "*.dll" -ErrorAction SilentlyContinue
            foreach ($debugDll in $debugDllFiles) {
                Copy-Item -LiteralPath $debugDll.FullName -Destination $debugBinTarget -Force
                Write-Host "      - Copied $($debugDll.Name)" -ForegroundColor DarkGray
                $copyCount++
            }
        }

        if ($copyCount -eq 0) {
            throw "No files were copied from vcpkg installation! Check paths and installation."
        }

        # Record success in ledger
        Set-LedgerFingerprint -Ledger $ledger -Name $portName -Fingerprint $fingerprint
        Save-Ledger -Ledger $ledger -Path $ledgerPath

        Write-Host "  [OK] Installed via vcpkg: $portSpec ($triplet) - $copyCount files copied" -ForegroundColor Green
        $successCount++
    }
    catch {
        Write-Host "  [FAIL] vcpkg error: $($_.Exception.Message)" -ForegroundColor Red
        $failCount++
        $failedPackages += $portName
    }
    finally {
        Remove-Item Env:\VCPKG_BUILD_TYPE       -ErrorAction SilentlyContinue
        Remove-Item Env:\VCPKG_DEFAULT_TRIPLET  -ErrorAction SilentlyContinue
        Remove-Item Env:\VCPKG_DOWNLOADS        -ErrorAction SilentlyContinue
        Remove-Item Env:\VCPKG_BUILDTREES_ROOT  -ErrorAction SilentlyContinue
    }
}

# ================================================================================
# SUMMARY REPORT
# ================================================================================
Write-Host "`n========================================" -ForegroundColor Cyan
Write-Host "Package Processing Summary" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Successful: $successCount" -ForegroundColor Green
Write-Host "  Skipped:    $skipCount" -ForegroundColor Yellow
Write-Host "  Failed:     $failCount" -ForegroundColor $(if ($failCount -gt 0) { "Red" } else { "Green" })

if ($failedPackages.Count -gt 0) {
    Write-Host "`nFailed packages:" -ForegroundColor Yellow
    foreach ($failed in $failedPackages) {
        Write-Host "  - $failed" -ForegroundColor Red
    }
}
Write-Host "========================================`n" -ForegroundColor Cyan

# ================================================================================
# CREATE COMPLETION MARKER
# ================================================================================
# Create marker only when work was done and no failures occurred.
# ===============================================================================
if (($successCount + $failCount + $skipCount) -eq 0) {
    Write-Host "ERROR: No packages were processed. Check manifest parsing/filtering." -ForegroundColor Red
    exit 1
}

if ($failCount -eq 0) {
    $markerDir = Join-Path $TopDir "indra/build-vc145/prepare"
    if (-not (Test-Path $markerDir)) {
        New-Item -ItemType Directory -Path $markerDir -Force | Out-Null
    }
    $markerPath = Join-Path $markerDir "prebuilt"
    New-Item -ItemType File -Path $markerPath -Force | Out-Null
    Write-Host "Preparation complete. Marker created at: $markerPath" -ForegroundColor Green
    exit 0
}

Write-Host "Preparation incomplete due to package failures. Marker not created." -ForegroundColor Red
exit $failCount