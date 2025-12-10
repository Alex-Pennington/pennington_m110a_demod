# MELPe Test Audio Files

Test audio files for validating the MELPe vocoder at 600/1200/2400 bps rates.

## Source

**Open Speech Repository**  
https://www.voiptroubleshooter.com/open_speech/

These are Harvard Sentences recordings, freely available for VoIP testing, research, and development. Attribution: "Open Speech Repository"

## Audio Format

- **Sample Rate**: 8000 Hz
- **Bit Depth**: 16-bit signed
- **Channels**: Mono
- **Encoding**: Little-endian PCM

Two formats are provided:
- `.wav` - Standard WAV files (with headers)
- `.raw` - Raw PCM data (no headers) - **Use these with MELPe vocoder**

## Files

| File | Speaker | Duration | Description |
|------|---------|----------|-------------|
| OSR_us_000_0010_8k | Female | ~34s | Harvard Sentences Set 1 |
| OSR_us_000_0011_8k | Female | ~33s | Harvard Sentences Set 2 |
| OSR_us_000_0030_8k | Male | ~47s | Harvard Sentences Set 1 |
| OSR_us_000_0031_8k | Male | ~42s | Harvard Sentences Set 2 |

## Usage with MELPe Vocoder

### Loopback Test (Encode + Decode)
```bash
# Test at 2400 bps
melpe_vocoder.exe -r 2400 -m C -i OSR_us_000_0010_8k.raw -o output_2400.raw

# Test at 1200 bps
melpe_vocoder.exe -r 1200 -m C -i OSR_us_000_0010_8k.raw -o output_1200.raw

# Test at 600 bps  
melpe_vocoder.exe -r 600 -m C -i OSR_us_000_0010_8k.raw -o output_600.raw
```

### Encode Only
```bash
melpe_vocoder.exe -r 2400 -m A -i OSR_us_000_0010_8k.raw -o encoded.mel
```

### Decode Only
```bash
melpe_vocoder.exe -r 2400 -m S -i encoded.mel -o decoded.raw
```

## Playing Raw Audio

### Using FFplay (part of FFmpeg)
```bash
ffplay -f s16le -ar 8000 -ac 1 output_2400.raw
```

### Using SoX
```bash
play -r 8000 -b 16 -c 1 -e signed-integer output_2400.raw
```

### Convert to WAV
```bash
ffmpeg -f s16le -ar 8000 -ac 1 -i output_2400.raw output_2400.wav
```

## Download More Files

Run `download_test_audio.ps1` to download additional test files:

```powershell
.\download_test_audio.ps1
```

## License

The test audio files are from the Open Speech Repository and are freely available for use in VoIP testing, research, development, marketing and any other reasonable application. Please attribute as "Open Speech Repository".
