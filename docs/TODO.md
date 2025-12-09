# MIL-STD-188-110A Demodulator - TODO Tracker

**Last Updated:** 2025-12-07

---

## ✅ COMPLETED

### Phase 1: Core Infrastructure (2025-12-04)
- [x] Project setup and build system
- [x] NCO (Numerically Controlled Oscillator)
- [x] FIR filter with SRRC coefficients
- [x] Farrow resampler/interpolator
- [x] Symbol mapper (BPSK/QPSK/8PSK)
- [x] Scrambler (12-bit LFSR, MS-DMT compatible)
- [x] Test transmitter

### Phase 2: Synchronization (2025-12-04)
- [x] Preamble detector (correlation-based)
- [x] Timing recovery (Gardner TED)
- [x] Carrier recovery (PLL-based)
- [x] Frequency search/AFC
- [x] Adaptive timing recovery V2

### Phase 3: FEC (2025-12-04)
- [x] Convolutional encoder (K=7, G1=0x5B, G2=0x79)
- [x] Viterbi decoder (soft decision)
- [x] Traceback algorithm
- [x] 8-PSK soft demapper

### Phase 4: Interleaver (2025-12-04)
- [x] Block interleaver (helical)
- [x] Multi-mode interleaver (all configurations)
- [x] SHORT/LONG/ZERO modes

### Phase 5: Mode Support (2025-12-05 to 2025-12-07)
- [x] Mode database (16 modes)
- [x] D1/D2 preamble mode detection
- [x] M2400S/L decode
- [x] M1200S/L decode
- [x] M600S/L decode
- [x] M300S/L decode
- [x] M150S/L decode
- [x] M4800S uncoded mode
- [x] Symbol repetition handling (150/300/600 bps)

### Phase 6: Equalizers (2025-12-06)
- [x] DFE (Decision Feedback Equalizer)
- [x] MLSE Viterbi equalizer (L=2, L=3)
- [x] DDFSE reduced-state
- [x] SOVA soft outputs
- [x] Probe-based channel estimation

### Phase 7: Channel Models (2025-12-06)
- [x] AWGN channel
- [x] Static multipath
- [x] Watterson HF fading
- [x] CCIR channel profiles

### Phase 8: MS-DMT Compatibility (2025-12-06 to 2025-12-07)
- [x] Scrambler polynomial fix
- [x] FEC generator swap (G1↔G2)
- [x] Gray code tables
- [x] LSB-first bit ordering
- [x] Interleaver parameter corrections
- [x] Reference file decode (M150-M2400)

### Phase 9: API Layer (2025-12-07)
- [x] Result<T> error handling pattern
- [x] Error codes and messages
- [x] Mode enumeration and helpers
- [x] TxConfig / RxConfig structures
- [x] Config builders (fluent API)
- [x] ModemTX class (PIMPL, thread-safe)
- [x] ModemRX class (PIMPL, thread-safe)
- [x] PCM/WAV file I/O helpers
- [x] Convenience functions (encode/decode)
- [x] API documentation

---

## ⚠️ IN PROGRESS

### M75 Walsh Mode
- [x] Walsh sequence tables (MNS/MES)
- [x] Walsh correlator
- [x] Gray decode (mgd2)
- [x] Scrambler integration
- [x] Loopback test (working)
- [ ] **Real file decode** ← BLOCKED
- [ ] sync_75_mask implementation
- [ ] MES/MNS block synchronization
- [ ] Symbol timing alignment (4800 Hz vs 2400 Hz)

### API TX Integration
- [x] Interface defined
- [x] Skeleton implementation
- [x] **Integrated with M110ACodec**
- [x] **encode_with_probes() for continuous scrambler**
- [x] **Loopback test passing - 11/11 modes**

---

## 📋 TODO (Future)

### Next Up (Session 19+)

#### 1. FFT-Based Coarse AFC (HIGH PRIORITY)
**Effort:** 8-12 hours | **Impact:** ±10 Hz spec compliance  
**Status:** Preamble-only AFC limited to ±2 Hz (Build 83)

Current implementation achieves:
- ✓ 100% pass at 0 Hz offset
- ✓ 75-100% pass at ±1 Hz
- ✗ 0% pass at ±3 Hz or ±5 Hz

MIL-STD-188-110A requires ±10 Hz frequency tolerance.

**Implementation Plan:**
- [ ] FFT coarse estimate: Search ±10 Hz, ~2 Hz accuracy
- [ ] Preamble fine search: ±2 Hz around coarse estimate
- [ ] Two-stage acquisition: Coarse → Fine → Decode
- [ ] Test with foff_5hz, foff_10hz channels
- [ ] Verify 80%+ pass rate at ±5 Hz, 60%+ at ±10 Hz

**References:**
- `docs/AFC_ROOT_CAUSE.md` - Investigation results
- `docs/afc_implementation_plan.md` - Original AFC design
- Standard HF modem techniques (pilot tones, decision-directed loops)

**Alternative approaches** (if FFT coarse fails):
- Decision-directed carrier tracking loop (12-16 hours)
- Pilot tone tracking (requires TX modification, 16-24 hours)

