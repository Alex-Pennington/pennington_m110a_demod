<#
.SYNOPSIS
    Verify a signed M110A Modem release
.DESCRIPTION
    Verifies the GPG signature of a release artifact
.PARAMETER ZipFile
    Path to the zip file to verify
.PARAMETER SigFile
    Path to the signature file (defaults to ZipFile.sig)
.EXAMPLE
    .\verify_release.ps1 -ZipFile "M110A_Modem_v1.2.0_Build148.zip"
#>

param(
    [Parameter(Mandatory=$true)]
    [string]$ZipFile,
    
    [string]$SigFile = ""
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$GpgHome = Join-Path $ScriptDir "gnupg"

# Find GPG executable
function Find-Gpg {
    $localGpg = Join-Path $ScriptDir "gpg.exe"
    if (Test-Path $localGpg) { return $localGpg }
    
    $portableGpg = Join-Path $ScriptDir "GnuPG\bin\gpg.exe"
    if (Test-Path $portableGpg) { return $portableGpg }
    
    $systemGpg = Get-Command gpg -ErrorAction SilentlyContinue
    if ($systemGpg) { return $systemGpg.Path }
    
    return $null
}

try {
    Write-Host "=== M110A Modem Release Verification ===" -ForegroundColor Cyan
    Write-Host ""
    
    # Resolve paths
    if (-not [System.IO.Path]::IsPathRooted($ZipFile)) {
        $ZipFile = Join-Path (Get-Location) $ZipFile
    }
    
    if (-not $SigFile) {
        $SigFile = "$ZipFile.sig"
    } elseif (-not [System.IO.Path]::IsPathRooted($SigFile)) {
        $SigFile = Join-Path (Get-Location) $SigFile
    }
    
    if (-not (Test-Path $ZipFile)) {
        throw "File not found: $ZipFile"
    }
    
    if (-not (Test-Path $SigFile)) {
        throw "Signature not found: $SigFile"
    }
    
    # Find GPG
    $GpgExe = Find-Gpg
    if (-not $GpgExe) {
        throw "GPG not found. Please install GnuPG."
    }
    
    Write-Host "Using GPG: $GpgExe" -ForegroundColor Gray
    
    # Initialize GPG home if needed
    if (-not (Test-Path $GpgHome)) {
        New-Item -ItemType Directory -Path $GpgHome -Force | Out-Null
    }
    
    $env:GNUPGHOME = $GpgHome
    
    # Import public key if available
    $publicKeyPath = Join-Path $ScriptDir "public_key.asc"
    if (Test-Path $publicKeyPath) {
        Write-Host "Importing public key..." -ForegroundColor Gray
        & $GpgExe --import $publicKeyPath 2>&1 | Out-Null
    }
    
    # Verify signature
    Write-Host "Verifying: $([System.IO.Path]::GetFileName($ZipFile))" -ForegroundColor Cyan
    Write-Host ""
    
    $result = & $GpgExe --verify $SigFile $ZipFile 2>&1
    $exitCode = $LASTEXITCODE
    
    Write-Host $result
    Write-Host ""
    
    if ($exitCode -eq 0) {
        Write-Host "✓ Signature is VALID" -ForegroundColor Green
    } else {
        Write-Host "✗ Signature verification FAILED" -ForegroundColor Red
        exit 1
    }
    
} catch {
    Write-Host "Error: $_" -ForegroundColor Red
    exit 1
}
