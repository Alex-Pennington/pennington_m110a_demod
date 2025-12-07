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

### High Priority
- [ ] Debug M75 real file decode
- [ ] Add M75NL mode testing
- [ ] Integrate Walsh decoder into MultiModeRx

### Medium Priority
- [ ] M4800L mode (long interleave uncoded)
- [ ] Voice modes (M75V, M150V, etc.)
- [ ] Real-time streaming decode
- [ ] CLI interface improvements
- [ ] Automatic gain control

### Low Priority
- [ ] GUI interface
- [ ] Doppler compensation
- [ ] Additional equalizer algorithms
- [ ] Performance optimization
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

*Last updated: 2025-12-07 Session 17*
