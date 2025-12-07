# MIL-STD-188-110A Modes, Speeds, and Test Matrix

## 1. Standard Overview

MIL-STD-188-110A defines HF data modem waveforms for serial tone operation
in the 300-3000 Hz voice band. It supports multiple data rates and
interleaving modes for various channel conditions.

---

## 2. Data Rates and Modulation

### 2.1 Supported Data Rates

| Data Rate | Modulation | Bits/Symbol | Symbol Rate | Coding | Interleave |
|-----------|------------|-------------|-------------|--------|------------|
| 75 bps    | 2-PSK      | 1           | 2400 Bd     | Rate 1/2 | Available |
| 150 bps   | 2-PSK      | 1           | 2400 Bd     | Rate 1/2 | Available |
| 300 bps   | 2-PSK      | 1           | 2400 Bd     | Rate 1/2 | Available |
| 600 bps   | 4-PSK      | 2           | 2400 Bd     | Rate 1/2 | Available |
| 1200 bps  | 4-PSK      | 2           | 2400 Bd     | Rate 1/2 | Available |
| 2400 bps  | 8-PSK      | 3           | 2400 Bd     | Rate 1/2 | Available |
| 4800 bps  | 8-PSK      | 3           | 2400 Bd     | Rate 2/3 | Available |

### 2.2 Modulation Details

#### 2-PSK (BPSK)
- Constellation: 2 points at 0° and 180°
- Used for: 75, 150, 300 bps (most robust)
- Gray coding: 0 → 0°, 1 → 180°

#### 4-PSK (QPSK)
- Constellation: 4 points at 45°, 135°, 225°, 315°
- Used for: 600, 1200 bps
- Gray coding: 00→45°, 01→135°, 11→225°, 10→315°

#### 8-PSK
- Constellation: 8 points at 0°, 45°, 90°, ... 315°
- Used for: 2400, 4800 bps (highest throughput)
- Gray coding: 000→0°, 001→45°, 011→90°, etc.

### 2.3 Symbol Rate

All modes use **2400 baud** symbol rate.

- Symbol period: 416.67 µs
- Carrier frequency: 1800 Hz (center of voice band)
- Bandwidth: ~2400 Hz (with SRRC α=0.35)

---

## 3. Forward Error Correction (FEC)

### 3.1 Convolutional Code

| Parameter | Value |
|-----------|-------|
| Constraint Length (K) | 7 |
| Number of States | 64 |
| Generator Polynomial G1 | 171 (octal) = 0x79 |
| Generator Polynomial G2 | 133 (octal) = 0x5B |

**Note**: Some implementations use G1=155, G2=117 (octal). Verify against
actual standard for production use.

### 3.2 Code Rates

| Code Rate | Input Bits | Output Bits | Used For |
|-----------|------------|-------------|----------|
| Rate 1/2  | 1          | 2           | 75-2400 bps |
| Rate 2/3  | 2          | 3           | 4800 bps only |

### 3.3 Coding Gain

Typical coding gain with Viterbi decoding:

| Decoder | Coding Gain (dB) |
|---------|------------------|
| Hard Decision | 4-5 dB |
| Soft Decision | 6-7 dB |

---

## 4. Interleaving Modes

### 4.1 Interleave Options

| Mode | Duration | Purpose | Latency |
|------|----------|---------|---------|
| ZERO | None | Minimum latency | ~0 ms |
| SHORT | 0.6 sec | Low latency, moderate protection | 600 ms |
| LONG | 4.8 sec | Maximum burst error protection | 4800 ms |

### 4.2 Interleaver Parameters

| Mode | Block Size (bits) | Rows | Columns |
|------|-------------------|------|---------|
| ZERO | N/A | 1 | 1 |
| SHORT | ~1440 | 40 | 36 |
| LONG | ~11520 | 40 | 288 |

### 4.3 Burst Error Protection

| Mode | Max Correctable Burst |
|------|----------------------|
| ZERO | ~7 bits (code only) |
| SHORT | ~36 bits |
| LONG | ~288 bits |

---

## 5. Frame Structure

### 5.1 Preamble

| Segment | Duration | Purpose |
|---------|----------|---------|
| Segment | 0.2 sec (480 symbols) | Sync pattern |
| SHORT preamble | 0.6 sec (3 segments) | ZERO/SHORT modes |
| LONG preamble | 4.8 sec (24 segments) | LONG mode |

