# Current Work

**Branch:** `PhoenixNest_to_Brain_Testing`  
**Last Updated:** 2025-12-12

---

## Goal

Test cross-modem interoperability: **Phoenix Nest TX → Brain Core RX**

This validates that both modem implementations are compatible with MIL-STD-188-110A.

---

## Architecture (from testing.instructions.md)

| Modem | Control Port | Data Port | Sample Rate |
|-------|-------------|-----------|-------------|
| Phoenix Nest | 4999 | 4998 | 48000 Hz |
| Brain Core | 3999 | 3998 | 9600 Hz |

**Critical:** Phoenix Nest outputs 48000 Hz, Brain Core expects 9600 Hz.
- Decimation ratio: 5:1 (average every 5 samples)
- Must decimate PN output before feeding to BC

---

## Test Plan

1. Start both servers (Phoenix Nest on 4999, Brain Core on 3999)
2. Connect C++ test client to both
3. For each of 12 modes:
   - Send test message to Phoenix Nest data port
   - Phoenix Nest TX → generates PCM file (48000 Hz)
   - Decimate PCM 5:1 → 9600 Hz
   - Inject decimated PCM to Brain Core RX
   - Read decoded data from Brain Core data port
   - Compare: original message == decoded message?

---

## What's Been Done

1. [x] Create `test_pn_to_bc.cpp` - Cross-modem test client ✅
2. [ ] Start both servers
3. [ ] Run cross-modem test
4. [ ] Document results

**Build:** `g++ -o test_pn_to_bc.exe test_pn_to_bc.cpp -lws2_32`

---

## Previous Branch Summary (feature/tcp-server-testing)

- ✅ Created `tcp_server_base` for robust socket handling
- ✅ Phoenix Nest server passes 12/12 modes on persistent connection
- ✅ TX/RX loopback works: Sent "HELLO", decoded "HELLO"
- ✅ Merged to master
