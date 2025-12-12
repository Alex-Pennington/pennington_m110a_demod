# Current Work

**Branch:** `PhoenixNest_to_Brain_Testing`  
**Last Updated:** 2025-12-12 3:26 PM EDT

---

## Goal

Test cross-modem interoperability: **Phoenix Nest TX → Brain Core RX**

This validates that both modem implementations are compatible with MIL-STD-188-110A.

---

## Session Progress (2025-12-12 Afternoon)

### Qt MSDMT as Ground Truth

User clarified: **Qt MSDMT is the ONLY ground truth**. All other code (including our existing brain_core library) is suspect until verified against the original source.

**Original Source Location:**
```
excluded_from_git/Qt MSDMT Project/m188110a as used with Win, Linux MS-DMT/
  └── m188110a (Original Modem Core Source (for Qt5)/
      ├── Cm110s.cpp (24KB) - Main modem class
      ├── pxrx110a.cpp (56KB) - RX processing
      ├── ptx110a.cpp (24KB) - TX processing  
      ├── t110a.cpp (31KB) - Tables and constants
      └── (5 other source files)
```

### Constants Verification ✅

Verified Phoenix Nest constants match Qt MSDMT exactly:
- `PSYMBOL[8][8]` - Walsh-Hadamard patterns ✅
- `PSCRAMBLE[32]` - D1/D2 scrambler ✅
- `p_c_seq[9]` - Preamble count sequence ✅
- `con_symbol[8]` - Constellation points ✅
- D1/D2 mode values per Table C-VI ✅

### Brain Core Rebuilt from Source ✅

1. Created `testing/interop/build_brain_core_from_source.ps1`
2. Compiled all 9 Qt MSDMT source files with `-O1 -w` flags
3. Built `libm188110a.a` (121.8 KB) from original source
4. Copied to `extern/brain_core/lib/win64/`

### Servers Rebuilt ✅

- Fixed type mismatch in `brain_wrapper.h` (int16_t → reinterpret_cast<unsigned short*>)
- Built `brain_tcp_server.exe` (244 KB) with fresh library

### Documentation Created

- `docs/CURRENT_UNDERSTANDING_MSDMT.md` - Comprehensive Qt MSDMT internals
- Timestamp: December 12, 2025, 3:15 PM EDT

### Next Steps

1. Start Brain Core server with fresh build
2. Generate new reference PCM files (old ones deleted as suspect)
3. Run cross-modem tests (PN TX → BC RX)
4. Validate interoperability

---

## Git History Discovery (2025-12-12)

### Working Cross-Modem Test Found!

**Commit `34ba342`** (Dec 10, 2025) - "Phoenix Nest Interoperability Status Brain TX → Phoenix Nest RX: 10/12 PASS ✅"

