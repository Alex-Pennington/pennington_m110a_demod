# Download MELPe Test Audio Files
# Source: Open Speech Repository (https://www.voiptroubleshooter.com/open_speech/)
# License: Freely available for VoIP testing, research, development
#
# These are Harvard Sentences recorded at 8kHz, 16-bit PCM mono
# Perfect for MELPe codec testing at 600/1200/2400 bps

param(
    [string]$OutputDir = "test_audio"
)

$ErrorActionPreference = "Stop"

# Create output directory
$testAudioPath = Join-Path $PSScriptRoot $OutputDir
if (-not (Test-Path $testAudioPath)) {
    New-Item -ItemType Directory -Path $testAudioPath | Out-Null
}

Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "MELPe Test Audio Downloader" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "Source: Open Speech Repository" -ForegroundColor Gray
Write-Host "Format: 8kHz, 16-bit PCM, Mono (WAV)" -ForegroundColor Gray
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host ""

# Base URL for Open Speech Repository
$baseUrl = "https://www.voiptroubleshooter.com/open_speech/american"

# Test files to download (mix of male and female voices)
$testFiles = @(
    @{ Name = "OSR_us_000_0010_8k.wav"; Description = "Female - Harvard Sentences Set 1" },
    @{ Name = "OSR_us_000_0011_8k.wav"; Description = "Female - Harvard Sentences Set 2" },
    @{ Name = "OSR_us_000_0030_8k.wav"; Description = "Male - Harvard Sentences Set 1" },
    @{ Name = "OSR_us_000_0031_8k.wav"; Description = "Male - Harvard Sentences Set 2" }
)

Write-Host "Downloading test audio files..." -ForegroundColor Yellow
Write-Host ""

$downloaded = @()

foreach ($file in $testFiles) {
    $url = "$baseUrl/$($file.Name)"
    $wavPath = Join-Path $testAudioPath $file.Name
    $rawName = $file.Name -replace "\.wav$", ".raw"
    $rawPath = Join-Path $testAudioPath $rawName
    
    Write-Host "  $($file.Name)" -ForegroundColor White -NoNewline
    Write-Host " - $($file.Description)" -ForegroundColor Gray
    
    try {
        # Download WAV file
        if (-not (Test-Path $wavPath)) {
            Write-Host "    Downloading..." -ForegroundColor DarkGray
            Invoke-WebRequest -Uri $url -OutFile $wavPath -UseBasicParsing
        } else {
            Write-Host "    Already exists (skipping download)" -ForegroundColor DarkGray
        }
        
        # Convert WAV to raw PCM (strip 44-byte header)
        if (-not (Test-Path $rawPath)) {
            Write-Host "    Converting to raw PCM..." -ForegroundColor DarkGray
            $wavBytes = [System.IO.File]::ReadAllBytes($wavPath)
            # WAV header is 44 bytes for standard PCM
            $rawBytes = $wavBytes[44..($wavBytes.Length - 1)]
            [System.IO.File]::WriteAllBytes($rawPath, $rawBytes)
        }
        
        $wavSize = (Get-Item $wavPath).Length
        $rawSize = (Get-Item $rawPath).Length
        $durationSec = [math]::Round($rawSize / (8000 * 2), 1)  # 8000 Hz * 2 bytes per sample
        
        Write-Host "    WAV: $([math]::Round($wavSize/1024, 1)) KB | RAW: $([math]::Round($rawSize/1024, 1)) KB | Duration: ${durationSec}s" -ForegroundColor Green
        
        $downloaded += @{
            WAV = $wavPath
            RAW = $rawPath
            Duration = $durationSec
            Description = $file.Description
        }
    }
    catch {
        Write-Host "    FAILED: $_" -ForegroundColor Red
    }
    
    Write-Host ""
}

# Create metadata JSON
$metadata = @{
    Source = "Open Speech Repository"
    SourceURL = "https://www.voiptroubleshooter.com/open_speech/"
    License = "Freely available for VoIP testing, research, development"
    Attribution = "Open Speech Repository"
    Format = @{
        SampleRate = 8000
        BitDepth = 16
        Channels = 1
        Encoding = "Signed PCM, Little-Endian"
    }
    Files = $downloaded | ForEach-Object {
        @{
            WAV = Split-Path $_.WAV -Leaf
            RAW = Split-Path $_.RAW -Leaf
            DurationSeconds = $_.Duration
            Description = $_.Description
        }
    }
    DownloadDate = (Get-Date -Format "yyyy-MM-dd")
}

$metadataPath = Join-Path $testAudioPath "test_audio_metadata.json"
$metadata | ConvertTo-Json -Depth 4 | Set-Content $metadataPath

Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "Download Complete!" -ForegroundColor Green
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "Files saved to: $testAudioPath" -ForegroundColor Yellow
Write-Host ""
Write-Host "Usage with MELPe vocoder:" -ForegroundColor White
Write-Host "  # Encode at 2400 bps:" -ForegroundColor Gray
Write-Host "  melpe_vocoder.exe -r 2400 -m A -i test_audio\OSR_us_000_0010_8k.raw -o encoded.mel" -ForegroundColor Cyan
Write-Host ""
Write-Host "  # Decode:" -ForegroundColor Gray
Write-Host "  melpe_vocoder.exe -r 2400 -m S -i encoded.mel -o decoded.raw" -ForegroundColor Cyan
Write-Host ""
Write-Host "  # Loopback test (encode + decode):" -ForegroundColor Gray
Write-Host "  melpe_vocoder.exe -r 2400 -m C -i test_audio\OSR_us_000_0010_8k.raw -o loopback.raw" -ForegroundColor Cyan
Write-Host ""
Write-Host "Play raw audio (requires ffplay/ffmpeg):" -ForegroundColor White
Write-Host "  ffplay -f s16le -ar 8000 -ac 1 decoded.raw" -ForegroundColor Cyan
Write-Host ""
Write-Host "================================================================" -ForegroundColor Cyan
