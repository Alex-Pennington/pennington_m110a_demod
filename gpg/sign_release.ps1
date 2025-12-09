<#
.SYNOPSIS
    Sign M110A Modem release files with GPG
.DESCRIPTION
    Uses portable GPG to create detached signatures for release artifacts.
    Requires private key to be imported or available in gpg/private_key.asc
.PARAMETER ZipFile
    Path to the zip file to sign
.PARAMETER KeyId
    Optional GPG key ID or email to use for signing
.EXAMPLE
    .\sign_release.ps1 -ZipFile "..\M110A_Modem_v1.2.0_Build148.zip"
#>

param(
    [Parameter(Mandatory=$true)]
    [string]$ZipFile,
    
    [string]$KeyId = "",
    
    [switch]$DownloadGpg
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$GpgHome = Join-Path $ScriptDir "gnupg"
$GpgExe = $null

# Find GPG executable
function Find-Gpg {
    # Check local directory first
    $localGpg = Join-Path $ScriptDir "gpg.exe"
    if (Test-Path $localGpg) {
        return $localGpg
    }
    
    # Check for portable GnuPG in subdirectory
    $portableGpg = Join-Path $ScriptDir "GnuPG\bin\gpg.exe"
    if (Test-Path $portableGpg) {
        return $portableGpg
    }
    
    # Check system PATH
    $systemGpg = Get-Command gpg -ErrorAction SilentlyContinue
    if ($systemGpg) {
        return $systemGpg.Path
    }
    
    return $null
}

function Download-PortableGpg {
    Write-Host "Downloading portable GnuPG..." -ForegroundColor Cyan
    
    $gpgUrl = "https://gnupg.org/ftp/gcrypt/binary/gnupg-w32-2.4.5_20240307.exe"
    $installerPath = Join-Path $ScriptDir "gnupg-installer.exe"
    
    # Note: GnuPG installer is not truly portable, we need gpg4win portable or extract manually
    # Alternative: Use standalone gpg.exe from a portable distribution
    
    Write-Host "Please download GnuPG portable manually:" -ForegroundColor Yellow
    Write-Host "  1. Download from: https://www.gpg4win.org/download.html" -ForegroundColor Yellow
    Write-Host "  2. Or get standalone binaries from: https://gnupg.org/download/" -ForegroundColor Yellow
    Write-Host "  3. Extract gpg.exe to: $ScriptDir" -ForegroundColor Yellow
    
    throw "Portable GPG not found. Please install manually."
}

# Initialize GPG home directory
function Initialize-GpgHome {
    if (-not (Test-Path $GpgHome)) {
        New-Item -ItemType Directory -Path $GpgHome -Force | Out-Null
    }
    
    # Set restrictive permissions (GPG requires this)
    $acl = Get-Acl $GpgHome
    $acl.SetAccessRuleProtection($true, $false)
    $rule = New-Object System.Security.AccessControl.FileSystemAccessRule(
        [System.Security.Principal.WindowsIdentity]::GetCurrent().Name,
        "FullControl",
        "ContainerInherit,ObjectInherit",
        "None",
        "Allow"
    )
    $acl.SetAccessRule($rule)
    Set-Acl $GpgHome $acl
}

# Import private key if not already imported
function Import-PrivateKey {
    param([string]$GpgExe)
    
    $privateKeyPath = Join-Path $ScriptDir "private_key.asc"
    
    if (-not (Test-Path $privateKeyPath)) {
        Write-Host "Private key not found at: $privateKeyPath" -ForegroundColor Red
        Write-Host "Please export your private key from Proton Mail and save it there." -ForegroundColor Yellow
        throw "Private key not found"
    }
    
    Write-Host "Importing private key..." -ForegroundColor Cyan
    $env:GNUPGHOME = $GpgHome
    & $GpgExe --import $privateKeyPath 2>&1
    
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to import private key"
    }
}

# List available keys
function Get-AvailableKeys {
    param([string]$GpgExe)
    
    $env:GNUPGHOME = $GpgHome
    $keys = & $GpgExe --list-secret-keys --keyid-format=long 2>&1
    return $keys
}

# Sign the file
function Sign-File {
    param(
        [string]$GpgExe,
        [string]$FilePath,
        [string]$KeyId
    )
    
    $env:GNUPGHOME = $GpgHome
    
    $sigPath = "$FilePath.sig"
    
    Write-Host "Signing: $FilePath" -ForegroundColor Cyan
    
    $gpgArgs = @("--detach-sign", "--armor", "--output", $sigPath)
    
    if ($KeyId) {
        $gpgArgs += @("--local-user", $KeyId)
    }
    
    $gpgArgs += $FilePath
    
    & $GpgExe @gpgArgs
    
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to sign file"
    }
    
    Write-Host "Signature created: $sigPath" -ForegroundColor Green
    return $sigPath
}

# Main execution
try {
    Write-Host "=== M110A Modem Release Signing ===" -ForegroundColor Cyan
    Write-Host ""
    
    # Resolve zip file path
    if (-not [System.IO.Path]::IsPathRooted($ZipFile)) {
        $ZipFile = Join-Path (Get-Location) $ZipFile
    }
    
    if (-not (Test-Path $ZipFile)) {
        throw "File not found: $ZipFile"
    }
    
    # Find GPG
    $GpgExe = Find-Gpg
    
    if (-not $GpgExe) {
        if ($DownloadGpg) {
            Download-PortableGpg
            $GpgExe = Find-Gpg
        } else {
            Write-Host "GPG not found. Use -DownloadGpg to download, or install manually." -ForegroundColor Red
            throw "GPG not found"
        }
    }
    
    Write-Host "Using GPG: $GpgExe" -ForegroundColor Gray
    
    # Initialize
    Initialize-GpgHome
    
    # Check for keys
    $keys = Get-AvailableKeys -GpgExe $GpgExe
    
    if ($keys -match "no secret key") {
        Write-Host "No keys found in keyring. Importing private key..." -ForegroundColor Yellow
        Import-PrivateKey -GpgExe $GpgExe
    }
    
    # Sign the file
    $sigPath = Sign-File -GpgExe $GpgExe -FilePath $ZipFile -KeyId $KeyId
    
    Write-Host ""
    Write-Host "=== Signing Complete ===" -ForegroundColor Green
    Write-Host "File: $ZipFile"
    Write-Host "Signature: $sigPath"
    Write-Host ""
    Write-Host "Users can verify with:" -ForegroundColor Cyan
    Write-Host "  gpg --import public_key.asc"
    Write-Host "  gpg --verify $([System.IO.Path]::GetFileName($sigPath)) $([System.IO.Path]::GetFileName($ZipFile))"
    
} catch {
    Write-Host "Error: $_" -ForegroundColor Red
    exit 1
}