Preamble characteristics:
- Known scrambler sequence (init=0x64 for preamble)
- Enables frequency/timing acquisition
- Correlation-based detection

### 5.2 Data Frame

| Component | Symbols | Purpose |
|-----------|---------|---------|
| Data block | 32 | User data |
| Probe | 16 | Channel estimation |
| **Total frame** | **48** | Repeating unit |

Frame timing:
- Frame duration: 48 / 2400 = 20 ms
- 50 frames per second
- Probe duty cycle: 33%

### 5.3 Scrambler

| Context | Initial State |
|---------|---------------|
| Preamble | 0x64 (100 decimal) |
| Data | 0x05 (5 decimal) |

Polynomial: x^7 + x + 1 (maximal length = 127)

---

## 6. Receiver Requirements

### 6.1 Frequency Offset Tolerance

| Parameter | Requirement |
|-----------|-------------|
| Acquisition range | ±75 Hz |
| Tracking range | ±37.5 Hz |

### 6.2 Timing Requirements

| Parameter | Requirement |
|-----------|-------------|
| Clock offset tolerance | ±100 ppm |
| Timing jitter | < 5% symbol period |

### 6.3 SNR Requirements (Target BER = 10^-5)

| Data Rate | Required Eb/N0 (dB) | Required SNR (dB) |
|-----------|---------------------|-------------------|
| 75 bps    | ~6 | ~0 |
| 150 bps   | ~6 | ~3 |
| 300 bps   | ~6 | ~6 |
| 600 bps   | ~8 | ~11 |
| 1200 bps  | ~8 | ~14 |
| 2400 bps  | ~10 | ~20 |
| 4800 bps  | ~12 | ~25 |

---

## 7. Test Matrix

### 7.1 Unit Tests (Per Component)

#### 7.1.1 Scrambler Tests

| Test ID | Description | Pass Criteria |
|---------|-------------|---------------|
| SCR-001 | LFSR sequence length | Period = 127 bits |
| SCR-002 | Known sequence (init=0x64) | Match expected pattern |
| SCR-003 | Known sequence (init=0x05) | Match expected pattern |
| SCR-004 | Descramble = inverse | Original recovered |
| SCR-005 | Tribit packing | Correct bit ordering |

#### 7.1.2 Symbol Mapper Tests

| Test ID | Description | Pass Criteria |
|---------|-------------|---------------|
| MAP-001 | 2-PSK constellation points | Correct phases (0°, 180°) |
| MAP-002 | 4-PSK constellation points | Correct phases (45°, 135°, 225°, 315°) |
| MAP-003 | 8-PSK constellation points | Correct phases (0°, 45°, ..., 315°) |
| MAP-004 | Gray coding verification | Adjacent symbols differ by 1 bit |
| MAP-005 | Unit magnitude | All |symbols| = 1.0 |
| MAP-006 | Demapper inverse | Round-trip = identity |

#### 7.1.3 Convolutional Encoder Tests

| Test ID | Description | Pass Criteria |
|---------|-------------|---------------|
| ENC-001 | Initial state = 0 | state() returns 0 |
| ENC-002 | Single bit encoding | Correct G1, G2 output |
| ENC-003 | Rate 1/2 expansion | Output = 2x input |
| ENC-004 | Flush to zero state | Final state = 0 |
| ENC-005 | Known test vector | Match reference output |

#### 7.1.4 Viterbi Decoder Tests

| Test ID | Description | Pass Criteria |
|---------|-------------|---------------|
| VIT-001 | Clean channel decode | 0 errors |
| VIT-002 | Single bit error correction | Corrected |
| VIT-003 | Random errors (3 in 44 bits) | All corrected |
| VIT-004 | Burst error (5 consecutive) | All corrected |
| VIT-005 | Soft vs hard decision | Soft ≥ Hard performance |
| VIT-006 | Long block (1000 bits) | 0 errors on clean channel |
| VIT-007 | Traceback depth | Consistent output |

#### 7.1.5 Interleaver Tests

