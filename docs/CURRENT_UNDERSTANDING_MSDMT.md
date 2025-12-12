# Current Understanding of Qt MSDMT Modem Implementation

**Document Created:** December 12, 2025, 3:15 PM Eastern Daylight Time  
**Author:** GitHub Copilot (Claude Opus 4.5)  
**Purpose:** Comprehensive documentation of Qt MSDMT modem internals based on original source code analysis

---

## Executive Summary

This document captures my complete understanding of how the Qt MSDMT modem (m188110a / Brain Core) works, derived from reading the **original, untouched source code** in:

```
excluded_from_git/Qt MSDMT Project/m188110a as used with Win, Linux MS-DMT/
```

This is the ONLY ground truth we can trust. Everything else in our project (brain_core wrapper, reference PCM files, our Phoenix Nest implementation) is derived and therefore suspect.

---

## Part 1: Preamble Structure (MIL-STD-188-110A)

### 1.1 Frame Layout

Each preamble frame is **480 symbols** at 2400 baud:

| Segment | Start Symbol | Length | Duration | Purpose |
|---------|--------------|--------|----------|---------|
| Common | 0 | 288 | 120 ms | Sync, AGC, carrier lock |
| Mode (D1+D2) | 288 | 64 | 26.7 ms | Mode identification |
| Count | 352 | 96 | 40 ms | Frame countdown |
| Zero | 448 | 32 | 13.3 ms | Padding |
| **Total** | - | **480** | **200 ms** | One frame |

### 1.2 Interleave Types

| Type | Frames | Total Preamble | Countdown |
|------|--------|----------------|-----------|
| Short | 3 | 1,440 symbols (600 ms) | 2, 1, 0 |
| Long | 24 | 11,520 symbols (4.8 s) | 23, 22, ... 1, 0 |

### 1.3 Common Segment Generation

From `t110a.cpp` lines 234-259:

```cpp
int p_c_seq[9] = {0, 1, 3, 0, 1, 3, 1, 2, 0};

// Generate 288 symbols: 9 groups × 32 symbols each
for (j = 0; j < 9; j++) {
    for (k = 0; k < 32; k++) {
        // Get base pattern from psymbol table
        sym = psymbol[p_c_seq[j]][k % 8];
        // Apply scrambler
        sym = (sym + pscramble[k]) % 8;
        // Convert to complex constellation point
        p_common[index++] = con_symbol[sym];
    }
}
```

**Key Constants:**
- `p_c_seq[9]` = {0, 1, 3, 0, 1, 3, 1, 2, 0}
- Each D value maps to an 8-symbol Walsh pattern
- Pattern repeated 4× for 32 symbols
- Scrambler applied with 32-symbol period

---

## Part 2: D1/D2 Mode Identification

### 2.1 D1/D2 Values Per Mode

From `MIL-STD-188-110A_Mode_Parameters.md` (Qt MSDMT documentation):

| Mode Index | Mode | D1 | D2 | Interleave |
|------------|------|----|----|------------|
| 0 | M75NS | - | - | Short |
| 1 | M75NL | - | - | Long |
| 2 | M150S | 7 | 4 | Short |
| 3 | M150L | 5 | 4 | Long |
| 4 | M300S | 6 | 7 | Short |
| 5 | M300L | 4 | 7 | Long |
| 6 | M600S | 6 | 6 | Short |
| 7 | M600L | 4 | 6 | Long |
| 8 | M1200S | 6 | 5 | Short |
| 9 | M1200L | 4 | 5 | Long |
| 10 | M2400S | 6 | 4 | Short |
| 11 | M2400L | 4 | 4 | Long |
| 17 | M4800S | 7 | 6 | Short |

**Pattern:** Short interleave uses D1=6 or 7, Long uses D1=4 or 5.

### 2.2 Walsh-Hadamard Patterns (PSYMBOL)

From `t110a.cpp` lines 85-94:

