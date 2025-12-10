# build.ps1 - M110A Modem Build Script with Version Management
# 
# Usage: .\build.ps1 [-Target <server|test|all|release>] [-Increment <major|minor|patch>] [-Clean]
#        .\build.ps1 -Prerelease "rc.1"    # Create prerelease version 1.2.0-rc.1
#        .\build.ps1 -Target release       # Create full release package with signing
#        .\build.ps1 -Target release -Publish  # Build, sign, and publish to GitHub
#
# Workflow:
#   Development:  .\build.ps1                      # Increments BUILD_NUMBER only
#   Bug fix:      .\build.ps1 -Increment patch     # 1.2.0 -> 1.2.1
#   Feature:      .\build.ps1 -Increment minor     # 1.2.1 -> 1.3.0  
#   Breaking:     .\build.ps1 -Increment major     # 1.3.0 -> 2.0.0
#   Prerelease:   .\build.ps1 -Prerelease "rc.1"   # 1.2.0-rc.1
#   Publish:      .\build.ps1 -Target release -Publish  # Full release to GitHub
#
# See docs/DEVELOPMENT.md for full workflow documentation

param(
    [string]$Target = "all",
    [string]$Increment = "",
    [string]$Prerelease = "",
    [switch]$Clean = $false,
    [switch]$NoSign = $false,
    [switch]$Publish = $false,
    [switch]$Draft = $false,
    [switch]$Help = $false
)

$ErrorActionPreference = "Stop"
$ProjectRoot = $PSScriptRoot
$VersionFile = Join-Path $ProjectRoot "api\version.h"

# GitHub repository info
$script:GitHubOwner = "Alex-Pennington"
$script:GitHubRepo = "pennington_m110a_demod"

# Detect platform for artifact naming
$script:Platform = if ($env:PROCESSOR_ARCHITECTURE -eq "AMD64") { "x64" } else { "x86" }
$script:OS = "windows"

# Show help if requested
if ($Help) {
    Write-Host @"
M110A Modem Build Script
========================

USAGE:
    .\build.ps1 [options]

OPTIONS:
    -Target <target>      Build target: server, test, all, release (default: all)
    -Increment <type>     Version increment: major, minor, patch
    -Prerelease <tag>     Add prerelease tag (e.g., "alpha.1", "beta.2", "rc.1")
    -Clean                Clean build directory before building
    -NoSign               Skip GPG signing for release builds
    -Publish              Publish release to GitHub (requires gh CLI)
    -Draft                Create GitHub release as draft (use with -Publish)
    -Help                 Show this help message

EXAMPLES:
    .\build.ps1                          # Build all, increment build number
    .\build.ps1 -Target server           # Build server only
    .\build.ps1 -Increment patch         # Bug fix release (1.2.0 -> 1.2.1)
    .\build.ps1 -Increment minor         # Feature release (1.2.0 -> 1.3.0)
    .\build.ps1 -Prerelease "rc.1"       # Prerelease (1.2.0-rc.1)
    .\build.ps1 -Target release          # Full release with signing
    .\build.ps1 -Target release -NoSign  # Release without GPG signing
    .\build.ps1 -Target release -Publish # Build and publish to GitHub
    .\build.ps1 -Target release -Publish -Draft  # Publish as draft release

CONVENTIONAL COMMITS (recommended for -Increment):
    fix:      -> -Increment patch   (bug fixes)
    feat:     -> -Increment minor   (new features)
    feat!:    -> -Increment major   (breaking changes)
    BREAKING CHANGE: in body -> -Increment major

See docs/DEVELOPMENT.md for full workflow documentation.
"@
    exit 0
}

# ============================================================
# Version Management
# ============================================================

function Get-CurrentVersion {
    if (-not (Test-Path $VersionFile)) {
        return @{ Major = 1; Minor = 0; Patch = 0; Build = 0; Prerelease = "" }
    }
    
    $content = Get-Content $VersionFile -Raw
    $major = if ($content -match 'VERSION_MAJOR = (\d+)') { [int]$Matches[1] } else { 1 }
    $minor = if ($content -match 'VERSION_MINOR = (\d+)') { [int]$Matches[1] } else { 0 }
    $patch = if ($content -match 'VERSION_PATCH = (\d+)') { [int]$Matches[1] } else { 0 }
    $build = if ($content -match 'BUILD_NUMBER = (\d+)') { [int]$Matches[1] } else { 0 }
    $prerelease = if ($content -match 'VERSION_PRERELEASE = "([^"]*)"') { $Matches[1] } else { "" }
    
    return @{ Major = $major; Minor = $minor; Patch = $patch; Build = $build; Prerelease = $prerelease }
}

function Get-VersionString {
    param($Version, [switch]$Full)
    
    $base = "v$($Version.Major).$($Version.Minor).$($Version.Patch)"
    if ($Version.Prerelease) {
        $base += "-$($Version.Prerelease)"
    }
    if ($Full) {
        $base += "+build.$($Version.Build)"
    }
    return $base
}