This commit had a working Python script `test_cross_modem.py` (now deleted) that achieved:
- **10/12 modes passed** (all except 75S/75L which weren't implemented)
- Direction: **Brain TX → Phoenix Nest RX** (opposite of what we're trying now)

### Key Differences: Working vs. Current Attempt

| Working Test (Brain → PN) | Our Current Test (PN → BC) |
|---------------------------|----------------------------|
| Brain TX (48kHz) → PN RX (48kHz) | PN TX (48kHz) → BC RX (9600 Hz) |
| **Same sample rate** | **Different sample rates** - requires decimation |
| Used pre-recorded reference PCM | Live TX → RX |
| Python script | C++ client |

### Reference PCM Files Exist

Location: `refrence_pcm/` directory contains Brain-generated PCM for all 12 modes:
- `tx_600S_20251206_202518_709.pcm` (and all other modes)
- Sample rate: **48000 Hz**
- Test message: **"THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 1234567890"**

### The Core Problem (UPDATED 2025-12-12)

**Sample rate is NOT the issue!** Brain Core server accepts 48kHz and decimates internally via `decode_48k()`.

The REAL problem is **preamble/mode detection incompatibility**:
- Brain Core RX detects **1200 BPS SHORT** when Phoenix Nest sends 600S
- Brain Core RX decodes 0 bytes from PN output
- Even Brain TX → Brain RX gives garbled data (bit-reversal applied but still garbage)

### Test Results Summary

| Direction | Result | Notes |
|-----------|--------|-------|
| Brain TX → PN RX | ✅ 4/4 PASS | "THE QUICK BROWN FOX..." decoded correctly |
| PN TX → PN RX | ✅ WORKS | Loopback successful |
| PN TX → Brain RX | ❌ FAIL | BC detects wrong mode (1200S instead of 600S) |
| Brain TX → Brain RX | ❌ PARTIAL | Decodes bytes but data is garbled |

### Archive Resources

`ARCHIVE/exhaustive_tests/` contains:
- `server_backend.h` - Working ServerBackend class with proper timeout handling
- `test_gui/brain_client.h` - Clean Brain modem TCP client
- `test_gui/pn_client.h` - Clean Phoenix Nest TCP client  
- `test_gui/html_tab_interop.h` - Interop GUI (never fully implemented)

### Next Steps Options

1. ~~Restore `test_cross_modem.py`~~ - ✅ Verified Brain→PN works (4/4 pass)
2. ~~Fix sample rate issue~~ - Not the problem (BC handles 48kHz internally)
3. **Investigate preamble differences** - Why does BC detect wrong mode from PN?
4. **Check D1/D2 symbol positions** - Reference commit `cdd2995` fixed D1/D2 for external interop

---

## Architecture (from testing.instructions.md)

| Modem | Control Port | Data Port | Sample Rate |
|-------|-------------|-----------|-------------|
| Phoenix Nest | 4999 | 4998 | 48000 Hz |
| Brain Core | 3999 | 3998 | 48000 Hz (decimates internally to 9600) |

**Update:** Brain Core's `brain_tcp_server` uses `decode_48k()` which handles decimation.
No external decimation needed!

---

## ROOT CAUSE IDENTIFIED (2025-12-12)

### The Preamble Encoding Mismatch

**Phoenix Nest TX (`preamble_codec.h`):**
- Uses **simple mode_id bit repetition** at symbols 288-351
- NOT MIL-STD-188-110A compliant D1/D2 encoding

**Phoenix Nest RX (`mode_detector.h`):**
- Uses **Walsh-Hadamard PSYMBOL patterns** (MIL-STD Table C-VII)
- Uses **PSCRAMBLE scrambler** (MIL-STD Section C.5.2.1)
- D1/D2 lookup per MIL-STD Table C-VI: `{6,6}=600S`, `{6,5}=1200S`, etc.
- Looks at symbols 320-383 for D1/D2

**Brain Core (both TX and RX):**
- Uses standard MIL-STD D1/D2 Walsh encoding

**Why Each Test Case Works/Fails:**
| Direction | Result | Reason |
|-----------|--------|--------|
| Brain TX → PN RX | ✅ WORKS | Brain uses standard D1/D2, PN RX decodes it correctly |
| PN TX → PN RX | ✅ WORKS | PN has fallback decoder for its own custom format |
| PN TX → Brain RX | ❌ FAILS | PN TX uses custom format, Brain expects standard D1/D2 |
| Brain TX → Brain RX | ⚠️ GARBLED | Bit-reversal issue or interleaver mismatch |

### Fix Required

Update `preamble_codec.h` to use standard MIL-STD-188-110A D1/D2 Walsh encoding:
- D1 at symbols 320-351 (32 symbols)
- D2 at symbols 352-383 (32 symbols)
- Use PSYMBOL[d] Walsh pattern × 4 repetitions
- Apply PSCRAMBLE[32] scrambler
- D1/D2 values per Table C-VI

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
2. [x] Verify constants match Qt MSDMT ✅
3. [x] Rebuild brain_core from Qt MSDMT source ✅
4. [x] Rebuild brain_tcp_server.exe ✅
5. [x] Create CURRENT_UNDERSTANDING_MSDMT.md ✅
6. [ ] Start servers and generate reference PCM
7. [ ] Run cross-modem test
8. [ ] Document final results

**Build:** `g++ -o test_pn_to_bc.exe test_pn_to_bc.cpp -lws2_32`

---

## Previous Branch Summary (feature/tcp-server-testing)

- ✅ Created `tcp_server_base` for robust socket handling
- ✅ Phoenix Nest server passes 12/12 modes on persistent connection
- ✅ TX/RX loopback works: Sent "HELLO", decoded "HELLO"
- ✅ Merged to master