| Test ID | Description | Pass Criteria |
|---------|-------------|---------------|
| INT-001 | ZERO mode passthrough | Output = Input |
| INT-002 | SHORT mode round-trip | Deinterleave(Interleave(x)) = x |
| INT-003 | LONG mode round-trip | Deinterleave(Interleave(x)) = x |
| INT-004 | Burst spreading (SHORT) | Burst becomes distributed |
| INT-005 | Burst spreading (LONG) | Burst becomes distributed |
| INT-006 | Soft bit interleaving | Preserves values |

#### 7.1.6 Pulse Shaping Tests

| Test ID | Description | Pass Criteria |
|---------|-------------|---------------|
| PSH-001 | SRRC filter symmetry | Symmetric impulse response |
| PSH-002 | SRRC rolloff (α=0.35) | Correct spectral shape |
| PSH-003 | ISI at symbol points | Zero ISI (Nyquist) |
| PSH-004 | Filter delay | Correct group delay |
| PSH-005 | Energy normalization | Unit energy |

#### 7.1.7 NCO Tests

| Test ID | Description | Pass Criteria |
|---------|-------------|---------------|
| NCO-001 | Frequency accuracy | Error < 0.01 Hz |
| NCO-002 | Phase continuity | No discontinuities |
| NCO-003 | Frequency change | Immediate response |
| NCO-004 | Complex output | |output| = 1.0 |
| NCO-005 | Long-term stability | No drift over 10 sec |

---

### 7.2 Synchronization Tests

#### 7.2.1 Preamble Detection

| Test ID | Description | Pass Criteria |
|---------|-------------|---------------|
| PRE-001 | Detection on clean channel | Detected within 0.2 sec |
| PRE-002 | Detection with AWGN (10 dB) | Detection probability > 99% |
| PRE-003 | Detection with AWGN (5 dB) | Detection probability > 90% |
| PRE-004 | Frequency offset ±25 Hz | Detected and estimated |
| PRE-005 | Frequency offset ±50 Hz | Detected and estimated |
| PRE-006 | Frequency offset ±75 Hz | Detected (at threshold) |
| PRE-007 | False alarm rate | < 10^-4 per second |
| PRE-008 | SHORT preamble | Sync acquired |
| PRE-009 | LONG preamble | Sync acquired |

#### 7.2.2 Timing Recovery

| Test ID | Description | Pass Criteria |
|---------|-------------|---------------|
| TIM-001 | Lock on clean channel | Lock within 100 symbols |
| TIM-002 | Arbitrary timing offset | Converges to correct |
| TIM-003 | Clock offset +100 ppm | Tracks continuously |
| TIM-004 | Clock offset -100 ppm | Tracks continuously |
| TIM-005 | Timing jitter (5%) | Maintains lock |
| TIM-006 | Symbol output rate | Exactly 2400 sps |
| TIM-007 | With AWGN (10 dB) | Maintains lock |

#### 7.2.3 Carrier Recovery

| Test ID | Description | Pass Criteria |
|---------|-------------|---------------|
| CAR-001 | Lock on clean channel | Lock within 50 symbols |
| CAR-002 | Phase offset 0-360° | Converges |
| CAR-003 | Frequency offset ±10 Hz | Tracks |
| CAR-004 | Frequency offset ±25 Hz | Tracks |
| CAR-005 | Phase noise (1° RMS) | Maintains lock |
| CAR-006 | Cycle slip detection | No slips at 15 dB |
| CAR-007 | With AWGN (10 dB) | Maintains lock |

---

### 7.3 Equalization Tests

#### 7.3.1 DFE Equalizer

| Test ID | Description | Pass Criteria |
|---------|-------------|---------------|
| EQ-001 | Flat channel | Unity response |
| EQ-002 | Single echo (0.5 @ 1 sym) | Equalizes to < -20 dB |
| EQ-003 | Single echo (0.3 @ 2 sym) | Equalizes to < -20 dB |
| EQ-004 | Two echoes | Equalizes both |
| EQ-005 | Probe-based training | Converges in 1 frame |
| EQ-006 | Decision-directed tracking | Maintains performance |
| EQ-007 | Doppler spread (1 Hz) | Tracks channel |
| EQ-008 | LMS step size adaptation | Stable convergence |

---

### 7.4 End-to-End Tests

#### 7.4.1 Loopback Tests (AWGN Channel)