function Update-Version {
    param($Version, $IncrementType, $PrereleaseTag = "")
    
    switch ($IncrementType) {
        "major" { 
            $Version.Major++
            $Version.Minor = 0
            $Version.Patch = 0
            $Version.Prerelease = ""  # Clear prerelease on version bump
        }
        "minor" {
            $Version.Minor++
            $Version.Patch = 0
            $Version.Prerelease = ""  # Clear prerelease on version bump
        }
        "patch" {
            $Version.Patch++
            $Version.Prerelease = ""  # Clear prerelease on version bump
        }
    }
    
    # Set prerelease tag if provided
    if ($PrereleaseTag) {
        $Version.Prerelease = $PrereleaseTag
    }
    
    # Always increment build number
    $Version.Build++
    
    return $Version
}

function Write-VersionHeader {
    param($Version)
    
    # Get git info
    $gitCommit = (git rev-parse --short HEAD 2>$null) -replace '\s',''
    if (-not $gitCommit) { $gitCommit = "unknown" }
    
    $gitBranch = (git rev-parse --abbrev-ref HEAD 2>$null) -replace '\s',''
    if (-not $gitBranch) { $gitBranch = "unknown" }
    
    $buildDate = Get-Date -Format "yyyy-MM-dd"
    $buildTime = Get-Date -Format "HH:mm:ss"
    
    $header = @"
/**
 * @file version.h
 * @brief Auto-generated version information
 * 
 * M110A Modem - MIL-STD-188-110A Compatible HF Modem
 * Copyright (c) 2024-2025 Alex Pennington
 * Email: alex.pennington@organicengineer.com
 * 
 * DO NOT EDIT MANUALLY - Generated by build.ps1
 */

#ifndef M110A_VERSION_H
#define M110A_VERSION_H

#include <string>

namespace m110a {

// Semantic version
constexpr int VERSION_MAJOR = $($Version.Major);
constexpr int VERSION_MINOR = $($Version.Minor);
constexpr int VERSION_PATCH = $($Version.Patch);

// Prerelease tag (empty for stable releases)
constexpr const char* VERSION_PRERELEASE = "$($Version.Prerelease)";

// Build information (auto-generated)
constexpr int BUILD_NUMBER = $($Version.Build);
constexpr const char* GIT_COMMIT = "$gitCommit";
constexpr const char* GIT_BRANCH = "$gitBranch";
constexpr const char* BUILD_DATE = "$buildDate";
constexpr const char* BUILD_TIME = "$buildTime";

/// Get version string (e.g., "1.2.0" or "1.2.0-rc.1")
inline std::string version() {
    std::string v = std::to_string(VERSION_MAJOR) + "." +
                    std::to_string(VERSION_MINOR) + "." +
                    std::to_string(VERSION_PATCH);
    if (VERSION_PRERELEASE[0] != '\0') {
        v += "-";
        v += VERSION_PRERELEASE;
    }
    return v;
}

/// Get full version string with branch and build info
/// Format: "1.2.0-rc.1 (turbo) build.42.abc1234"
inline std::string version_full() {
    return version() + " (" + GIT_BRANCH + ") build." + 
           std::to_string(BUILD_NUMBER) + "." + GIT_COMMIT;
}

/// Get build info string for reports
/// Format: "Build 42 (abc1234) turbo 2025-12-08 07:30:00"
inline std::string build_info() {
    return std::string("Build ") + std::to_string(BUILD_NUMBER) + 
           " (" + GIT_COMMIT + ") " + GIT_BRANCH + " " + BUILD_DATE + " " + BUILD_TIME;
}

/// Get version for help/usage display
/// Format: "v1.2.0-turbo+42"
inline std::string version_short() {
    return "v" + version() + "-" + GIT_BRANCH + "+" + std::to_string(BUILD_NUMBER);
}

/// Get detailed version info for headers
/// Format: "M110A Modem v1.2.0 (turbo branch, build 42, commit abc1234)"
inline std::string version_header() {
    return std::string("M110A Modem v") + version() + " (" + GIT_BRANCH + 
           " branch, build " + std::to_string(BUILD_NUMBER) + ", commit " + GIT_COMMIT + ")";
}

/// Get copyright notice
inline std::string copyright_notice() {
    return "Copyright (c) 2024-2025 Alex Pennington\n"
           "Email: alex.pennington@organicengineer.com\n"
           "MIL-STD-188-110A Compatible HF Modem";
}

/// Get EULA acceptance notice
inline std::string eula_notice() {
    return "By using this software, you agree to the End User License Agreement.\n"
           "See EULA.md for complete terms and conditions.\n"
           "License required - Contact: alex.pennington@organicengineer.com";
}

} // namespace m110a

#endif // M110A_VERSION_H
"@
    
    Set-Content -Path $VersionFile -Value $header -NoNewline
    Write-Host "Version: $($Version.Major).$($Version.Minor).$($Version.Patch)+build.$($Version.Build).$gitCommit" -ForegroundColor Cyan
}

# ============================================================
# Build Functions
# ============================================================

