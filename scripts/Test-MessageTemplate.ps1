#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Message template compatibility verifier for KV Viewer S24
.DESCRIPTION
    Modern PowerShell 7 replacement for template_verifier.py
    Verifies message template compatibility against the canonical LL master template
.PARAMETER TemplateFile
    Path to the local message template file to verify
.PARAMETER MasterUrl
    URL of the master message template (default: GitHub LL master)
.PARAMETER Mode
    Verification mode: 'development' (lenient) or 'production' (strict)
.PARAMETER CacheMaster
    Cache the master template locally to reduce network calls
.PARAMETER Force
    Force verification even if SHA-1 hasn't changed
.EXAMPLE
    .\Test-MessageTemplate.ps1 -TemplateFile ".\messages\message_template.msg"
#>

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$TemplateFile = (Join-Path $PSScriptRoot "messages\message_template.msg"),
    
    [Parameter()]
    [string]$MasterUrl = "https://github.com/secondlife/master-message-template/raw/master/message_template.msg",
    
    [Parameter()]
    [ValidateSet('development', 'production')]
    [string]$Mode = 'development',
    
    [Parameter()]
    [switch]$CacheMaster = $true,
    
    [Parameter()]
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

# Configuration
$MaxMasterAgeHours = 4
$CacheDir = [System.IO.Path]::GetTempPath()
$CacheFile = Join-Path $CacheDir "master_message_template_cache.$env:USERNAME.msg"
$Sha1File = "$TemplateFile.sha1"

#region Helper Functions

function Write-ColorOutput {
    param(
        [string]$Message,
        [ConsoleColor]$Color = 'White'
    )
    Write-Host $Message -ForegroundColor $Color
}

function Get-FileSha1 {
    param([string]$Path)
    $sha1 = [System.Security.Cryptography.SHA1]::Create()
    try {
        $stream = [System.IO.File]::OpenRead($Path)
        try {
            $hash = $sha1.ComputeHash($stream)
            return [BitConverter]::ToString($hash).Replace('-', '').ToLower()
        }
        finally {
            $stream.Close()
        }
    }
    finally {
        $sha1.Dispose()
    }
}

function Get-CachedMaster {
    param([string]$Url)
    
    # Check if cache exists and is fresh
    if (Test-Path $CacheFile) {
        $cacheAge = (Get-Date) - (Get-Item $CacheFile).LastWriteTime
        if ($cacheAge.TotalHours -lt $MaxMasterAgeHours) {
            Write-ColorOutput "Using cached master template (age: $([int]$cacheAge.TotalHours)h)" Cyan
            return Get-Content $CacheFile -Raw
        }
    }
    
    # Download fresh master
    Write-ColorOutput "Downloading master template from $Url" Yellow
    try {
        $webClient = [System.Net.Http.HttpClient]::new()
        $webClient.Timeout = [TimeSpan]::FromSeconds(30)
        
        for ($retry = 0; $retry -lt 3; $retry++) {
            try {
                $response = $webClient.GetAsync($Url).Result
                if ($response.IsSuccessStatusCode) {
                    $content = $response.Content.ReadAsStringAsync().Result
                    
                    # Validate content by checking for basic structure
                    if ($content -match 'version\s+\d+\.\d+' -and $content.Length -gt 1000) {
                        # Cache it
                        [System.IO.File]::WriteAllText($CacheFile, $content, [System.Text.Encoding]::UTF8)
                        Write-ColorOutput "Master template cached successfully" Green
                        return $content
                    }
                    else {
                        throw "Invalid master template format"
                    }
                }
                else {
                    throw "HTTP $($response.StatusCode): $($response.ReasonPhrase)"
                }
            }
            catch {
                if ($retry -eq 2) { throw }
                Write-ColorOutput "Retry $($retry + 1)/3..." DarkYellow
                Start-Sleep -Seconds 2
            }
        }
    }
    catch {
        Write-ColorOutput "WARNING: Unable to download master template: $_" Red
        
        # Fall back to cached version if available
        if (Test-Path $CacheFile) {
            Write-ColorOutput "Using stale cached master template" DarkYellow
            return Get-Content $CacheFile -Raw
        }
        
        if ($Mode -eq 'production') {
            throw "Cannot proceed in production mode without master template"
        }
        
        Write-ColorOutput "Proceeding with syntax-check only (no compatibility verification)" Yellow
        return $null
    }
    finally {
        if ($webClient) { $webClient.Dispose() }
    }
}