| Test ID | Data Rate | SNR (dB) | Interleave | Pass Criteria |
|---------|-----------|----------|------------|---------------|
| E2E-001 | 2400 bps  | 25       | ZERO       | BER < 10^-5 |
| E2E-002 | 2400 bps  | 20       | ZERO       | BER < 10^-4 |
| E2E-003 | 2400 bps  | 15       | ZERO       | BER < 10^-3 |
| E2E-004 | 2400 bps  | 20       | SHORT      | BER < 10^-5 |
| E2E-005 | 2400 bps  | 20       | LONG       | BER < 10^-5 |
| E2E-006 | 1200 bps  | 15       | ZERO       | BER < 10^-5 |
| E2E-007 | 600 bps   | 12       | ZERO       | BER < 10^-5 |
| E2E-008 | 300 bps   | 8        | ZERO       | BER < 10^-5 |
| E2E-009 | 75 bps    | 5        | ZERO       | BER < 10^-5 |

#### 7.4.2 Loopback Tests (Multipath Channel)

| Test ID | Channel Model | Data Rate | Interleave | Pass Criteria |
|---------|---------------|-----------|------------|---------------|
| E2E-010 | CCIR Poor | 2400 bps | SHORT | BER < 10^-3 |
| E2E-011 | CCIR Poor | 2400 bps | LONG | BER < 10^-4 |
| E2E-012 | CCIR Good | 2400 bps | ZERO | BER < 10^-4 |
| E2E-013 | Watterson 1Hz | 2400 bps | SHORT | Sync maintained |
| E2E-014 | Watterson 2Hz | 1200 bps | LONG | Sync maintained |

#### 7.4.3 Stress Tests

| Test ID | Description | Duration | Pass Criteria |
|---------|-------------|----------|---------------|
| STR-001 | Continuous operation | 1 hour | No memory leaks |
| STR-002 | Repeated sync/desync | 100 cycles | All reacquire |
| STR-003 | Random message lengths | 1000 msgs | All decoded |
| STR-004 | Frequency hopping sim | 10 hops | All reacquire |
| STR-005 | CPU load at 2x real-time | 1 min | No underruns |

---

### 7.5 Performance Measurement Tests

#### 7.5.1 BER vs SNR Curves

| Test ID | Data Rate | SNR Range | Points | Output |
|---------|-----------|-----------|--------|--------|
| PER-001 | 75 bps    | 0-15 dB   | 16     | BER curve |
| PER-002 | 300 bps   | 3-18 dB   | 16     | BER curve |
| PER-003 | 1200 bps  | 8-23 dB   | 16     | BER curve |
| PER-004 | 2400 bps  | 12-27 dB  | 16     | BER curve |
| PER-005 | 4800 bps  | 18-33 dB  | 16     | BER curve |

#### 7.5.2 Acquisition Time

| Test ID | Condition | Target | Measurement |
|---------|-----------|--------|-------------|
| ACQ-001 | Clean, no offset | < 1 sec | Mean time |
| ACQ-002 | 10 dB SNR | < 2 sec | Mean time |
| ACQ-003 | +50 Hz offset | < 2 sec | Mean time |
| ACQ-004 | Multipath | < 3 sec | Mean time |

#### 7.5.3 Coding Gain Verification

| Test ID | Condition | Expected | Measurement |
|---------|-----------|----------|-------------|
| COD-001 | Hard decision | 4-5 dB | Measure at BER=10^-4 |
| COD-002 | Soft decision | 6-7 dB | Measure at BER=10^-4 |

---

### 7.6 Compatibility Tests

#### 7.6.1 Reference Waveform Tests

| Test ID | Description | Source | Pass Criteria |
|---------|-------------|--------|---------------|
| REF-001 | Decode reference preamble | Standard | Sync acquired |
| REF-002 | Decode reference data | Standard | Exact match |
| REF-003 | Generate matching preamble | Standard | Correlation > 0.99 |
| REF-004 | Generate matching data | Standard | Bit-exact |

#### 7.6.2 Interoperability

| Test ID | Other Implementation | Test |
|---------|---------------------|------|
| IOP-001 | FreeDV (if available) | TX/RX exchange |
| IOP-002 | Military radio | Field test |
| IOP-003 | SDR implementation | Lab test |

---

## 8. Test Execution Checklist