$CXX = "g++"
$CXXFLAGS = "-std=c++17 -O2 -I. -Isrc -D_USE_MATH_DEFINES"
$LDFLAGS = "-static -lws2_32"

function Build-Server {
    Write-Host "`n=== Building Server ===" -ForegroundColor Yellow
    
    # Kill existing server
    Get-Process m110a_server -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500
    
    $sources = @(
        "server/main.cpp",
        "server/brain_server.cpp",
        "api/modem_tx.cpp",
        "api/modem_rx.cpp"
    )
    
    $cmd = "$CXX $CXXFLAGS -o server/m110a_server.exe $($sources -join ' ') $LDFLAGS"
    Write-Host $cmd -ForegroundColor DarkGray
    
    Invoke-Expression $cmd
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Server built successfully" -ForegroundColor Green
    } else {
        throw "Server build failed"
    }
}

function Build-ExhaustiveTest {
    Write-Host "`n=== Building Exhaustive Test ===" -ForegroundColor Yellow
    
    $cmd = "$CXX $CXXFLAGS -o server/exhaustive_server_test.exe server/exhaustive_server_test.cpp $LDFLAGS"
    Write-Host $cmd -ForegroundColor DarkGray
    
    Invoke-Expression $cmd
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Exhaustive test built successfully" -ForegroundColor Green
    } else {
        throw "Exhaustive test build failed"
    }
}

function Build-UnifiedTest {
    Write-Host "`n=== Building Unified Exhaustive Test ===" -ForegroundColor Yellow
    
    $sources = @(
        "test/exhaustive_test_unified.cpp",
        "api/modem_tx.cpp",
        "api/modem_rx.cpp",
        "src/io/pcm_file.cpp"
    )
    
    $cmd = "$CXX $CXXFLAGS -o test/exhaustive_test.exe $($sources -join ' ') $LDFLAGS"
    Write-Host $cmd -ForegroundColor DarkGray
    
    Invoke-Expression $cmd
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Unified test built successfully" -ForegroundColor Green
    } else {
        throw "Unified test build failed"
    }
}

function Build-UnitTests {
    Write-Host "`n=== Building Unit Tests ===" -ForegroundColor Yellow
    
    $cmd = "$CXX $CXXFLAGS -o test/test_channel_params.exe test/test_channel_params.cpp $LDFLAGS"
    Write-Host $cmd -ForegroundColor DarkGray
    
    Invoke-Expression $cmd
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Unit tests built successfully" -ForegroundColor Green
    } else {
        throw "Unit test build failed"
    }
}

function Build-TestGui {
    Write-Host "`n=== Building Test GUI Server ===" -ForegroundColor Yellow
    
    $cmd = "$CXX $CXXFLAGS -o test/test_gui.exe test/test_gui_server.cpp $LDFLAGS -lshell32"
    Write-Host $cmd -ForegroundColor DarkGray
    
    Invoke-Expression $cmd
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Test GUI built successfully" -ForegroundColor Green
    } else {
        throw "Test GUI build failed"
    }
}

function Build-LicenseGen {
    Write-Host "`n=== Building License Generator ===" -ForegroundColor Yellow
    
    # Create tools directory if it doesn't exist
    if (-not (Test-Path "tools")) {
        New-Item -ItemType Directory -Path "tools" | Out-Null
    }
    
    $cmd = "$CXX $CXXFLAGS -o tools/license_gen.exe tools/license_gen.cpp $LDFLAGS"
    Write-Host $cmd -ForegroundColor DarkGray
    
    Invoke-Expression $cmd
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host "License generator built successfully" -ForegroundColor Green
    } else {
        throw "License generator build failed"
    }
}

function Build-MelpeVocoder {
    Write-Host "`n=== Building MELPe Vocoder ===" -ForegroundColor Yellow
    
    $melpeBuildDir = "src/melpe_core/build"
    
    # Create build directory if needed
    if (-not (Test-Path $melpeBuildDir)) {
        New-Item -ItemType Directory -Path $melpeBuildDir | Out-Null
    }
    
    # Configure with CMake
    Push-Location $melpeBuildDir
    try {
        $cmakeResult = cmake .. -G "MinGW Makefiles" 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Host "CMake configuration failed" -ForegroundColor Red
            Write-Host $cmakeResult
            throw "MELPe CMake failed"
        }
        
        # Build melpe_vocoder target
        $buildResult = cmake --build . --target melpe_vocoder 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Host "MELPe build failed" -ForegroundColor Red
            Write-Host $buildResult
            throw "MELPe build failed"
        }
        
        Write-Host "MELPe vocoder built successfully" -ForegroundColor Green
    }
    finally {
        Pop-Location
    }
}

