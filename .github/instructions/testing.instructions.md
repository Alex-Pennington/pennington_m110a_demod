---
applyTo: "testing/**"
---

# Testing Framework Instructions

## Overview

This folder contains all testing infrastructure for the M110A modem project, including interop testing between different modem implementations.

## Test Architecture

### Modem Servers

| Server | Control Port | Data Port | Sample Rate |
|--------|-------------|-----------|-------------|
| Brain Core | 3999 | 3998 | 9600 Hz |
| Phoenix Nest | 4999 | 4998 | 48000 Hz |

### Connection Rules

1. **Persistent Connections**: Connect to BOTH control and data ports at test start. Keep connections open for the ENTIRE test run. Never reconnect between iterations.

2. **Server Startup**: Servers MUST run in separate terminal windows (not background processes). Use `interop/start_brain_core.ps1` or `interop/start_phoenix_nest.ps1`.

3. **VS Code Terminal Warning**: VS Code kills background processes. Always use separate PowerShell windows for servers.

## Folder Structure

```
testing/
├── README.md                # General testing documentation
├── interop/                 # Interop testing scripts
│   ├── baseline/           # FROZEN - Working baseline tests
│   │   └── test_brain_core_refrence_rx.ps1
│   ├── start_brain_core.ps1
│   ├── start_phoenix_nest.ps1
│   ├── start_servers.ps1
│   ├── stop_servers.ps1
│   ├── test_bc_loopback.ps1
│   ├── test_pn_loopback.ps1
│   ├── test_pn_to_bc.ps1
│   ├── loopback_bc.ps1
│   ├── loopback_pn.ps1
│   └── README.md
├── logs/                    # Test output logs
└── pcm/                     # Test PCM files
```

## Baseline Tests (FROZEN)

The `interop/baseline/` folder contains frozen, working tests that serve as a reference. **Do not modify these files.**

Current baseline tests:
- `test_brain_core_refrence_rx.ps1` - Brain Core RX with reference PCM files (12/12 modes pass)
- `test_bc_persistent.ps1` - Persistent connection loopback (12/12 modes pass)

## Server Selection

**Always use `brain_tcp_server`** (the tcp_base implementation) for testing:

```powershell
.\start_brain_core.ps1 -UseTcpBase
```

The original `brain_modem_server` fails persistent connection tests (0/12).

| Server | Persistent Connections |
|--------|----------------------|
| brain_modem_server | ❌ 0/12 FAIL |
| brain_tcp_server | ✅ 12/12 PASS |

## Test Flow Pattern

All interop tests follow this pattern:

```powershell
# 1. Connect to both ports
$control = [System.Net.Sockets.TcpClient]::new("localhost", $controlPort)
$data = [System.Net.Sockets.TcpClient]::new("localhost", $dataPort)

# 2. Get streams
$controlStream = $control.GetStream()
$dataStream = $data.GetStream()
$reader = [System.IO.StreamReader]::new($controlStream)
$writer = [System.IO.StreamWriter]::new($controlStream)
$writer.AutoFlush = $true

# 3. Wait for READY
$ready = $reader.ReadLine()

# 4. Run tests (connections stay open)
foreach ($mode in $modes) {
    $writer.WriteLine("RX:MODE=$mode")
    $response = $reader.ReadLine()
    # ... inject data, check results
}

# 5. Close only at the end
$control.Close()
$data.Close()
```

## Sample Rate Handling

- **Brain Core**: Expects 9600 Hz samples
- **Phoenix Nest**: Outputs 48000 Hz samples
- **Cross-testing**: When feeding PN output to BC, decimate 5:1 (average every 5 samples)

## Test Types

1. **Reference RX**: Feed known-good PCM files to RX, verify decode
2. **Loopback**: TX a message, feed TX output back to RX, verify decode
3. **Cross-Interop**: One modem TX → other modem RX

## Writing New Tests

1. Copy the pattern from an existing working test
2. Use persistent connections (don't reconnect per iteration)
3. Add proper error handling and timeouts
4. Log results clearly (PASS/FAIL with mode info)
5. When stable, consider moving to `baseline/` folder
