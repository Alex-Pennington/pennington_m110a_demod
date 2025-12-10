# M110A Modem Installation Guide

## System Requirements
- Windows 10/11 (64-bit)
- 4 GB RAM minimum
- Network connection for TCP/IP server mode

## Installation Steps

1. **Extract the release package** to your desired location

2. **Activate your license**
     - Open a command prompt in the release directory
     - Run the server .exe file and copy your hwid.
     - Go to https://www.organicengineer.com/projects to obtain a license key using your hardware ID
     - You will receive a `license.key` file
     - Place the `license.key` file in the `bin/` directory

3. **Verify installation**
     - Navigate to the `bin/` directory
     - Run: `m110a_server.exe`
     - You should see license information and server startup

## Components

### bin/m110a_server.exe
TCP/IP server for modem operations.

Ports:
- TCP 4998: Data port
- TCP 4999: Control port
- UDP 5000: Discovery (optional)

Usage:
`
cd bin
m110a_server.exe
`

### bin/exhaustive_test.exe
Comprehensive test suite for all modem modes.

Usage:
`
cd bin
exhaustive_test.exe [options]

Options:
    --server          Use TCP/IP server backend (requires m110a_server.exe running)
    --mode MODE       Test specific mode (e.g., 600S, 1200L)
    --progressive     Run progressive SNR/frequency/multipath tests
    --csv FILE        Output results to CSV
    --help            Show all options
`

### bin/test_gui.exe
Web-based graphical interface for running tests.

Usage:
`
cd bin
test_gui.exe
`

Opens http://localhost:8080 in your browser with a full GUI for test configuration.

## Quick Start

1. Start the server:
`
cd bin
m110a_server.exe
`

2. In another terminal, run a test:
`
cd bin
exhaustive_test.exe
`

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

- **AFC Range**: Automatic Frequency Control (AFC) is designed for ±2 Hz frequency offset
    - Offsets beyond ±5 Hz may result in reduced demodulation performance
    - For operation in challenging HF conditions with larger Doppler shifts, ensure transmitter/receiver frequency stability

## Troubleshooting

### "LICENSE REQUIRED" message
- Ensure `license.key` is in the same directory as the executable
- Verify license has not expired
- Check that you're running on the same hardware the license was issued for

### Server fails to start
- Check ports 4998/4999 are not in use: `netstat -an | findstr "4998 4999"`
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

See `RELEASE_INFO.txt` for build and version information.