function Build-Release {
    Write-Host "`n=== Building Release Package ===" -ForegroundColor Cyan
    
    # Build all release components
    Build-Server
    Build-UnifiedTest
    Build-TestGui
    Build-LicenseGen
    Build-MelpeVocoder
    
    # Create release directory structure
    $releaseDir = "release"
    $binDir = "$releaseDir/bin"
    $docsDir = "$releaseDir/docs"
    $examplesDir = "$releaseDir/examples"
    
    Write-Host "`nCreating release directory structure..." -ForegroundColor Yellow
    
    if (Test-Path $releaseDir) {
        Remove-Item $releaseDir -Recurse -Force
    }
    
    New-Item -ItemType Directory -Path $binDir | Out-Null
    New-Item -ItemType Directory -Path $docsDir | Out-Null
    New-Item -ItemType Directory -Path $examplesDir | Out-Null
    
    # Copy executables
    Write-Host "Copying executables..." -ForegroundColor Yellow
    Copy-Item "server/m110a_server.exe" $binDir
    Copy-Item "test/exhaustive_test.exe" $binDir
    Copy-Item "test/test_gui.exe" $binDir
    Copy-Item "src/melpe_core/build/melpe_vocoder.exe" $binDir
    
    # Copy documentation
    Write-Host "Copying documentation..." -ForegroundColor Yellow
    Copy-Item "README.md" $releaseDir
    Copy-Item "phoenixnestmodem_eula.md" "$releaseDir/EULA.md"
    Copy-Item "docs/PROTOCOL.md" $docsDir
    Copy-Item "docs/CHANNEL_SIMULATION.md" $docsDir
    
    # Copy reference PCM files
    Write-Host "Copying reference PCM files..." -ForegroundColor Yellow
    Copy-Item "refrence_pcm" $examplesDir -Recurse
    
    # Copy MELPe test audio files
    Write-Host "Copying MELPe test audio files..." -ForegroundColor Yellow
    $melpeTestAudioSrc = "src/melpe_core/test_audio"
    if (Test-Path $melpeTestAudioSrc) {
        Copy-Item $melpeTestAudioSrc "$examplesDir/melpe_test_audio" -Recurse
    } else {
        Write-Host "  MELPe test audio not found - run src/melpe_core/download_test_audio.ps1 first" -ForegroundColor DarkYellow
    }

    # Create license template
    Write-Host "Creating license template..." -ForegroundColor Yellow
    $licenseTemplate = @"
# M110A Modem License File
# 
# To activate this software, go to https://www.organicengineer.com/projects to obtain a license key.
# 
# Steps to activate:
# 1. Run: license_gen.exe --hwid
# 2. Visit https://www.organicengineer.com/projects and submit the hardware ID
# 3. Replace this file with the license.key file you receive
#
# Format: CUSTOMER-HWID-EXPIRY-CHECKSUM
# Example: ACME01-A3B4C5D6-20261231-9F8E7D6C
#
# For support: alex.pennington@organicengineer.com
"@
    Set-Content -Path "$releaseDir/license.key.template" -Value $licenseTemplate
    
    # Create installation guide
    Write-Host "Creating installation guide..." -ForegroundColor Yellow
        $installGuide = @"
# M110A Modem Installation Guide

## System Requirements
- Windows 10/11 (64-bit)
- 4 GB RAM minimum
- Network connection for TCP/IP server mode

## Installation Steps

1. **Extract the release package** to your desired location

2. **Activate your license**
     - Open a command prompt in the release directory
     - Run: ``license_gen.exe --hwid``
     - Go to https://www.organicengineer.com/projects to obtain a license key using your hardware ID
     - You will receive a ``license.key`` file
     - Place the ``license.key`` file in the ``bin/`` directory

3. **Verify installation**
     - Navigate to the ``bin/`` directory
     - Run: ``m110a_server.exe``
     - You should see license information and server startup

## Components

### bin/m110a_server.exe
TCP/IP server for modem operations.

Ports:
- TCP 4998: Data port
- TCP 4999: Control port
- UDP 5000: Discovery (optional)

Usage:
```
cd bin
m110a_server.exe
```

### bin/exhaustive_test.exe
Comprehensive test suite for all modem modes.

Usage:
```
cd bin
exhaustive_test.exe [options]

Options:
    --server          Use TCP/IP server backend (requires m110a_server.exe running)
    --mode MODE       Test specific mode (e.g., 600S, 1200L)
    --progressive     Run progressive SNR/frequency/multipath tests
    --csv FILE        Output results to CSV
    --help            Show all options
```

### bin/test_gui.exe
Web-based graphical interface for running tests.

Usage:
```
cd bin
test_gui.exe
```

Opens http://localhost:8080 in your browser with a full GUI for test configuration.

## Quick Start

1. Start the server:
```
cd bin
m110a_server.exe
```

2. In another terminal, run a test:
```
cd bin
exhaustive_test.exe
```

## Reference PCM Files

The ``examples/refrence_pcm/`` directory contains reference waveforms for all supported modes:
- 75 BPS (BPSK, Long/Short interleaver)
- 150 BPS (BPSK, Long/Short)
- 300 BPS (QPSK, Long/Short)
- 600 BPS (QPSK, Long/Short)
- 1200 BPS (8-PSK, Long/Short)
- 2400 BPS (8-PSK, Long/Short)

Each PCM file includes metadata in a JSON file.

## Known Limitations

- **AFC Range**: Automatic Frequency Control (AFC) is designed for ±2 Hz frequency offset
    - Offsets beyond ±5 Hz may result in reduced demodulation performance
    - For operation in challenging HF conditions with larger Doppler shifts, ensure transmitter/receiver frequency stability

## Troubleshooting

### "LICENSE REQUIRED" message
- Ensure ``license.key`` is in the same directory as the executable
- Verify license has not expired
- Check that you're running on the same hardware the license was issued for

### Server fails to start
- Check ports 4998/4999 are not in use: ``netstat -an | findstr "4998 4999"``
- Verify license is valid
- Check firewall settings

### Test failures
- Ensure reference PCM files are in correct location
- Check channel simulation parameters
- Review test_reports/ directory for detailed logs

## Support

For technical support or licensing inquiries:
- Website: https://www.organicengineer.com/projects
- Email: alex.pennington@organicengineer.com
- Documentation: See docs/ directory

## Version

See ``RELEASE_INFO.txt`` for build and version information.
"@
    Set-Content -Path "$releaseDir/INSTALL.md" -Value $installGuide
    
    # Create quick start guide
    Write-Host "Creating quick start guide..." -ForegroundColor Yellow
    $quickStart = @"
# M110A Modem Quick Start

## First Time Setup

1. Get your hardware ID:
   ``````
   license_gen.exe --hwid
   ``````

2. Go to https://www.organicengineer.com/projects to obtain a license key using your hardware ID

3. Place the ``license.key`` file in the ``bin/`` directory

## Running the Server

``````
cd bin
m110a_server.exe
``````

The server listens on TCP 4998 (data), TCP 4999 (control), and UDP 5000 (discovery).

## Running Tests

Basic test (ideal channel):
``````
cd bin
exhaustive_test.exe
``````

Test with channel simulation:
``````
exhaustive_test.exe --channel poor
``````

Server-based test:
``````
# Terminal 1
m110a_server.exe

# Terminal 2
exhaustive_test.exe --server
``````

## Next Steps

- Read ``INSTALL.md`` for detailed installation instructions
- Review ``docs/API.md`` for programming interface
- Check ``examples/refrence_pcm/`` for reference waveforms
- See ``docs/CHANNEL_SIMULATION.md`` for channel modeling details

## License

See ``EULA.md`` for license terms and conditions. Go to https://www.organicengineer.com/projects to obtain a license key.
"@
    Set-Content -Path "$releaseDir/QUICKSTART.md" -Value $quickStart
    
    # Create release info
    $version = Get-CurrentVersion
    $versionDisplay = "$($version.Major).$($version.Minor).$($version.Patch)"
    if ($version.Prerelease) {
        $versionDisplay += "-$($version.Prerelease)"
    }
    $releaseInfo = @"
M110A Modem Release Package
Version: $versionDisplay
Build: $($version.Build)
Platform: $($script:OS)-$($script:Platform)
Date: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")

MIL-STD-188-110A Compatible HF Modem
(c) 2024-2025 Alex Pennington

CONTENTS:
- bin/m110a_server.exe       - TCP/IP modem server
- bin/exhaustive_test.exe    - Comprehensive test suite
- bin/test_gui.exe           - Web-based test GUI (http://localhost:8080)
- docs/                      - API and protocol documentation
- examples/refrence_pcm/     - Reference waveform files
- INSTALL.md                 - Installation instructions
- QUICKSTART.md              - Quick start guide
- EULA.md                    - End User License Agreement
- README.md                  - Project overview

SYSTEM REQUIREMENTS:
- Windows 10/11 (64-bit)
- 4 GB RAM minimum
- Valid license key required

LICENSING:
This software requires activation with a hardware-locked license key.
Go to https://www.organicengineer.com/projects to obtain a license key.

SUPPORT:
alex.pennington@organicengineer.com
"@
    Set-Content -Path "$releaseDir/RELEASE_INFO.txt" -Value $releaseInfo
    
    Write-Host "`n=== Release Package Complete ===" -ForegroundColor Green
    Write-Host "Location: $releaseDir/" -ForegroundColor Cyan
    Write-Host "Contents:" -ForegroundColor Yellow
    Get-ChildItem $releaseDir -Recurse | ForEach-Object {
        $relativePath = $_.FullName.Replace("$(Get-Location)\$releaseDir\", "")
        if ($_.PSIsContainer) {
            Write-Host "  [DIR]  $relativePath" -ForegroundColor Blue
        } else {
            $size = "{0:N0} KB" -f ($_.Length / 1KB)
            Write-Host "  [FILE] $relativePath ($size)" -ForegroundColor Gray
        }
    }

    # Archive release package and generate checksum manifest
    Write-Host "`nCreating release archive..." -ForegroundColor Yellow
    $versionString = "$($version.Major).$($version.Minor).$($version.Patch)"
    if ($version.Prerelease) {
        $versionString += "-$($version.Prerelease)"
    }
    $zipBaseName = "M110A_Modem_v$versionString`_Build$($version.Build)_$($script:OS)-$($script:Platform)"
    $zipFileName = "$zipBaseName.zip"
    $zipPath = Join-Path $ProjectRoot $zipFileName
    if (Test-Path $zipPath) {
        Remove-Item $zipPath -Force
    }
    Compress-Archive -Path "$releaseDir\*" -DestinationPath $zipPath -Force
    Write-Host "Archive created: $zipFileName" -ForegroundColor Green

    # GPG signing
    $ascFileName = "$zipFileName.asc"
    $ascPath = Join-Path $ProjectRoot $ascFileName
    $gpgKeyId = "91F913C25A03B0F4"
    $gpgSigned = $false
    
    if (-not $NoSign) {
        # Find GPG executable
        $gpgExe = "gpg"
        $gpgPaths = @(
            "C:\Program Files (x86)\gnupg\bin\gpg.exe",
            "C:\Program Files\Git\usr\bin\gpg.exe",
            "C:\Program Files\GnuPG\bin\gpg.exe"
        )
        foreach ($gp in $gpgPaths) {
            if (Test-Path $gp) {
                $gpgExe = $gp
                break
            }
        }
        
        Write-Host "Signing archive with GPG..." -ForegroundColor Yellow
        $gpgResult = & $gpgExe --armor --detach-sign --local-user $gpgKeyId $zipPath 2>&1
        if ($LASTEXITCODE -eq 0 -and (Test-Path $ascPath)) {
            Write-Host "GPG signature created: $ascFileName" -ForegroundColor Green
            $gpgSigned = $true
        } else {
            Write-Host "GPG signing skipped (key not available or GPG not installed)" -ForegroundColor Yellow
            Write-Host "  To enable signing: gpg --import gpg/public_key.asc" -ForegroundColor DarkGray
        }
    } else {
        Write-Host "GPG signing skipped (-NoSign flag)" -ForegroundColor Yellow
    }

    Write-Host "Computing SHA256 checksum..." -ForegroundColor Yellow
    $hashResult = Get-FileHash -Path $zipPath -Algorithm SHA256
    $zipInfo = Get-Item $zipPath
    $zipSizeBytes = $zipInfo.Length
    $zipSizePretty = "{0:N0}" -f $zipSizeBytes
    $zipSizeMB = "{0:N0}" -f ([math]::Round($zipSizeBytes / 1MB))
    $dateLine = Get-Date -Format "MMMM d, yyyy"
    $shaFileName = "$zipBaseName.SHA256.txt"
    $shaPath = Join-Path $ProjectRoot $shaFileName

    $shaContent = @"
M110A Modem v$versionString Build $($version.Build) - Release Package
==============================================
Author: Alex Pennington
Email:  alex.pennington@organicengineer.com
Web:    https://www.organicengineer.com/projects

Filename: $zipFileName
Size:     $zipSizePretty bytes ($zipSizeMB MB)
Date:     $dateLine

SHA256 Checksum:
$($hashResult.Hash)

Verification (Windows PowerShell):
Get-FileHash $zipFileName -Algorithm SHA256

"@

    # Add GPG signature section if signed
    if ($gpgSigned) {
        $shaContent += @"
GPG Signature Verification:
---------------------------
Signature File: $ascFileName
Signing Key:    91F913C25A03B0F4
Key Owner:      Alexander Keith Pennington <alex.pennington@organicengineer.com>

To verify the signature:
1. Import the public key from GitHub or gpg/public_key.asc:
   gpg --import public_key.asc
   
2. Verify the signature:
   gpg --verify $ascFileName $zipFileName

Expected output: "Good signature from Alexander Keith Pennington"

The signing key is linked to the author's GitHub account:
https://github.com/akpennington

"@
    }

    $shaContent += @"
Package Contents:
- bin/m110a_server.exe (TCP/IP modem server)
- bin/exhaustive_test.exe (comprehensive modem test suite)
- bin/test_gui.exe (web-based test GUI at http://localhost:8080)
- bin/melpe_vocoder.exe (MELPe voice codec - 600/1200/2400 bps)
- docs/ (protocol, channel simulation references)
- examples/refrence_pcm/ (M110A reference waveforms and metadata)
- examples/melpe_test_audio/ (MELPe test audio files - 8kHz PCM)
- INSTALL.md, QUICKSTART.md, README.md, EULA.md

Installation:
1. Extract ZIP archive
2. Read INSTALL.md
3. Go to https://www.organicengineer.com/projects to obtain a license key
4. Place license.key in bin/ directory
5. Run m110a_server.exe, exhaustive_test.exe, or test_gui.exe

Support: alex.pennington@organicengineer.com

Copyright (c) 2024-2025 Alex Pennington
MIL-STD-188-110A Compatible HF Modem
"@
    Set-Content -Path $shaPath -Value $shaContent
    Write-Host "Checksum manifest written: $shaFileName" -ForegroundColor Green

    # Return artifact info for publishing
    return @{
        ZipPath = $zipPath
        ZipFileName = $zipFileName
        AscPath = $ascPath
        AscFileName = $ascFileName
        ShaPath = $shaPath
        ShaFileName = $shaFileName
        VersionString = $versionString
        GpgSigned = $gpgSigned
    }
}

function Publish-GitHubRelease {
    param(
        $ReleaseInfo,
        [switch]$Draft,
        [switch]$IsPrerelease
    )
    
    Write-Host "`n=== Publishing to GitHub ===" -ForegroundColor Cyan
    
    # Check for GitHub CLI
    $ghPath = Get-Command gh -ErrorAction SilentlyContinue
    if (-not $ghPath) {
        Write-Host "ERROR: GitHub CLI (gh) not found" -ForegroundColor Red
        Write-Host "Install from: https://cli.github.com/" -ForegroundColor Yellow
        Write-Host "Then run: gh auth login" -ForegroundColor Yellow
        return $false
    }
    
    # Check authentication
    $authStatus = gh auth status 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: Not authenticated with GitHub" -ForegroundColor Red
        Write-Host "Run: gh auth login" -ForegroundColor Yellow
        return $false
    }
    
    $version = Get-CurrentVersion
    $tagName = "v$($ReleaseInfo.VersionString)"
    $releaseName = "M110A Modem $tagName"
    
    # Get default branch from remote
    $defaultBranch = (git remote show origin 2>&1 | Select-String "HEAD branch:" | ForEach-Object { $_.Line -replace ".*HEAD branch:\s*", "" }).Trim()
    if (-not $defaultBranch) {
        $defaultBranch = "master"  # Fallback
    }
    $currentBranch = git rev-parse --abbrev-ref HEAD
    
    # Check for uncommitted changes
    $gitStatus = git status --porcelain 2>&1
    if ($gitStatus) {
        Write-Host "Uncommitted changes detected. Committing version bump..." -ForegroundColor Yellow
        git add api/version.h
        git commit -m "chore(release): $tagName"
        if ($LASTEXITCODE -ne 0) {
            Write-Host "WARNING: Could not commit version.h (may already be committed)" -ForegroundColor Yellow
        }
    }
    
    # Push current branch first
    Write-Host "Pushing $currentBranch to remote..." -ForegroundColor Yellow
    $null = git push origin $currentBranch 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: Failed to push to remote" -ForegroundColor Red
        return $false
    }
    Write-Host "Pushed to origin/$currentBranch" -ForegroundColor Green
    
    # If not on default branch, push to default branch to trigger release workflow
    if ($currentBranch -ne $defaultBranch) {
        Write-Host "`nPushing to $defaultBranch to trigger release workflow..." -ForegroundColor Yellow
        $null = git push origin "${currentBranch}:${defaultBranch}" 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Host "ERROR: Failed to push to $defaultBranch" -ForegroundColor Red
            Write-Host "You may need to create a PR to merge $currentBranch -> $defaultBranch" -ForegroundColor Yellow
            Write-Host "  gh pr create --base $defaultBranch --head $currentBranch --title `"Release $tagName`"" -ForegroundColor DarkCyan
            return $false
        }
        Write-Host "Pushed to origin/$defaultBranch" -ForegroundColor Green
    }
    
    # GitHub Actions workflow will auto-generate tag and release on push to default branch
    Write-Host "`nGitHub Actions will auto-generate tag and release" -ForegroundColor Cyan
    
    # Check if tag already exists (from previous release or manual creation)
    $existingTag = git tag -l $tagName 2>&1
    if (-not $existingTag) {
        Write-Host "`nArtifacts built locally:" -ForegroundColor Yellow
        Write-Host "  $($ReleaseInfo.ZipFileName)" -ForegroundColor Gray
        Write-Host "  $($ReleaseInfo.ShaFileName)" -ForegroundColor Gray
        if ($ReleaseInfo.GpgSigned) {
            Write-Host "  $($ReleaseInfo.AscFileName)" -ForegroundColor Gray
        }
        Write-Host "`nCI will create release. To upload artifacts manually after CI completes:" -ForegroundColor Yellow
        Write-Host "  gh release upload $tagName $($ReleaseInfo.ZipFileName) $($ReleaseInfo.ShaFileName)" -ForegroundColor DarkCyan
        return $true
    }
    
    # Tag exists - upload to existing release
    Write-Host "Tag $tagName exists. Uploading artifacts to release..." -ForegroundColor Yellow
    
    # Build release notes
    $releaseNotes = @"
## M110A Modem $tagName

MIL-STD-188-110A Compatible HF Modem

### Downloads
- **$($ReleaseInfo.ZipFileName)** - Windows x64 release package
- **$($ReleaseInfo.ShaFileName)** - SHA256 checksum

### Verification
``````powershell
# Verify SHA256 checksum
(Get-FileHash $($ReleaseInfo.ZipFileName) -Algorithm SHA256).Hash
``````
"@
    
    if ($ReleaseInfo.GpgSigned) {
        $releaseNotes += @"

``````bash
# Verify GPG signature
gpg --verify $($ReleaseInfo.AscFileName) $($ReleaseInfo.ZipFileName)
``````
"@
    }
    
    $releaseNotes += @"

### Installation
1. Download and extract the ZIP archive
2. Read INSTALL.md for setup instructions
3. Obtain license key from https://www.organicengineer.com/projects

---
*Build $($version.Build) | $(Get-Date -Format "yyyy-MM-dd")*
"@
    
    # Build gh release command arguments
    $ghArgs = @("release", "create", $tagName)
    $ghArgs += $ReleaseInfo.ZipPath
    $ghArgs += $ReleaseInfo.ShaPath
    if ($ReleaseInfo.GpgSigned -and (Test-Path $ReleaseInfo.AscPath)) {
        $ghArgs += $ReleaseInfo.AscPath
    }
    $ghArgs += "--title"
    $ghArgs += $releaseName
    $ghArgs += "--notes"
    $ghArgs += $releaseNotes
    
    if ($Draft) {
        $ghArgs += "--draft"
    }
    if ($IsPrerelease) {
        $ghArgs += "--prerelease"
    }
    
    # Check if release already exists
    $existingRelease = gh release view $tagName 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Release $tagName already exists. Updating..." -ForegroundColor Yellow
        # Delete and recreate
        gh release delete $tagName -y 2>&1
    }
    
    Write-Host "Creating GitHub release..." -ForegroundColor Yellow
    $result = & gh $ghArgs 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: Failed to create release" -ForegroundColor Red
        Write-Host $result -ForegroundColor Red
        return $false
    }
    
    Write-Host "`n=== Release Published Successfully ===" -ForegroundColor Green
    Write-Host "URL: https://github.com/$($script:GitHubOwner)/$($script:GitHubRepo)/releases/tag/$tagName" -ForegroundColor Cyan
    
    # Offer attestation
    Write-Host "`nTo add attestation (optional):" -ForegroundColor Gray
    Write-Host "  gh attestation create $($ReleaseInfo.ZipFileName) --owner $($script:GitHubOwner)" -ForegroundColor DarkCyan
    
    return $true
}