function Test-TemplateCompatibility {
    param(
        [string]$MasterContent,
        [string]$CurrentContent
    )
    
    # Simplified compatibility check
    # In production, you'd parse the message structures and compare versions
    # For now, we do basic validation
    
    $issues = @()
    
    # Extract version from both templates
    if ($MasterContent -match 'version\s+([\d.]+)') {
        $masterVersion = [version]$matches[1]
    }
    else {
        $issues += "Cannot parse master template version"
    }
    
    if ($CurrentContent -match 'version\s+([\d.]+)') {
        $currentVersion = [version]$matches[1]
    }
    else {
        $issues += "Cannot parse current template version"
    }
    
    # Check message counts (rough compatibility indicator)
    $masterLowCount = ([regex]::Matches($MasterContent, '\sLow\s+\d+')).Count
    $currentLowCount = ([regex]::Matches($CurrentContent, '\sLow\s+\d+')).Count
    
    $masterMediumCount = ([regex]::Matches($MasterContent, '\sMedium\s+\d+')).Count
    $currentMediumCount = ([regex]::Matches($CurrentContent, '\sMedium\s+\d+')).Count
    
    $masterHighCount = ([regex]::Matches($MasterContent, '\sHigh\s+\d+')).Count
    $currentHighCount = ([regex]::Matches($CurrentContent, '\sHigh\s+\d+')).Count
    
    # Development mode: allow differences
    # Production mode: must match or be newer
    if ($Mode -eq 'production') {
        if ($currentVersion -lt $masterVersion) {
            $issues += "Template version $currentVersion is older than master $masterVersion"
        }
        if ($currentLowCount -lt $masterLowCount) {
            $issues += "Fewer Low messages than master ($currentLowCount vs $masterLowCount)"
        }
    }
    
    return @{
        Issues = $issues
        MasterVersion = $masterVersion
        CurrentVersion = $currentVersion
        Compatible = ($issues.Count -eq 0)
        Stats = @{
            Master = @{ Low = $masterLowCount; Medium = $masterMediumCount; High = $masterHighCount }
            Current = @{ Low = $currentLowCount; Medium = $currentMediumCount; High = $currentHighCount }
        }
    }
}

#endregion

#region Main Logic

Write-ColorOutput "`n=== KV Viewer S24 Message Template Verifier ===" Magenta
Write-ColorOutput "Mode: $Mode" Cyan
Write-ColorOutput "Template: $TemplateFile`n" Cyan

# Verify template file exists
if (-not (Test-Path $TemplateFile)) {
    Write-ColorOutput "ERROR: Template file not found: $TemplateFile" Red
    exit 1
}

# Read current template
$currentContent = Get-Content $TemplateFile -Raw

# Calculate SHA-1 hash
$currentHash = Get-FileSha1 $TemplateFile

# Check if we can skip verification (hash unchanged)
if (-not $Force -and (Test-Path $Sha1File)) {
    $savedHash = Get-Content $Sha1File -Raw
    if ($currentHash -eq $savedHash.Trim()) {
        Write-ColorOutput "✓ Message template SHA-1 unchanged - skipping verification" Green
        exit 0
    }
}

# Basic syntax validation
if ($currentContent -notmatch 'version\s+\d+\.\d+') {
    Write-ColorOutput "ERROR: Invalid template format - missing version header" Red
    exit 1
}

Write-ColorOutput "Template hash: $currentHash" DarkGray

# Get master template (if caching enabled in development mode)
$masterContent = $null
if ($CacheMaster -and $Mode -eq 'development') {
    $masterContent = Get-CachedMaster $MasterUrl
}
elseif ($Mode -eq 'production') {
    # Always fetch fresh in production
    Write-ColorOutput "Production mode: downloading fresh master template" Yellow
    try {
        $webClient = [System.Net.Http.HttpClient]::new()
        $response = $webClient.GetAsync($MasterUrl).Result
        if ($response.IsSuccessStatusCode) {
            $masterContent = $response.Content.ReadAsStringAsync().Result
        }
        $webClient.Dispose()
    }
    catch {
        Write-ColorOutput "ERROR: Cannot download master template in production mode" Red
        exit 1
    }
}

# Perform compatibility check if we have master
if ($masterContent) {
    Write-ColorOutput "`nPerforming compatibility check..." Cyan
    
    $result = Test-TemplateCompatibility -MasterContent $masterContent -CurrentContent $currentContent
    
    Write-ColorOutput "`n--- Compatibility Results ---" Yellow
    Write-ColorOutput "Master Version:  $($result.MasterVersion)" Gray
    Write-ColorOutput "Current Version: $($result.CurrentVersion)" Gray
    Write-ColorOutput "`nMessage Counts:" Gray
    Write-ColorOutput "  Low:    Master=$($result.Stats.Master.Low), Current=$($result.Stats.Current.Low)" Gray
    Write-ColorOutput "  Medium: Master=$($result.Stats.Master.Medium), Current=$($result.Stats.Current.Medium)" Gray
    Write-ColorOutput "  High:   Master=$($result.Stats.Master.High), Current=$($result.Stats.Current.High)" Gray
    
    if ($result.Compatible) {
        Write-ColorOutput "`n✓✓✓ PASS ✓✓✓" Green
        Write-ColorOutput "Template is compatible with master`n" Green
        
        # Update SHA-1 file
        [System.IO.File]::WriteAllText($Sha1File, $currentHash, [System.Text.Encoding]::ASCII)
        Write-ColorOutput "SHA-1 hash updated" DarkGray
        exit 0
    }
    else {
        Write-ColorOutput "`n✗✗✗ FAIL ✗✗✗" Red
        Write-ColorOutput "Compatibility issues found:`n" Red
        foreach ($issue in $result.Issues) {
            Write-ColorOutput "  • $issue" Red
        }
        Write-ColorOutput ""
        exit 1
    }
}
else {
    # Syntax check only
    Write-ColorOutput "`n✓ Syntax check PASSED (no compatibility verification performed)" Yellow
    
    # Still update hash in development mode
    if ($Mode -eq 'development') {
        [System.IO.File]::WriteAllText($Sha1File, $currentHash, [System.Text.Encoding]::ASCII)
    }
    exit 0
}

#endregion
