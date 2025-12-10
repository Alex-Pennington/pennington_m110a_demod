# M110A Modem Quick Start

## First Time Setup

1. Get your hardware ID:
   ```
   - Run the server .exe file and copy your hwid.
   ```

2. Go to https://www.organicengineer.com/projects to obtain a license key using your hardware ID

3. Place the `license.key` file in the `bin/` directory

## Running the Server

```
cd bin
m110a_server.exe
```

The server listens on TCP 4998 (data), TCP 4999 (control), and UDP 5000 (discovery).

## Running Tests

Basic test (ideal channel):
```
cd bin
exhaustive_test.exe
```

Test with channel simulation:
```
exhaustive_test.exe --channel poor
```

Server-based test:
```
# Terminal 1
m110a_server.exe

# Terminal 2
exhaustive_test.exe --server
```

## Next Steps

- Read `INSTALL.md` for detailed installation instructions
- Review `docs/API.md` for programming interface
- Check `examples/refrence_pcm/` for reference waveforms
- See `docs/CHANNEL_SIMULATION.md` for channel modeling details

## License

See `EULA.md` for license terms and conditions. Go to https://www.organicengineer.com/projects to obtain a license key.