### 8.1 Phase 1: Unit Tests
- [ ] SCR-001 through SCR-005 (Scrambler)
- [ ] MAP-001 through MAP-006 (Symbol Mapper)
- [ ] ENC-001 through ENC-005 (Encoder)
- [ ] VIT-001 through VIT-007 (Viterbi)
- [ ] INT-001 through INT-006 (Interleaver)
- [ ] PSH-001 through PSH-005 (Pulse Shaping)
- [ ] NCO-001 through NCO-005 (NCO)

### 8.2 Phase 2: Synchronization Tests
- [ ] PRE-001 through PRE-009 (Preamble)
- [ ] TIM-001 through TIM-007 (Timing)
- [ ] CAR-001 through CAR-007 (Carrier)

### 8.3 Phase 3: Equalization Tests
- [ ] EQ-001 through EQ-008 (DFE)

### 8.4 Phase 4: End-to-End Tests
- [ ] E2E-001 through E2E-014 (Loopback)
- [ ] STR-001 through STR-005 (Stress)

### 8.5 Phase 5: Performance Characterization
- [ ] PER-001 through PER-005 (BER curves)
- [ ] ACQ-001 through ACQ-004 (Acquisition)
- [ ] COD-001 through COD-002 (Coding gain)

### 8.6 Phase 6: Compatibility
- [ ] REF-001 through REF-004 (Reference)
- [ ] IOP-001 through IOP-003 (Interop)

---

## 9. Test Environment Requirements

### 9.1 Software

| Component | Version | Purpose |
|-----------|---------|---------|
| GCC/Clang | C++17 | Compilation |
| CMake | 3.10+ | Build system |
| Python | 3.8+ | Test scripting |
| NumPy | Latest | Reference calculations |
| Matplotlib | Latest | Result plotting |

### 9.2 Hardware (Optional)

| Equipment | Purpose |
|-----------|---------|
| Sound card | Audio loopback |
| SDR (RTL-SDR, HackRF) | RF testing |
| HF transceiver | Field testing |
| Audio analyzer | Signal quality |

### 9.3 Test Data

| Item | Size | Purpose |
|------|------|---------|
| Random bit sequences | 10 MB | BER testing |
| Text files (ASCII) | 1 MB | Message testing |
| Reference waveforms | 100 MB | Compatibility |
| Channel recordings | 1 GB | Real-world testing |

---

## 10. Reporting Template

### Test Report Header
```
Test ID: ____________
Date: ____________
Tester: ____________
Software Version: ____________
```

### Test Results
```
Test Case: ____________
Conditions: ____________
Expected: ____________
Actual: ____________
Status: [ ] PASS  [ ] FAIL  [ ] SKIP
Notes: ____________
```

### Summary Statistics
```
Total Tests: ____
Passed: ____
Failed: ____
Skipped: ____
Pass Rate: ____%
```

---

## Appendix A: Channel Models

### CCIR Good
- 2-path model
- Delay: 0.5 ms
- Doppler: 0.1 Hz

### CCIR Poor  
- 2-path model
- Delay: 2.0 ms
- Doppler: 1.0 Hz

### Watterson Model
- Statistical fading
- Configurable Doppler spread
- Gaussian tap gains

---

## Appendix B: Reference Test Vectors

### B.1 Scrambler (init=0x64)
```
First 21 bits: 0 0 1 0 0 1 1 0 1 0 1 1 1 1 1 0 0 0 0 1 0
First 7 tribits: 4 6 5 7 6 0 2
```

### B.2 Encoder (input = 0xAB)
```
Input:  1 0 1 0 1 0 1 1 (0xAB)
Output: 11 10 00 01 00 01 00 10 (+ flush)
```

### B.3 8-PSK Constellation
```
Symbol 0: 1.000 + 0.000j  (0°)
Symbol 1: 0.707 + 0.707j  (45°)
Symbol 2: 0.000 + 1.000j  (90°)
Symbol 3: -0.707 + 0.707j (135°)
Symbol 4: -1.000 + 0.000j (180°)
Symbol 5: -0.707 - 0.707j (225°)
Symbol 6: 0.000 - 1.000j  (270°)
Symbol 7: 0.707 - 0.707j  (315°)
```

---

*Document Version: 1.0*
*Last Updated: 2025-12-04*