```cpp
int psymbol[8][32] = {
    {0,0,0,0,0,0,0,0},  // D=0
    {0,4,0,4,0,4,0,4},  // D=1
    {0,0,4,4,0,0,4,4},  // D=2
    {0,4,4,0,0,4,4,0},  // D=3
    {0,0,0,0,4,4,4,4},  // D=4
    {0,4,0,4,4,0,4,0},  // D=5
    {0,0,4,4,4,4,0,0},  // D=6
    {0,4,4,0,4,0,0,4}   // D=7
};
```

Values are 0 or 4 (representing 0° or 180° phase shift - BPSK encoding within 8-PSK).

### 2.3 Preamble Scrambler (PSCRAMBLE)

From `t110a.cpp` lines 96-99:

```cpp
int pscramble[32] = {
    7,4,3,0,5,1,5,0,2,2,1,1,5,7,4,3,
    5,0,2,6,2,1,6,2,0,0,5,0,5,2,6,6
};
```

### 2.4 D Pattern Encoding Function

From `ptx110a.cpp` lines 100-127 (`create_d_preamble_sequence`):

```cpp
int Cm110s::create_d_preamble_sequence(int d, FComplex *buff) {
    int seq[32];
    int i, j, k, index = 0;
    
    // Step 1: Generate 32 symbols from Walsh pattern (4 reps × 8 symbols)
    for (j = 0; j < 4; j++) {
        for (k = 0; k < 8; k++) {
            seq[index++] = psymbol[d][k];
        }
    }
    
    // Step 2: Apply scrambling (CRITICAL: starts from pscramble[0])
    for (i = 0; i < index; i++) {
        seq[i] = (seq[i] + pscramble[i % 32]) % 8;
    }
    
    // Step 3: Convert to constellation symbols
    for (i = 0; i < index; i++) {
        buff[i] = con_symbol[seq[i]];
    }
    
    return index;  // Returns 32
}
```

**Critical Detail:** Each D pattern (D1 and D2) independently starts scrambling from `pscramble[0]`. They don't continue from each other.

---

## Part 3: 8-PSK Constellation

From `t110a.cpp` lines 101-111:

```cpp
FComplex con_symbol[8] = {
    { 1.000f,  0.000f},  // Symbol 0:   0° (I=1, Q=0)
    { 0.707f,  0.707f},  // Symbol 1:  45°
    { 0.000f,  1.000f},  // Symbol 2:  90°
    {-0.707f,  0.707f},  // Symbol 3: 135°
    {-1.000f,  0.000f},  // Symbol 4: 180°
    {-0.707f, -0.707f},  // Symbol 5: 225°
    { 0.000f, -1.000f},  // Symbol 6: 270°
    { 0.707f, -0.707f}   // Symbol 7: 315°
};
```

Phase mapping: Symbol N → N × 45°

---

## Part 4: TX Signal Flow

### 4.1 High-Level TX State Machine

From `txm110a.cpp`:

```
TX_IDLE_STATE
    ↓ (load first interleaver block)
TX_PREAMBLE_STATE
    ↓ (transmit all preamble frames)
TX_DATA_STATE
    ↓ (transmit data with probe sequences)
TX_CLOSING_STATE
    ↓ (8 symbols silence, clear filter)
TX_IDLE_STATE
```

### 4.2 TX Initialization (From modemservice.cpp)

```cpp
m_txModem->tx_set_soundblock_size(1920);  // 200ms at 9600 Hz
m_txModem->tx_enable();
m_txModem->rx_enable();  // Even TX modem has RX enabled!
m_txModem->set_psk_carrier(1800);
m_txModem->set_preamble_hunt_squelch(8);  // "None"
m_txModem->set_p_mode(1);
m_txModem->set_e_mode(0);
m_txModem->set_b_mode(0);
m_txModem->m_eomreset = 0;
m_txModem->eom_rx_reset();
```

### 4.3 Audio Generation

From `txm110a.cpp`:

