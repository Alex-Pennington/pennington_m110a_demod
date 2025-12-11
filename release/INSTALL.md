# M110A Modem Installation Guide

## System Requirements
- Windows 10/11 (64-bit)
- 4 GB RAM minimum
- Network connection for TCP/IP server mode

## Components

### bin/m110a_server.exe
TCP/IP server for modem operations.

Ports:
- TCP 4998: Data port
- TCP 4999: Control port

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
    --mode MODE       Test specific mode (e.g., 600S, 1200L)
    --channel TYPE    Channel simulation (clean, awgn, multipath, poor_hf)
    --snr VALUE       Signal-to-noise ratio in dB
    --duration SEC    Test duration in seconds
    --json            Output results in JSON format
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

### bin/melpe_vocoder.exe
MELPe voice codec (600/1200/2400 bps).

## Quick Start

1. Run the test GUI:
```
cd bin
test_gui.exe
```

2. Open http://localhost:8080 in your browser

3. Or run command-line tests:
```
cd bin
exhaustive_test.exe --mode 1200L --channel awgn --snr 15
```

## Reference PCM Files

The `examples/refrence_pcm/` directory contains reference waveforms for all supported modes:
- 75 BPS (BPSK, Long/Short interleaver)
- 150 BPS (BPSK, Long/Short)
- 300 BPS (QPSK, Long/Short)
- 600 BPS (QPSK, Long/Short)
- 1200 BPS (8-PSK, Long/Short)
- 2400 BPS (8-PSK, Long/Short)

Each PCM file includes metadata in a JSON file.

## Known Limitations

- **AFC Range**: Automatic Frequency Control handles approximately +/-2 Hz offset
- **75 BPS modes**: Currently under development

## Troubleshooting

### Server fails to start
- Check ports 4998/4999 are not in use: `netstat -an | findstr "4998 4999"`
- Check firewall settings

### Test failures
- Ensure reference PCM files are in correct location
- Check channel simulation parameters
- Review test output for detailed logs

## Support

- Website: https://www.organicengineer.com/projects
- Email: alex.pennington@organicengineer.com
- Documentation: See docs/ directory
