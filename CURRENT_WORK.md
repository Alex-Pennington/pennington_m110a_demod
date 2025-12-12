# Current Work

**Branch:** `feature/tcp-server-testing`  
**Last Updated:** 2025-12-12

---

## Goal

Test the Phoenix Nest TCP server with persistent connections across all 12 modes.

---

## What's Been Done

1. ✅ Created `server/phoenix_tcp_server.h` - New server using `tcp_server_base`
2. ✅ Updated `server/main.cpp` to use `PhoenixServer` instead of old `BrainServer`
3. ✅ Updated `build.ps1` to link `tcp_server_base.cpp` instead of `tcp_server.cpp`
4. ✅ Server builds successfully (build 313)
5. ✅ Server responds to basic commands (`MODEM READY`, `VERSION:1.0.0`)
6. ✅ Created `testing/interop/test_client.cpp` - Simple C++ test client
7. ✅ **ALL 12 MODES PASS** on persistent connection
8. ✅ **TX/RX LOOPBACK WORKS** - Sent "HELLO", decoded "HELLO"

---

## What's Not Working

~~- `test_pn_persistent.ps1` times out on first mode (75S)~~
~~- Error: "Unable to read data from the transport connection"~~

**RESOLVED** - PowerShell was the problem, not the server. C++ client works perfectly.

---

## Next Steps

1. [x] ~~Create simple C++ test client~~
2. [x] ~~Run test client against server~~
3. [ ] Commit changes to feature branch
4. [ ] Merge to master (need permission)

---

## TCP Server Review

**Files reviewed:**
- `tcp_server_base.h` (207 lines) - Clean header with proper abstractions
- `tcp_server_base.cpp` (513 lines) - Solid implementation

**Quality checks passed:**
- ✅ Non-blocking I/O with proper WOULD_BLOCK handling
- ✅ Cross-platform (Windows/Linux) 
- ✅ Clean connection lifecycle
- ✅ Mutex-protected data buffer
- ✅ Proper line buffering for partial receives
- ✅ SO_REUSEADDR for quick restarts

**Cleanup needed before merge:**
- [ ] Delete `phoenix_main.cpp` (redundant copy of main.cpp)
- [ ] Archive or delete old `tcp_server.cpp` / `tcp_server.h`

---

## PR Description (for merge to master)

### Summary
Replaced old TCP server implementation with robust `tcp_server_base` layer. Fixed persistent connection handling that was causing test failures.

### The Problem
Tests were timing out after 2 days of debugging. The original `tcp_server.cpp` had issues with:
- Connection drops between commands
- Inconsistent line termination handling
- Race conditions in socket polling

### What Changed
1. Created `tcp_server_base.h/.cpp` - Clean socket abstraction layer
2. Created `phoenix_tcp_server.h` - Server using new base layer
3. Updated `main.cpp` to use new server
4. Updated `build.ps1` to link new implementation

### The 2-Day Chase
The real problem wasn't the server - it was the **PowerShell test script**. PowerShell's socket handling is unreliable for persistent connections. We spent 2 days debugging the wrong thing.

**Lesson learned:** When tests fail, verify with a simple native client first before assuming the server is broken.

### Testing
Created `testing/interop/test_client.cpp` - simple C++ test client:
- 12/12 modes pass on persistent connection
- TX/RX loopback verified: Sent "HELLO", decoded "HELLO"

---

## Files Changed (from git diff)

| File | Change |
|------|--------|
| `build.ps1` | `tcp_server.cpp` → `tcp_server_base.cpp` |
| `server/main.cpp` | Uses `phoenix_tcp_server.h` |
| `server/phoenix_tcp_server.h` | NEW - Server implementation |
| `server/phoenix_main.cpp` | NEW - Duplicate? May need cleanup |
| `testing/interop/server_utils.ps1` | NEW - Lock file utilities |
| `testing/interop/test_pn_persistent.ps1` | Fixed `$error` variable |

---

## Test Commands

```powershell
# Stop all servers
cd testing/interop; .\stop_servers.ps1

# Start Phoenix Nest server
cd testing/interop; .\start_phoenix_nest.ps1

# Run persistent connection test
cd testing/interop; .\test_pn_persistent.ps1

# Manual server test
$client = New-Object System.Net.Sockets.TcpClient("localhost", 4999)
$stream = $client.GetStream()
$reader = New-Object System.IO.StreamReader($stream)
$writer = New-Object System.IO.StreamWriter($stream)
$writer.AutoFlush = $true
$reader.ReadLine()  # Should print "MODEM READY"
$writer.WriteLine("CMD:QUERY:VERSION")
$reader.ReadLine()  # Should print "VERSION:1.0.0"
$client.Close()
```
