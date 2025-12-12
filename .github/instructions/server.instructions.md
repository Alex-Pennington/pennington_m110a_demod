---
applyTo: "server/**"
---

# Server Implementation Instructions

## Overview

This folder contains TCP server implementations for the M110A modem project.

## Server Architecture

### Port Configuration

| Server | Control Port | Data Port | Description |
|--------|-------------|-----------|-------------|
| Phoenix Nest (m110a_server) | 4999 | 4998 | Main modem server |
| Brain Core (brain_tcp_server) | 3999 | 3998 | Brain wrapper server |

### Key Files

- `tcp_server_base.h/.cpp` - Robust TCP socket abstraction layer
- `tcp_server.h/.cpp` - Phoenix Nest TCP server (uses modem API)
- `brain_tcp_server.h` - Brain Core TCP server (uses brain_wrapper.h)
- `brain_server_main.cpp` - Brain Core server entry point

## TCP Connection Model

### Dual-Port Architecture

1. **Control Port**: Commands and responses (text, newline-terminated)
2. **Data Port**: Binary TX/RX data transfer

### Connection Lifecycle

```
1. Server starts listening on both ports
2. Client connects to control port → Server sends "READY:..." message
3. Client connects to data port (no handshake)
4. Commands sent on control, binary data on data port
5. Connections remain open for entire session
6. Client closes both ports when done
```

### Non-Blocking I/O

All sockets use non-blocking I/O to prevent deadlocks:

```cpp
// Windows
u_long mode = 1;
ioctlsocket(sock, FIONBIO, &mode);

// Linux
int flags = fcntl(sock, F_GETFL, 0);
fcntl(sock, F_SETFL, flags | O_NONBLOCK);
```

## Command Protocol

Commands are text-based, newline-terminated:

```
CMD:DATA RATE:600S       → OK:DATA RATE:600 BPS SHORT
CMD:SENDBUFFER           → TX:COMPLETE:N
CMD:RESET MDM            → OK:RESET
CMD:RXAUDIOINJECT:<path> → RX:COMPLETE:N:MODE:xxx
CMD:QUERY:STATUS         → STATUS:IDLE TX_MODE:...
CMD:QUERY:VERSION        → VERSION:x.x.x
CMD:QUERY:MODES          → MODES:75S,75L,...
```

## Testing Servers

### DO NOT use VS Code background terminals

VS Code kills background processes. Always start servers in **separate PowerShell windows**:

```powershell
# Start in separate window
Start-Process powershell -ArgumentList "-NoExit", "-Command", ".\release\bin\brain_tcp_server.exe"

# Or use the helper scripts
.\testing\interop\start_brain_core.ps1
```

### Testing Connection

Use the interop test scripts, NOT inline PowerShell TcpClient (it blocks):

```powershell
# Good - uses proper async handling
.\testing\interop\test_bc_loopback.ps1

# Bad - blocks on ReadLine()
$client = New-Object System.Net.Sockets.TcpClient(...)  # DON'T DO THIS
```

## ServerBase Class

The `tcp_base::ServerBase` class provides:

- Automatic socket initialization (WSAStartup on Windows)
- Non-blocking accept and recv
- Connection state management
- Command buffering and parsing
- Abstract `on_command()` for derived classes

### Creating a New Server

```cpp
class MyServer : public tcp_base::ServerBase {
protected:
    std::string get_ready_message() override {
        return "READY:My Server v1.0";
    }
    
    void on_command(const std::string& cmd) override {
        if (cmd == "CMD:QUERY:VERSION") {
            send_control("VERSION:1.0.0");
        }
    }
    
    void on_data_received(const std::vector<uint8_t>& data) override {
        // Handle binary data from data port
    }
};
```

## Build Notes

Brain Core server requires the brain_core submodule:

```powershell
# Ensure submodule is initialized
git submodule update --init --recursive

# Compile
g++ -std=c++17 -O2 -I. -Iextern -Iextern/brain_core/include/m188110a \
    -o brain_tcp_server.exe \
    server/brain_server_main.cpp server/tcp_server_base.cpp \
    -Lextern/brain_core/lib/win64 -lm188110a -lws2_32 -static
```