#### 2. EOM (End of Message) Marker ✓ COMPLETE
**Effort:** 3-4 hours | **Impact:** Protocol completeness
- [x] TX: Add `generate_eom()` - 4 flush frames with scrambled zeros
- [x] RX: Add `detect_eom()` - count trailing zeros (threshold 40+)
- [x] Wire up `TxConfig.include_eom` flag (default: true)
- [x] Add `DecodeResult.eom_detected` flag
- [x] Test with clean + noisy channels (5/5 tests passing)

#### 3. Adaptive LMS Step Size ✓ COMPLETE
**Effort:** 4 hours | **Impact:** Performance improvement
- [x] Implement NLMS: `μ_eff = μ / (δ + ||x||²)`
- [x] Add `RxConfig.use_nlms` flag  
- [x] Different mu values for LMS vs NLMS modes
- [x] Test: NLMS vs fixed on CCIR Good/Moderate
- [x] Result: NLMS wins 5/6 tests, 10-16% BER improvement

#### 3. Streaming RX (Real-time)
**Effort:** 16 hours | **Impact:** Real-time capability
- [ ] Ring buffer for sample accumulation
- [ ] `StreamingPreambleDetector` - sliding correlation
- [ ] `StreamingDemodulator` - symbol-by-symbol output
- [ ] `StreamingDecoder` - incremental Viterbi
- [ ] `StreamingRX` API class
- [ ] Test with 256-sample chunks

#### 4. Documentation Cleanup ✓ COMPLETE
**Effort:** ~4 hours | **Impact:** Maintainability
- [x] `docs/README.md` - project overview + quick start
- [x] `docs/API.md` - ModemTX/RX reference
- [x] `docs/TX_CHAIN.md` - transmitter pipeline
- [x] Renamed to `docs/RX_CHAIN.md` - receiver pipeline
- [x] `docs/EQUALIZERS.md` - DFE/MLSE/NLMS details
- [x] `docs/PROTOCOL.md` - MIL-STD-188-110A spec
- [x] `examples/` directory with working code
  - simple_loopback.cpp
  - mode_comparison.cpp
  - channel_test.cpp
  - auto_detect.cpp

**See `docs/TODO_DETAILED.md` for full implementation specs.**

### Other Items

#### High Priority
- [ ] Debug M75 real file decode
- [ ] Add M75NL mode testing

#### Medium Priority
- [ ] M4800L mode (long interleave uncoded)
- [ ] Voice modes (M75V, M150V, etc.)
- [ ] CLI interface improvements

#### Low Priority
- [ ] GUI interface
- [ ] Doppler compensation
- [ ] ARM/embedded port

---

## 🐛 KNOWN ISSUES

### Issue #1: M75 Walsh Real File Decode
**Status:** Open  
**Priority:** Medium  
**Description:** Loopback works perfectly but real PCM files produce garbage despite strong Walsh correlations.

**Investigated:**
- Scrambler matches reference ✓
- MNS/MES patterns correct ✓
- Gray decode verified ✓
- Searched 20000 symbol offsets ✓
- Searched 160 scrambler offsets ✓
- Tried bit inversions ✓
- Tried Gray mapping variations ✓

**Suspected Causes:**
1. Reference uses `in[i*2]` indexing (4800 Hz input), we duplicate 2400 Hz
2. sync_75_mask adaptive weighting not implemented
3. MES block boundary not synchronized

**Files:**
- `src/m110a/walsh_75_decoder.h`
- `test/test_walsh_75_decoder.cpp`
- `docs/M75_WALSH_ALGORITHM.md`

---

## 📊 TEST COVERAGE

| Category | Tests | Passing | Status |
|----------|-------|---------|--------|
| Core DSP | 7 | 7 | ✅ |
| Timing | 7 | 7 | ✅ |
| Viterbi | 5 | 5 | ✅ |
| Interleaver | 4 | 4 | ✅ |
| Scrambler | 3 | 3 | ✅ |
| Mode Detect | 6 | 6 | ✅ |
| Equalizer | 6 | 6 | ✅ |
| Channel | 5 | 5 | ✅ |
| E2E Loopback | 21 | 21 | ✅ |
| M75 Walsh | 10 | 10 | ⚠️ Loopback only |
| **Total** | **74** | **74** | ✅ |

---

## 📅 MILESTONES

| Milestone | Target | Status |
|-----------|--------|--------|
| Core infrastructure | 2025-12-04 | ✅ Complete |
| Synchronization | 2025-12-04 | ✅ Complete |
| FEC working | 2025-12-04 | ✅ Complete |
| First decode | 2025-12-06 | ✅ M2400S 54/54 |
| All PSK modes | 2025-12-07 | ✅ 20/20 pass |
| M75 Walsh | TBD | ⚠️ Partial |
| Production ready | TBD | 📋 Pending |

---

## 📝 NOTES

### Reference Files Location
```
/mnt/user-data/uploads/tx_*_*.pcm
```

### Build Command
```bash
cd /home/claude/m110a_demod
g++ -std=c++17 -O2 -I src -o test/test_name test/test_name.cpp
```

### Run All Tests
```bash
cd /home/claude/m110a_demod
for f in test/*.cpp; do
  g++ -std=c++17 -O2 -I src -o /tmp/test "$f" 2>/dev/null && /tmp/test
done
```

---

*Last updated: 2025-12-07 Session 19*

**Session 19 completed:**
- EOM marker implementation (TX+RX)
- NLMS adaptive equalizer (10-16% improvement)
- Full documentation package
- 4 working examples
- 46/46 tests passing