function Clean-Build {
    Write-Host "`n=== Cleaning ===" -ForegroundColor Yellow
    
    $exeFiles = Get-ChildItem -Path $ProjectRoot -Recurse -Include "*.exe" -ErrorAction SilentlyContinue
    foreach ($exe in $exeFiles) {
        Remove-Item $exe.FullName -Force -ErrorAction SilentlyContinue
        Write-Host "Removed: $($exe.Name)" -ForegroundColor DarkGray
    }
}

# ============================================================
# Main
# ============================================================

Write-Host "============================================" -ForegroundColor Cyan
Write-Host "M110A Modem Build System" -ForegroundColor Cyan  
Write-Host "============================================" -ForegroundColor Cyan

Set-Location $ProjectRoot

# Clean if requested
if ($Clean) {
    Clean-Build
}

# Update version
$version = Get-CurrentVersion
$version = Update-Version -Version $version -IncrementType $Increment -PrereleaseTag $Prerelease
Write-VersionHeader -Version $version

# Display version info
$versionStr = Get-VersionString -Version $version
$versionFull = Get-VersionString -Version $version -Full
Write-Host "`nVersion: $versionStr" -ForegroundColor Cyan
Write-Host "Full:    $versionFull" -ForegroundColor DarkGray

# Build targets
$releaseInfo = $null
switch ($Target.ToLower()) {
    "server" {
        Build-Server
    }
    "test" {
        Build-ExhaustiveTest
    }
    "unified" {
        Build-UnifiedTest
    }
    "unit" {
        Build-UnitTests
    }
    "gui" {
        Build-TestGui
    }
    "license" {
        Build-LicenseGen
    }
    "release" {
        $releaseInfo = Build-Release
    }
    "all" {
        Build-Server
        Build-ExhaustiveTest
        Build-UnifiedTest
        Build-UnitTests
        Build-TestGui
        Build-LicenseGen
    }
    default {
        Write-Host "Unknown target: $Target" -ForegroundColor Red
        Write-Host "Valid targets: server, test, unified, unit, gui, license, release, all" -ForegroundColor Yellow
        exit 1
    }
}

# Publish to GitHub if requested
if ($Publish -and $releaseInfo) {
    $isPrerelease = [bool]$version.Prerelease
    $publishResult = Publish-GitHubRelease -ReleaseInfo $releaseInfo -Draft:$Draft -IsPrerelease:$isPrerelease
    if (-not $publishResult) {
        Write-Host "`nPublish failed, but local artifacts are available." -ForegroundColor Yellow
    }
} elseif ($Publish -and -not $releaseInfo) {
    Write-Host "`nWARNING: -Publish requires -Target release" -ForegroundColor Yellow
    Write-Host "Run: .\build.ps1 -Target release -Publish" -ForegroundColor Gray
}

Write-Host "`n============================================" -ForegroundColor Cyan
Write-Host "Build Complete" -ForegroundColor Green
Write-Host "============================================" -ForegroundColor Cyan