1. **TX Filter:** 32-tap RRC (Root Raised Cosine)
2. **Sample Rate:** 9600 Hz internal
3. **Symbol Rate:** 2400 baud (4 samples/symbol)
4. **Carrier:** 1800 Hz (configurable: 1650, 1500)
5. **Block Size:** 1920 samples (200 ms)

```cpp
void Cm110s::tx_symbol(FComplex sym) {
    // Apply TX filter (RRC)
    // Upconvert to carrier frequency
    // Add to output buffer
}
```

### 4.4 Silence Insertion

Before preamble: 8 symbols of zeros to clear TX filter:
```cpp
void Cm110s::tx_silence(int symbols) {
    for (int i = 0; i < symbols; i++) {
        tx_symbol({0.0f, 0.0f});
    }
}
```

---

## Part 5: RX Signal Flow

### 5.1 RX Processing

From `modemservice.cpp`:

```cpp
void ModemService::onAudioSamplesReady(const QByteArray &samples) {
    const int16_t *data = reinterpret_cast<const int16_t*>(samples.constData());
    int numSamples = samples.size() / sizeof(int16_t);
    m_rxModem->rx_process_block(const_cast<int16_t*>(data), numSamples);
}
```

### 5.2 RX Initialization

```cpp
m_rxModem->rx_enable();
m_rxModem->set_psk_carrier(1800);
m_rxModem->set_preamble_hunt_squelch(8);
m_rxModem->set_p_mode(1);
m_rxModem->set_e_mode(0);
m_rxModem->set_b_mode(0);
```

### 5.3 Sample Types

- **Input:** int16_t at 9600 Hz (or 48000 Hz decimated 5:1)
- **Processing:** float internally
- **Output:** uint8_t octets via callback

---

## Part 6: Our Implementation Status

### 6.1 What Matches Qt MSDMT ✅

| Component | File | Status |
|-----------|------|--------|
| PSYMBOL Walsh patterns | brain_preamble.h, preamble_codec.h, mode_detector.h | ✅ MATCHES |
| PSCRAMBLE sequence | brain_preamble.h, preamble_codec.h, mode_detector.h | ✅ MATCHES |
| p_c_seq common sequence | brain_preamble.h | ✅ MATCHES |
| 8-PSK constellation | All files | ✅ MATCHES |
| D1/D2 values per mode | brain_preamble.h, mode_detector.h | ✅ MATCHES |
| Preamble structure (288/64/96/32) | All files | ✅ MATCHES |
| D pattern scrambling (restart at 0) | By accident (288%32=0) | ✅ MATCHES |

### 6.2 What Differs or Is Suspect ⚠️

| Issue | Description | Impact |
|-------|-------------|--------|
| **Leading Symbols** | Phoenix Nest adds 288 extra symbols before preamble | High - shifts D1/D2 positions |
| **brain_wrapper.h init** | Missing Qt MSDMT initialization parameters | High - Brain Core may not decode properly |
| **Reference PCM files** | Deleted - were generated by unvalidated build | N/A - no longer available |
| **brain_core library** | Derived from Qt MSDMT but process uncertain | Medium - should rebuild from source |
| **Dual modem instances** | Qt MSDMT uses separate RX/TX Cm110s | Unknown - might be necessary |

### 6.3 What We Fixed Today

1. **D1/D2 positions in mode_detector.h:** Changed from 320/352 to 288/320
2. **Preamble structure in preamble_codec.h:** Changed common from 320 to 288 symbols
3. **brain_wrapper.h initialization:** Added Qt MSDMT parameters (carrier, squelch, modes)
4. **Leading symbols:** Disabled in phoenix_tcp_server.h for interop

---

## Part 7: Recommended Path Forward

### 7.1 Immediate Actions

1. **Rebuild brain_core from Qt MSDMT source**
   - Use the original source in `excluded_from_git/Qt MSDMT Project/m188110a as used with Win, Linux MS-DMT/`
   - Ensure the build process is clean and documented
   - Keep this directory out of git (already excluded)

