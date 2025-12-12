# HF Channel Simulator (hfchansim)

Standalone command-line tool to apply realistic HF channel impairments to PCM audio files. Used for testing modem performance under various propagation conditions.

## Quick Start

```powershell
# List available reference PCM files
.\hfchansim.exe --list-ref

# Apply moderate HF channel to 600 BPS SHORT reference
.\hfchansim.exe --ref 600S --preset moderate

# Check output
ls hfchansim_out/
```

## Features

- **Reference PCM Integration**: Use bundled reference transmissions (75-2400 BPS, all interleave settings)
- **Watterson Channel Model**: Industry-standard two-path Rayleigh fading with Gaussian Doppler spectrum
- **CCIR/ITU Presets**: Good, Moderate, Poor, Flutter channel profiles
- **AWGN**: Calibrated additive white Gaussian noise with precise SNR control
- **Frequency Offset**: Simulate oscillator drift
- **Metadata Generation**: JSON output with complete channel settings for reproducibility
- **Reproducible Results**: Seed control for consistent test conditions

## Usage

### Using Reference PCMs (Recommended)

```powershell
# Basic usage with preset
.\hfchansim.exe --ref <mode> --preset <preset>

# Available modes: 75S, 75L, 150S, 150L, 300S, 300L, 600S, 600L, 1200S, 1200L, 2400S, 2400L
# Available presets: clean, awgn, good, moderate, poor, flutter, midlat, highlat
```

Output files are automatically written to `hfchansim_out/` directory.

### Using Custom Files

```powershell
.\hfchansim.exe input.pcm output.pcm [options]
```

### Examples

```powershell
# CCIR Moderate channel on 2400 BPS LONG
.\hfchansim.exe --ref 2400L --preset moderate

# Custom Watterson settings
.\hfchansim.exe --ref 600S --model watterson --doppler 2 --delay 1.5 --snr 12

# Just add noise and frequency offset
.\hfchansim.exe --ref 1200S --snr 15 --freq 3.5

# Reproducible test with specific seed
.\hfchansim.exe --ref 600S --preset moderate --seed 12345

# Process your own PCM file
.\hfchansim.exe my_transmission.pcm degraded.pcm --preset poor
```

## Command Reference

### Input Selection

| Option | Description |
|--------|-------------|
| `--ref <mode>` | Use reference PCM (e.g., 600S, 2400L) |
| `--list-ref` | List available reference PCM files |
| `<input> <output>` | Use custom input/output files |

### Channel Presets

| Preset | Model | Doppler | Delay | SNR | Description |
|--------|-------|---------|-------|-----|-------------|
| `clean` | - | - | - | ∞ | No impairments (passthrough) |
| `awgn` | AWGN | - | - | 15 dB | Pure noise, no fading |
| `good` | Watterson | 0.5 Hz | 0.5 ms | 20 dB | CCIR Good conditions |
| `moderate` | Watterson | 1.0 Hz | 1.0 ms | 15 dB | CCIR Moderate conditions |
| `poor` | Watterson | 2.0 Hz | 2.0 ms | 10 dB | CCIR Poor conditions |
| `flutter` | Watterson | 10.0 Hz | 0.5 ms | 12 dB | Near-vertical incidence flutter |
| `midlat` | Watterson | 1.0 Hz | 2.0 ms | 12 dB | Mid-latitude disturbed |
| `highlat` | Watterson | 5.0 Hz | 3.0 ms | 8 dB | High-latitude disturbed |

### Custom Channel Options

| Option | Description | Default |
|--------|-------------|---------|
| `--snr <dB>` | Target SNR for AWGN | No noise |
| `--freq <Hz>` | Frequency offset | 0 |
| `--model <type>` | Channel model: `awgn`, `watterson` | awgn |
| `--doppler <Hz>` | Watterson Doppler spread | 1.0 |
| `--delay <ms>` | Watterson differential delay | 1.0 |
| `--path1-gain <dB>` | Watterson path 1 gain | 0 |
| `--path2-gain <dB>` | Watterson path 2 gain | 0 |

### General Options

| Option | Description | Default |
|--------|-------------|---------|
| `--sample-rate <Hz>` | Sample rate for PCM | 48000 |
| `--seed <n>` | Random seed for reproducibility | 42 |
| `--verbose` | Show detailed progress | Off |
| `--version` | Show version info | - |
| `--help` | Show help | - |

## Output Files

When using `--ref`, outputs are written to `hfchansim_out/`:

```
hfchansim_out/
├── 600S_moderate_20251212_070000.pcm    # Degraded audio
└── 600S_moderate_20251212_070000.json   # Metadata
```

### Metadata JSON Format

```json
{
  "toolInfo": {
    "name": "hfchansim",
    "version": "1.4.1",
    "build": 306,
    "timestamp": "2025-12-12T07:00:00"
  },
  "inputFile": {
    "path": "../examples/refrence_pcm/tx_600S_20251206_202518_709.pcm",
    "sampleRate": 48000,
    "sampleCount": 105600,
    "durationSeconds": 2.200
  },
  "sourceMode": {
    "id": "600S",
    "name": "600 BPS SHORT",
    "bitsPerSecond": 600,
    "interleave": "SHORT",
    "modulation": "8-PSK",
    "symbolRate": 2400
  },
  "outputFile": {
    "path": "hfchansim_out/600S_moderate_20251212_070000.pcm",
    "sampleRate": 48000,
    "sampleCount": 105600,
    "durationSeconds": 2.200
  },
  "channelSettings": {
    "model": "watterson",
    "preset": "moderate",
    "snr_dB": 15.0,
    "frequencyOffset_Hz": 0.00,
    "dopplerSpread_Hz": 1.00,
    "differentialDelay_ms": 1.00,
    "path1Gain_dB": 0.0,
    "path2Gain_dB": 0.0,
    "seed": 42
  }
}
```

## Channel Models

### Watterson Model

The Watterson model is the industry-standard HF channel simulator based on:

> Watterson, Juroshek, Bensema - "Experimental Confirmation of an HF Channel Model", IEEE Trans. Comm., 1970

Features:
- Two independent Rayleigh fading paths
- Gaussian Doppler spectrum (configurable 0.1-10 Hz spread)
- Differential delay between paths (0-10 ms)
- CCIR/ITU-R standard channel profiles

### AWGN Model

Calibrated additive white Gaussian noise. SNR is measured relative to signal power:

```
SNR (dB) = 10 * log10(signal_power / noise_power)
```

## PCM File Format

All PCM files use:
- **Format**: Raw headerless PCM
- **Sample Rate**: 48000 Hz
- **Bit Depth**: 16-bit signed
- **Channels**: Mono
- **Byte Order**: Little-endian

## Typical Workflow

```powershell
# 1. Generate transmission with modem
.\m110a_server.exe  # Start server, send data via TCP

# 2. Apply channel impairments
.\hfchansim.exe --ref 600S --preset moderate

# 3. Decode degraded signal
# (inject into modem RX or use exhaustive_test.exe)

# 4. Compare results
# Check metadata JSON for exact channel settings
```

## Building

Built as part of the main project:

```powershell
.\build.ps1
# Output: release/bin/hfchansim.exe
```

## See Also

- `src/channel/watterson.h` - Watterson channel implementation
- `src/channel/awgn.h` - AWGN implementation
- `examples/refrence_pcm/` - Reference PCM files and metadata
- `docs/CHANNEL_SIMULATION.md` - Detailed channel model documentation

## License

Copyright (c) 2025 Phoenix Nest LLC