2. **Generate new reference PCM files**
   - Use the freshly built brain_core
   - Document exactly which source code version was used
   - Store metadata with each file

3. **Validate Phoenix Nest TX**
   - Generate PN TX output
   - Demodulate with freshly built brain_core
   - Compare symbol-by-symbol with expected preamble

### 7.2 Testing Protocol

1. **Unit test:** PN preamble encoder → PN mode detector (already passes 22/22)
2. **Integration test:** PN TX → Brain Core RX (with fresh brain_core build)
3. **Round-trip test:** Brain Core TX → PN RX (known working direction)
4. **Loopback test:** PN TX → PN RX (sanity check)

### 7.3 Source Code to Use

The authoritative source files are:

```
excluded_from_git/Qt MSDMT Project/m188110a as used with Win, Linux MS-DMT/
├── t110a.cpp      - Constants (psymbol, pscramble, con_symbol)
├── ptx110a.cpp    - Preamble TX (create_d_preamble_sequence)
├── txm110a.cpp    - TX state machine
├── rxm110a.cpp    - RX processing
├── g110a.cpp      - Main entry points (tx_sync_frame_eom)
├── Cm110s.h       - Header with all structures
└── ...
```

---

## Part 8: Key Equations

### 8.1 D Pattern Symbol Generation

For D value `d` (0-7), generate 32 symbols:

```
for i in 0..31:
    walsh_idx = i % 8
    base = psymbol[d][walsh_idx]      // 0 or 4
    scrambled = (base + pscramble[i]) % 8
    symbol = con_symbol[scrambled]    // Complex I+jQ
```

### 8.2 Symbol to Phase

```
phase = symbol_index × 45°
I = cos(phase)
Q = sin(phase)
```

### 8.3 Sample Rate Relationships

```
Modem sample rate: 9600 Hz
Symbol rate: 2400 baud
Samples per symbol: 4
Audio sample rate: 48000 Hz (5:1 ratio)
```

---

## Part 9: Open Questions

1. **Why dual Cm110s instances?** Qt MSDMT uses separate RX and TX modems. Is this required for state isolation, or just architectural preference?

2. **m_eomreset purpose?** Set to 0 in initialization. What does it control?

3. **set_p_mode(1) meaning?** What does p_mode=1 enable vs p_mode=0?

4. **set_preamble_hunt_squelch(8)?** Value 8 = "None" per comments. What are other valid values?

---

## Appendix A: File Locations

### Qt MSDMT Original Source (GROUND TRUTH)
```
d:\pennington_m110a_demod\excluded_from_git\Qt MSDMT Project\
├── m188110a as used with Win, Linux MS-DMT\    ← Original modem core
├── ms-dmt-backend\                              ← Qt application wrapper
└── ...
```

### Our Implementation (DERIVED)
```
d:\pennington_m110a_demod\
├── src\m110a\brain_preamble.h      ← Preamble encoder (matches Qt MSDMT)
├── src\m110a\preamble_codec.h      ← Alternative encoder (matches Qt MSDMT)
├── src\m110a\mode_detector.h       ← D1/D2 detector (matches Qt MSDMT)
├── extern\brain_wrapper.h          ← Brain Core API wrapper (UPDATED)
├── server\phoenix_tcp_server.h     ← PN TCP server (UPDATED)
└── server\brain_tcp_server.h       ← BC TCP server
```

### Brain Core (DERIVED - NEEDS REBUILD)
```
d:\pennington_m110a_demod\extern\brain_core\
├── include\m188110a\Cm110s.h       ← API header
├── lib\win64\libm188110a.a         ← Compiled library (suspect)
└── src\main.cpp                    ← Server main
```

---

*End of Document*

**Next Steps:** Rebuild brain_core from Qt MSDMT source, generate fresh reference PCM files, run cross-modem validation tests.
